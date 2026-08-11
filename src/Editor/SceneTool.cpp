#include "Editor/SceneTool.h"

#include "Editor/AnimationPanel.h"
#include "Editor/ConsolePanel.h"
#include "Editor/ContentBrowser.h"
#include "Editor/EditorContext.h"
#include "Editor/EnvironmentPanel.h"
#include "Editor/InspectorPanel.h"
#include "Editor/OutlinerPanel.h"
#include "Editor/StatsPanel.h"
#include "Editor/ToolsPanel.h"
#include "Editor/PropertyGrid.h"
#include "Editor/DialogManager.h"
#include "AssetPipeline/AssetImport.h"
#include "Core/TypeSystem/Serialization.h"
#include "Renderer/MaterialAsset.h"
#include "AssetPipeline/AssetRegistry.h"
#include "Editor/PlayMode.h"
#include "Editor/SystemDialogs.h"
#include "Editor/UI/Icons.h"
#include "Editor/ViewportInteraction.h"
#include "ECS/SceneFile.h"
#include "ECS/World.h"
#include "Renderer/Renderer.h"
#include "Generated/Reflection.g.h"

#include <filesystem>

#include <imgui.h>
#include <imgui_internal.h>

namespace tucano::editor {

// Held by pointer so the panel headers stay out of SceneTool.h — they pull in ImGui, the renderer
// and std::filesystem, and every translation unit that merely wants to open the tool would inherit
// all of it.
struct SceneTool::Panels {
	OutlinerPanel outliner;
	InspectorPanel inspector;
	ContentBrowser contentBrowser;
	ConsolePanel console;
	EnvironmentPanel environment;
	ToolsPanel tools;
	AnimationPanel animation;
	StatsPanel stats;
	// One grid per block: each keeps its own filter and column widths, which is what a user expects
	// when they narrow one panel and not the other.
	PropertyGrid skyGrid;
	PropertyGrid renderGrid;
	PropertyGrid postFxGrid;
	PropertyGrid terrainGrid;
	PropertyGrid waterGrid;
	PropertyGrid fogGrid;
	PropertyGrid cloudGrid;
	PropertyGrid rainGrid;

