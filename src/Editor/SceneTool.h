#pragma once

#include "Editor/EditorTool.h"

#include "Core/AssetGuid.h"

#include <functional>

#include <memory>
#include <string>

// SceneTool — the editor's level-editing workspace, and the home of the panels that used to float
// loose in the shell.
//
// P2-06 of the roadmap: the Outliner, Inspector, Content Browser, Console, Environment, Tools,
// Animation and Stats panels were top-level ImGui windows sharing one global dockspace. As tool
// windows they now live inside this tool's own dockspace, which means their arrangement is saved
// with the tool, they cannot be dragged out into an unrelated workspace, and opening a different
// tool swaps the whole set out instead of piling on top of it.
//
// The panels themselves were not rewritten — they work. This is a change of ownership, not of
// content: each ToolWindow body calls straight into the existing panel's draw(ctx).

namespace tucano {
namespace asset {
class AssetRegistry;
}
struct MaterialAsset;
class Scene;
class Renderer;
class Camera;
struct RendererSettings;
} // namespace tucano

namespace tucano::editor {

struct EditorContext;
class DialogManager;
class PlayMode;

class SceneTool final : public EditorTool {
public:
	SceneTool();
	~SceneTool() override;

	const char* toolTypeName() const override { return "Scene"; }
	const char* icon() const override;

	void onInitialize() override;
	void setupDefaultLayout(uint32_t dockspaceId, float width, float height) override;

	// The tool draws whatever this points at; the host owns it and refreshes the per-frame stats.
	// Null is tolerated — the panels say "no scene" rather than crashing.
	void setContext(EditorContext* context) { m_context = context; }
	EditorContext* context() const { return m_context; }

	// ── Document (C-04) ──
	//
	// Split into "do it to this path" and "ask the user for a path" on purpose: the first half is
	// testable headlessly, the second blocks on a modal dialog and can only be driven by a person.
	// The gate covers the first; the second is a thin wrapper over it.

	// Empties the world and resets the document path. Does not prompt — the caller decides whether
	// unsaved work matters, because only it knows if this is a menu click or a scripted setup.
	void newScene();

	// Both return false and leave the tool untouched on failure; `error()` says why.
	bool saveSceneTo(const std::string& path);
	bool openSceneFrom(const std::string& path);

	// Save to the current document path, or fall through to Save As when there is none. This is
	// what Ctrl+S means, and the difference matters: a scene that has never been saved must not
	// silently invent a file name.
	bool saveScene();

	// Native file dialogs. No-ops when the user cancels.
	void openSceneDialog();
	bool saveSceneAsDialog();

	const std::string& error() const { return m_error; }

	// Where the "you have unsaved changes" prompt goes. The host owns the DialogManager (ToolHost
	// does), so it hands one over; without it the guarded entry points below refuse to discard
	// work rather than discarding it silently.
	void setDialogs(DialogManager* dialogs) { m_dialogs = dialogs; }

	// What the File menu calls. These ask before throwing away unsaved work; newScene() and
	// openSceneFrom() do not, because a script or a test wants the operation, not a question.
	void requestNewScene();
	void requestOpenScene();

	// ── Play mode (I-01) ──
	// Owned by the tool because it snapshots this tool's document. The host fills in `onTick` with
	// whatever its simulation is.
	PlayMode& playMode() { return *m_play; }

	// ── Assets (B-01) ──
	//
	// Indexes the project so references are GUIDs rather than paths. Creating the `.tumeta`
	// sidecars writes into the asset folder, so the caller says whether that is wanted — a viewer
	// opening someone else's project should not litter it.
	size_t scanAssets(const std::string& root, bool createMissingMeta = true);
	asset::AssetRegistry& assets() { return *m_assets; }

	// ── Import (B-03) ──
	//
	// Cooks a source model into `.tuasset` under the project and re-indexes. Returns how many
	// assets were written; 0 means nothing was imported and `error()` says why.
	//
	// The source keeps its own identity: its `.tumeta` sidecar is created first if missing, and
	// every cooked sub-asset gets an id **derived from it**. Re-importing after renaming the source
	// therefore produces the same ids, and scenes that referenced them keep resolving. Importing
	// with the path hash instead — the old behaviour — silently orphans them.
	int importAsset(const std::string& sourcePath);

	// Brings a texture into the project: copies it under `Assets/Textures` when it comes from
	// outside, gives it a sidecar, and re-indexes. Returns its GUID, invalid on failure.
	//
	// A texture needs no cooking to be usable — the loader reads .png/.hdr directly — so "import"
	// here means *acquiring identity*, not converting. Cooking to a GPU format is a later
	// optimisation and would not change what the reference points at.
	asset::AssetGuid importTexture(const std::string& sourcePath);

	// File > Import Asset. No-op when the user cancels.
	void importAssetDialog();

	// ── Materials (B-05) ──
	//
	// Writes a new `.tumat` under the project with its `.tumeta` sidecar, then re-indexes so the
	// pickers can see it immediately. Returns the new asset's GUID, invalid on failure.
	// Two overloads rather than a defaulted argument: `MaterialAsset` is only forward-declared here
	// (it is authoring data, and SceneTool.h is included widely), and a default argument would need
	// the complete type.
	asset::AssetGuid createMaterial(const std::string& relativePath);
	asset::AssetGuid createMaterial(const std::string& relativePath, const MaterialAsset& material);

	// Reads a material asset by identity. False when the GUID is unknown or the file is unreadable.
	bool loadMaterial(const asset::AssetGuid& guid, MaterialAsset& out);
	bool saveMaterial(const asset::AssetGuid& guid, const MaterialAsset& material);

private:
	void drawInspector(EditorContext& context);
	// Fills a SceneEnvironment from whatever the bound context actually has. A host with no
	// renderer still saves and loads its entities.
	void bindEnvironment(void* environment) const;
	void drawViewport(EditorContext& context);
	// The strip of gizmo controls overlaid on the viewport image.
	void drawGizmoToolbar(EditorContext& context);
	void drawSky(EditorContext& context);
	void drawRendering(EditorContext& context);
	void drawPostFx(EditorContext& context);
	void drawTerrain(EditorContext& context);
	void drawWater(EditorContext& context);
	void drawFog(EditorContext& context);
	void drawClouds(EditorContext& context);
	void drawRain(EditorContext& context);

	struct Panels;
	std::unique_ptr<Panels> m_panels;
	EditorContext* m_context = nullptr;
	DialogManager* m_dialogs = nullptr;
	std::unique_ptr<PlayMode> m_play;
	std::unique_ptr<asset::AssetRegistry> m_assets;
	std::string m_assetRoot;
	std::string m_error;
	// Terrain parameters were edited but the landscape has not been rebuilt from them yet. Shown in
	// the panel, because a slider that visibly changes nothing is how people conclude a tool is
	// broken.
	bool m_terrainDirty = false;

	// Runs `action` now when there is nothing to lose, otherwise after the user answers.
	void guardUnsaved(const char* what, std::function<void()> action);
};

} // namespace tucano::editor