	// Which handle the viewport is offering, and the transform a drag started from. Per tool rather
	// than global: two scene tools open on two documents each keep their own mode.
	ViewportGizmoState gizmo;
};

SceneTool::SceneTool()
    : m_panels(std::make_unique<Panels>()), m_play(std::make_unique<PlayMode>()),
      m_assets(std::make_unique<asset::AssetRegistry>()) {}
SceneTool::~SceneTool() = default;

const char* SceneTool::icon() const { return TUCANO_ICON_FILE_TREE; }

void SceneTool::onInitialize() {
	// Each window is the panel that already existed; the tool only decides where it lives.
	// A null context is drawn as "no scene" by the panels themselves, so there is no guard here.
	const auto withContext = [this](auto&& drawPanel) {
		return [this, drawPanel]() {
			if (m_context == nullptr) {
				ImGui::TextDisabled("No editor context bound.");
				return;
			}
			// Published here rather than by the host: the undo stack, the play mode and the asset
			// index all belong to *this* workspace, and a panel drawn by another tool must not
			// record into them.
			m_context->undo = &undoStack();
			m_context->play = m_play.get();
			m_context->assets = m_assets.get();
			m_context->assetRoot = m_assetRoot;
			// Rebound while not playing because New or Open can replace the world under us, and a
			// snapshot taken against a world that is gone would restore into nothing.
			if (!m_play->isPlaying() && m_context->world != nullptr) {
				ecs::SceneEnvironment environment;
				bindEnvironment(&environment);
				m_play->bind(m_context->world, environment);
			}
			drawPanel(*m_context);
		};
	};

	// First, so it lands in the central node the layout leaves empty for it.
	// noPadding + no scrolling: the image is the window. Padding would leave a border of dock
	// background around the world, and a scrollbar on a viewport is meaningless.
	addWindow("Viewport", withContext([this](EditorContext& c) { drawViewport(c); }),
	          /*noPadding=*/true, /*disableScrolling=*/true);
	addWindow("Outliner", withContext([this](EditorContext& c) { m_panels->outliner.draw(c); }));
	addWindow("Inspector", withContext([this](EditorContext& c) { drawInspector(c); }));
	addWindow("Content Browser",
	          withContext([this](EditorContext& c) { m_panels->contentBrowser.draw(c); }));
	addWindow("Console", withContext([this](EditorContext& c) { m_panels->console.draw(c); }));
	addWindow("Environment", withContext([this](EditorContext& c) { m_panels->environment.draw(c); }));
	// Water and Fog are generated from their reflection data — no UI written per field. This is what
	// P3 was for: adding a setting to WaterParams makes it appear here with its range and tooltip.
	addWindow("Sky", withContext([this](EditorContext& c) { drawSky(c); }));
	addWindow("Rendering", withContext([this](EditorContext& c) { drawRendering(c); }));
	addWindow("Post FX", withContext([this](EditorContext& c) { drawPostFx(c); }));
	addWindow("Terrain", withContext([this](EditorContext& c) { drawTerrain(c); }));
	addWindow("Water", withContext([this](EditorContext& c) { drawWater(c); }));
	addWindow("Fog", withContext([this](EditorContext& c) { drawFog(c); }));
	addWindow("Clouds", withContext([this](EditorContext& c) { drawClouds(c); }));
	addWindow("Rain", withContext([this](EditorContext& c) { drawRain(c); }));
	addWindow("Tools", withContext([this](EditorContext& c) { m_panels->tools.draw(c); }));
	addWindow("Animation", withContext([this](EditorContext& c) { m_panels->animation.draw(c); }));
	addWindow("Stats", withContext([this](EditorContext& c) { m_panels->stats.draw(c); }));
}

void SceneTool::drawInspector(EditorContext& context) {
	m_panels->inspector.setUndoStack(&undoStack());
	m_panels->inspector.setAssetSource(m_assets.get(), m_assetRoot);
	m_panels->inspector.draw(context);
	if (m_panels->inspector.changed()) markDirty();
}

void SceneTool::drawViewport(EditorContext& context) {
	// Tell the host how big the target should be *before* using the image: the panel is the only
	// thing that knows, and the host resizes between the UI and the render.
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const uint32_t width = static_cast<uint32_t>(avail.x > 1.0f ? avail.x : 1.0f);
	const uint32_t height = static_cast<uint32_t>(avail.y > 1.0f ? avail.y : 1.0f);
	context.requestedViewportW = width;
	context.requestedViewportH = height;

	if (context.sceneTexture == 0) {
		// Honest rather than a black rectangle that looks like a broken render: this is what a host
		// that never wired the offscreen target gets.
		ImGui::TextDisabled("No rendered scene bound.");
		return;
	}

	// The image is last frame's target at last frame's size — on the frame a resize lands it is
	// stretched by a few pixels for one frame, which is invisible next to the alternative of
	// stalling the GPU mid-UI to resize.
	const ImVec2 imageMin = ImGui::GetCursorScreenPos();
	ImGui::Image(static_cast<ImTextureID>(context.sceneTexture), ImVec2(static_cast<float>(width),
	                                                                    static_cast<float>(height)));
	context.viewportHovered = ImGui::IsItemHovered();

	// Submitted after the image so it draws on top of it, and grouped so one hover test covers the
	// whole strip — a click on a toolbar button must not also pick whatever is behind it.
	ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
	ImGui::BeginGroup();
	drawGizmoToolbar(context);
	ImGui::EndGroup();
	const bool overToolbar = ImGui::IsItemHovered();

	// The gizmo, in screen coordinates: ImGuizmo hit-tests against io.MousePos, so a rect of
	// (0,0,w,h) would put the handles wherever the window happens not to be.
	bool moved = false;
	const bool usingGizmo =
	    drawViewportGizmo(context, m_panels->gizmo, imageMin.x, imageMin.y,
	                      static_cast<float>(width), static_cast<float>(height), moved);
	if (moved) markDirty();

	// Picking. Suppressed while a handle is held or hovered, otherwise finishing a drag would
	// re-select whatever is behind the object — or nothing, if the drag ended over the sky.
	if (context.viewportHovered && !overToolbar && !usingGizmo && !gizmoIsOver() &&
	    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		pickAtViewportPosition(context, mouse.x - imageMin.x, mouse.y - imageMin.y,
		                       static_cast<float>(width), static_cast<float>(height));
	}
}

void SceneTool::drawGizmoToolbar(EditorContext& context) {
	ViewportGizmoState& gizmo = m_panels->gizmo;

	// Ctrl+1/2/3 rather than the W/E/R every other editor uses: W, E and R are already the host's
	// fly-camera keys, and a shortcut that moves the camera *and* changes the tool is worse than no
	// shortcut. Rebinding them belongs to A-01 (commands with names), where the conflict can be
	// resolved in one place instead of guessed at here.
	const bool takesKeys = ImGui::IsWindowFocused() || context.viewportHovered;
	if (takesKeys && ImGui::GetIO().KeyCtrl) {
		if (ImGui::IsKeyPressed(ImGuiKey_1)) gizmo.operation = GizmoOperation::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_2)) gizmo.operation = GizmoOperation::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_3)) gizmo.operation = GizmoOperation::Scale;
	}

	const auto opButton = [&gizmo](const char* label, GizmoOperation op, const char* tooltip) {
		const bool active = gizmo.operation == op;
		if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button(label)) gizmo.operation = op;
		if (active) ImGui::PopStyleColor();
		ImGui::SetItemTooltip("%s", tooltip);
		ImGui::SameLine();
	};

	opButton(TUCANO_ICON_ARROW_ALL, GizmoOperation::Translate, "Move  (Ctrl+1)");
	opButton(TUCANO_ICON_ROTATE_3D_VARIANT, GizmoOperation::Rotate, "Rotate  (Ctrl+2)");
	opButton(TUCANO_ICON_RESIZE, GizmoOperation::Scale, "Scale  (Ctrl+3)");

	// Scale is only meaningful along the object's own axes, so the choice is disabled rather than
	// offered and quietly ignored.
	const bool localOnly = gizmo.operation == GizmoOperation::Scale;
	ImGui::BeginDisabled(localOnly);
	if (ImGui::Button(localOnly || gizmo.space == GizmoSpace::Local ? "Local" : "World")) {
		gizmo.space = gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s", "Axes of the world, or of the object");
	ImGui::SameLine();

	ImGui::Checkbox("Snap", &gizmo.snapEnabled);
	if (gizmo.snapEnabled) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		switch (gizmo.operation) {
			case GizmoOperation::Translate:
				ImGui::DragFloat("##snapT", &gizmo.translateSnap, 0.05f, 0.01f, 100.0f, "%.2f m");
				break;
			case GizmoOperation::Rotate:
				ImGui::DragFloat("##snapR", &gizmo.rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
				break;
			case GizmoOperation::Scale:
				ImGui::DragFloat("##snapS", &gizmo.scaleSnap, 0.01f, 0.01f, 10.0f, "%.2f");
				break;
		}
	}
}

void SceneTool::drawSky(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->skyGrid.setUndoStack(&undoStack());
	m_panels->skyGrid.drawFilterBox();
	// Remembered across frames because the catalogue only has to be rebuilt when the path *changes*,
	// and rebuilding it every frame would re-read the file every frame.
	const std::string before = context.renderer->sky().starCatalogPath;
	if (m_panels->skyGrid.draw(context.renderer->sky())) {
		markDirty();
		// Writing the field is not the whole action: the star textures are built from the file, so
		// the value and what is drawn would disagree until the next restart.
		if (context.renderer->sky().starCatalogPath != before) {
			context.renderer->buildStarCatalogTextures();
			context.logInfo("Star catalogue reloaded from " + context.renderer->sky().starCatalogPath);
		}
	}
}

void SceneTool::drawRendering(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->renderGrid.setUndoStack(&undoStack());
	m_panels->renderGrid.drawFilterBox();
	if (m_panels->renderGrid.draw(context.renderer->settings())) markDirty();
}

void SceneTool::drawTerrain(EditorContext& context) {
	if (context.terrainParams == nullptr || !context.applyTerrain) {
		ImGui::TextDisabled("This host has no terrain.");
		return;
	}

	// The parameters *are* the landscape, so editing one does not change what is drawn until it is
	// rebuilt — generating a 512² heightmap and its collision mesh is not something to do sixty
	// times a second while a slider is dragged. Hence an explicit Generate, and a note saying so
	// rather than leaving people to wonder why the mountains did not move.
	m_panels->terrainGrid.setUndoStack(&undoStack());
	m_panels->terrainGrid.drawFilterBox();
	const bool edited = m_panels->terrainGrid.draw(*context.terrainParams);
	if (edited) {
		markDirty();
		m_terrainDirty = true;
	}

	ImGui::Separator();
	if (ImGui::Button(TUCANO_ICON_TERRAIN "  Generate")) {
		if (context.applyTerrain(*context.terrainParams)) {
			m_terrainDirty = false;
			markDirty();
			context.logInfo("Terrain rebuilt.");
		} else {
			context.logError("Could not build the terrain.");
		}
	}
	ImGui::SameLine();
	if (m_terrainDirty) {
		ImGui::TextDisabled("edited — press Generate to rebuild");
	} else {
		ImGui::TextDisabled("up to date");
	}
}

void SceneTool::drawPostFx(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->postFxGrid.setUndoStack(&undoStack());
	m_panels->postFxGrid.drawFilterBox();
	if (m_panels->postFxGrid.draw(context.renderer->postFx())) markDirty();
}

void SceneTool::drawWater(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->waterGrid.setUndoStack(&undoStack());
	m_panels->waterGrid.drawFilterBox();
	if (m_panels->waterGrid.draw(context.renderer->water())) markDirty();
}

void SceneTool::drawFog(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->fogGrid.setUndoStack(&undoStack());
	m_panels->fogGrid.drawFilterBox();
	if (m_panels->fogGrid.draw(context.renderer->fog())) markDirty();
}

void SceneTool::drawClouds(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->cloudGrid.setUndoStack(&undoStack());
	m_panels->cloudGrid.drawFilterBox();
	if (m_panels->cloudGrid.draw(context.renderer->clouds())) markDirty();
}

void SceneTool::drawRain(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->rainGrid.setUndoStack(&undoStack());
	m_panels->rainGrid.drawFilterBox();
	if (m_panels->rainGrid.draw(context.renderer->rain())) markDirty();
}

size_t SceneTool::scanAssets(const std::string& root, bool createMissingMeta) {
	m_assetRoot = root;
	asset::AssetRegistry::ScanOptions options;
	options.createMissingMeta = createMissingMeta;
	return m_assets->scanProject(root, options);
}

int SceneTool::importAsset(const std::string& sourcePath) {
	m_error.clear();
	if (m_assetRoot.empty()) {
		m_error = "no project root scanned";
		return 0;
	}
	if (!isImportableModel(sourcePath)) {
		m_error = "not an importable model: " + sourcePath;
		return 0;
	}

	// The source needs an identity before anything is derived from it. A file dropped in from
	// outside the project has no sidecar yet, so one is written now rather than at the next scan —
	// otherwise the cooked assets would be derived from nothing and fall back to the path hash.
	const asset::RegistryEntry* entry = m_assets->findByPath(sourcePath);
	asset::AssetGuid sourceGuid = entry != nullptr ? entry->guid : asset::AssetGuid{};
	if (!sourceGuid.valid()) {
		const std::string metaPath = asset::AssetRegistry::metaPathFor(sourcePath);
		sourceGuid = asset::AssetRegistry::readMetaGuid(metaPath);
		if (!sourceGuid.valid()) {
			sourceGuid = asset::AssetRegistry::generateGuid();
			if (!asset::AssetRegistry::writeMeta(metaPath, sourceGuid, asset::AssetType::Mesh)) {
				m_error = "cannot write the sidecar for " + sourcePath;
				return 0;
			}
		}
	}

	const int written = importModelAsTuasset(sourcePath, m_assetRoot + "/Imported", sourceGuid);
	if (written == 0) {
		m_error = "nothing was imported from " + sourcePath;
		return 0;
	}

	// The new .tuasset files exist only on disk until the index knows about them.
	scanAssets(m_assetRoot, /*createMissingMeta=*/true);
	return written;
}

asset::AssetGuid SceneTool::createMaterial(const std::string& relativePath) {
	return createMaterial(relativePath, MaterialAsset{});
}

asset::AssetGuid SceneTool::createMaterial(const std::string& relativePath,
                                          const MaterialAsset& material) {
	m_error.clear();
	if (m_assetRoot.empty()) {
		m_error = "no project root scanned";
		return {};
	}

	const std::string fullPath = m_assetRoot + "/" + relativePath;
	// The directory may not exist yet — a material is usually made *into* a new folder.
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(fullPath).parent_path(), ec);

	const TypeInfo* type = TypeRegistry::instance().find(TypeID{"MaterialAsset"});
	if (type == nullptr) {
		m_error = "MaterialAsset is not registered";
		return {};
	}
	if (!saveToFile(fullPath, *type, &material, &m_error)) return {};

	// Identity before indexing: the sidecar is what makes the GUID survive a later rename, and
	// writing it here means the material has one from its first moment rather than from the next
	// scan that happens to run with createMissingMeta on.
	const std::string metaPath = asset::AssetRegistry::metaPathFor(fullPath);
	asset::AssetGuid guid = asset::AssetRegistry::readMetaGuid(metaPath);
	if (!guid.valid()) {
		guid = asset::AssetRegistry::generateGuid();
		if (!asset::AssetRegistry::writeMeta(metaPath, guid, asset::AssetType::Material)) {
			m_error = "cannot write the sidecar for " + fullPath;
			return {};
		}
	}

	scanAssets(m_assetRoot, /*createMissingMeta=*/true);
	return guid;
}

bool SceneTool::loadMaterial(const asset::AssetGuid& guid, MaterialAsset& out) {
	m_error.clear();
	const asset::RegistryEntry* entry = m_assets->find(guid);
	if (entry == nullptr) {
		m_error = "no asset with that id";
		return false;
	}
	const TypeInfo* type = TypeRegistry::instance().find(TypeID{"MaterialAsset"});
	if (type == nullptr) {
		m_error = "MaterialAsset is not registered";
		return false;
	}
	return loadFromFile(m_assetRoot + "/" + entry->relativePath, *type, &out, &m_error);
}

bool SceneTool::saveMaterial(const asset::AssetGuid& guid, const MaterialAsset& material) {
	m_error.clear();
	const asset::RegistryEntry* entry = m_assets->find(guid);
	if (entry == nullptr) {
		m_error = "no asset with that id";
		return false;
	}
	const TypeInfo* type = TypeRegistry::instance().find(TypeID{"MaterialAsset"});
	if (type == nullptr) {
		m_error = "MaterialAsset is not registered";
		return false;
	}
	// Saved to the path the *index* holds, not to a path the caller remembered: the file may have
	// been renamed since, and the GUID is what still points at the right one.
	return saveToFile(m_assetRoot + "/" + entry->relativePath, *type, &material, &m_error);
}

asset::AssetGuid SceneTool::importTexture(const std::string& sourcePath) {
	m_error.clear();
	if (m_assetRoot.empty()) {
		m_error = "no project root scanned";
		return {};
	}

	const std::filesystem::path source(sourcePath);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(source, ec)) {
		m_error = "no such file: " + sourcePath;
		return {};
	}
	if (asset::AssetRegistry::typeFromExtension(source.extension().string()) !=
	    asset::AssetType::Texture) {
		m_error = "not a texture: " + sourcePath;
		return {};
	}

	// Copy in only when it is outside the project. A file already under the root is *already* the
	// project's; copying it would produce a second asset with a second identity for one image.
	std::filesystem::path target = source;
	const std::filesystem::path root(m_assetRoot);
	const std::filesystem::path relative = std::filesystem::relative(source, root, ec);
	const bool inside = !ec && !relative.empty() && relative.native().rfind(L"..", 0) != 0;
	if (!inside) {
		const std::filesystem::path folder = root / "Textures";
		std::filesystem::create_directories(folder, ec);
		target = folder / source.filename();
		// Never silently overwrite: two different images with the same file name is a real thing,
		// and losing one of them to an import is not recoverable.
		if (std::filesystem::exists(target, ec)) {
			m_error = "a texture named " + source.filename().string() + " is already in the project";
			return {};
		}
		std::filesystem::copy_file(source, target, ec);
		if (ec) {
			m_error = "cannot copy " + sourcePath + " into the project";
			return {};
		}
	}

	const std::string metaPath = asset::AssetRegistry::metaPathFor(target.string());
	asset::AssetGuid guid = asset::AssetRegistry::readMetaGuid(metaPath);
	if (!guid.valid()) {
		guid = asset::AssetRegistry::generateGuid();
		if (!asset::AssetRegistry::writeMeta(metaPath, guid, asset::AssetType::Texture)) {
			m_error = "cannot write the sidecar for " + target.string();
			return {};
		}
	}

	scanAssets(m_assetRoot, /*createMissingMeta=*/true);
	return guid;
}

void SceneTool::importAssetDialog() {
	const std::string path = openFileDialog("Import Asset",
	                                        {{"Models", "*.gltf;*.glb;*.fbx"},
	                                         {"Textures", "*.png;*.jpg;*.tga;*.dds;*.hdr;*.exr"},
	                                         {"All files", "*.*"}});
	if (path.empty()) return;

	// One entry point, two paths: a model is cooked into .tuasset, a texture only needs to enter
	// the project and gain an identity. Deciding here rather than making the user pick the right
	// menu item is the difference between a tool and a quiz.
	const std::filesystem::path source(path);
	if (asset::AssetRegistry::typeFromExtension(source.extension().string()) ==
	    asset::AssetType::Texture) {
		importTexture(path);
		return;
	}
	importAsset(path);
}

// ── Document (C-04) ─────────────────────────────────────────────────────────

void SceneTool::bindEnvironment(void* environmentPtr) const {
	auto& environment = *static_cast<ecs::SceneEnvironment*>(environmentPtr);
	environment = {};
	if (m_context == nullptr || m_context->renderer == nullptr) return;
	environment.water = &m_context->renderer->water();
	environment.fog = &m_context->renderer->fog();
	environment.clouds = &m_context->renderer->clouds();
	environment.rain = &m_context->renderer->rain();
	environment.sky = &m_context->renderer->sky();
	environment.postFx = &m_context->renderer->postFx();
	// Straight through from the host: the editor does not own a terrain, it edits the numbers the
	// host builds one from.
	environment.terrain = m_context->terrainParams;
	environment.applyTerrain = m_context->applyTerrain;
	// The HDRI is the one environment value that costs something to apply, so it travels as a value
	// plus the operation. `reloadIBL` already keeps the previous lighting when the file cannot be
	// used, which is exactly the contract the loader wants.
	environment.hdriPath = &m_context->renderer->settings().hdriPath;
	Renderer* renderer = m_context->renderer;
	environment.applyHdri = [renderer](const std::string& path) { return renderer->reloadIBL(path); };
}

void SceneTool::newScene() {
	m_error.clear();
	if (m_context != nullptr && m_context->world != nullptr) {
		ecs::SceneEnvironment environment;
		bindEnvironment(&environment);
		// An empty scene is an empty *file*, loaded normally — so "New" and "open a scene with no
		// entities" cannot drift apart in what they reset.
		ecs::sceneFromJson(R"({"format":"tuscene","version":1,"entities":[]})", *m_context->world,
		                   environment, &m_error);
		m_context->clearSelection();
	}
	setDocumentPath({});
	save(); // clears the dirty flag: a brand-new scene has nothing unsaved in it
}

bool SceneTool::saveSceneTo(const std::string& path) {
	m_error.clear();
	if (m_context == nullptr || m_context->world == nullptr) {
		m_error = "no world bound to the editor";
		return false;
	}
	if (m_play != nullptr && m_play->isPlaying()) {
		// The world currently holds the *running* game, not the authored scene. Saving it would
		// write bullet positions and knocked-over crates over the level.
		m_error = "stop play mode before saving";
		return false;
	}
	ecs::SceneEnvironment environment;
	bindEnvironment(&environment);
	if (!ecs::saveScene(path, *m_context->world, environment, &m_error)) return false;

	setDocumentPath(path);
	save();
	return true;
}

bool SceneTool::openSceneFrom(const std::string& path) {
	m_error.clear();
	if (m_context == nullptr || m_context->world == nullptr) {
		m_error = "no world bound to the editor";
		return false;
	}
	ecs::SceneEnvironment environment;
	bindEnvironment(&environment);
	if (!ecs::loadScene(path, *m_context->world, environment, &m_error)) return false;

	// A load can succeed *and* have something to say: a component the file could not fit, or an HDRI
	// that would not open. Those are warnings, not failures — the scene is loaded either way — so
	// they go to the Console instead of into `error()`, which means "why this did not happen".
	// Without this the report goes nowhere and the scene quietly opens lit by the wrong sky.
	if (!m_error.empty()) {
		m_context->logWarn("Scene loaded with warnings: " + m_error);
		m_error.clear();
	}

	// The old selection pointed into the world that was just replaced.
	m_context->clearSelection();
	setDocumentPath(path);
	save();
	// Undo history belongs to the document that was open; keeping it would let Ctrl+Z "undo" into
	// a scene that is no longer loaded.
	undoStack().clear();
	return true;
}

bool SceneTool::saveScene() {
	if (documentPath().empty()) return saveSceneAsDialog();
	return saveSceneTo(documentPath());
}

void SceneTool::guardUnsaved(const char* what, std::function<void()> action) {
	if (!isDirty()) {
		action();
		return;
	}
	if (m_dialogs == nullptr) {
		// No way to ask, so the safe answer is no. Discarding someone's work because the host
		// forgot to wire a dialog manager is not a trade worth making.
		m_error = std::string("unsaved changes; ") + what + " cancelled";
		return;
	}
	m_dialogs->choice(
	    "Unsaved changes", displayName() + " has unsaved changes.", "Save", "Discard",
	    [this, action = std::move(action)](DialogManager::Choice choice) {
		    switch (choice) {
			    case DialogManager::Choice::Primary:
				    // Only proceed if the save actually worked — a failed save followed by a discard
				    // is exactly the data loss the prompt exists to prevent.
				    if (saveScene()) action();
				    return;
			    case DialogManager::Choice::Secondary:
				    action();
				    return;
			    case DialogManager::Choice::Cancel:
				    return;
		    }
	    });
}

void SceneTool::requestNewScene() {
	guardUnsaved("New Scene", [this] { newScene(); });
}

void SceneTool::requestOpenScene() {
	guardUnsaved("Open Scene", [this] { openSceneDialog(); });
}

void SceneTool::openSceneDialog() {
	const std::string path =
	    openFileDialog("Open Scene", {{"Tucano scene", "*.tuscene"}, {"All files", "*.*"}});
	if (!path.empty()) openSceneFrom(path);
}

bool SceneTool::saveSceneAsDialog() {
	const std::string path =
	    saveFileDialog("Save Scene", {{"Tucano scene", "*.tuscene"}}, "Untitled.tuscene");
	if (path.empty()) return false;
	return saveSceneTo(path);
}

void SceneTool::setupDefaultLayout(uint32_t dockspaceId, float width, float height) {
	(void)width;
	(void)height;

	// The arrangement the shell used to impose globally, now owned by the tool: hierarchy over
	// properties on the right, browser and log along the bottom, viewport in the middle.
	ImGuiID centre = static_cast<ImGuiID>(dockspaceId);
	ImGuiID right = 0;
	ImGuiID rightLower = 0;
	ImGuiID bottom = 0;
	ImGuiID bottomRight = 0;

	ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, &right, &centre);
	ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, &rightLower, &right);
	ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, &bottom, &centre);
	ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f, &bottomRight, &bottom);

	// Viewport is deliberately not docked: whatever is left after the splits is the central node,
	// and a window docked there is exactly what the layout wants the scene to fill.
	dockWindow("Viewport", centre);
	dockWindow("Outliner", right);
	dockWindow("Inspector", rightLower);
	dockWindow("Stats", rightLower);
	dockWindow("Content Browser", bottom);
	dockWindow("Console", bottom);
	dockWindow("Environment", bottomRight);
	dockWindow("Sky", bottomRight);
	dockWindow("Rendering", bottomRight);
	dockWindow("Water", bottomRight);
	dockWindow("Fog", bottomRight);
	dockWindow("Clouds", bottomRight);
	dockWindow("Rain", bottomRight);
	dockWindow("Tools", bottomRight);
	dockWindow("Animation", bottomRight);

	// Draw order would open the right-hand node on Stats and the bottom-right one on Animation,
	// which is nobody's idea of where a fresh editor should start. Only one window can hold focus,
	// so the Inspector wins: it is the one that answers "what am I looking at".
	focusWindow("Inspector");
}

} // namespace tucano::editor
