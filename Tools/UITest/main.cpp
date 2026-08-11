// Gate for ImGui rasterisation (roadmap P0-02).
//
// Why this exists: the draw data ImGui produces was verified correct while parts of it never
// reached the screen — the menu bar, whole draw lists, and the top edge of quads went missing, in a
// way that changed with the frame's vertex count. That class of bug is invisible to a human glance
// and comes back silently, so it needs an assertion, not an eyeball.
//
// The pattern is deliberately boring: solid rectangles at known coordinates, one per draw-list kind
// ImGui emits (background, window, foreground), including one that starts at y=0 because that is
// where the failure showed. The window is small and the scene is a single clear, so a full run takes
// about a second — the point is to be fast enough to iterate against.
//
//   TucanoUITest [--out <png>]
//
// Exit code is the failure count, so CI can gate on it.

#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Runtime/DebugUI.h"
#include "Editor/UI/Fonts.h"
#include "Editor/EditorTool.h"
#include "Core/TypeSystem/PropertyPath.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "Core/TypeSystem/Serialization.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/EntityManager.h"
#include "ECS/SceneFile.h"
#include "Renderer/MaterialAsset.h"
#include "Editor/ContentBrowser.h"
#include "Editor/OutlinerPanel.h"
#include "Editor/SceneCommands.h"
#include "Editor/ViewportInteraction.h"
#include "AssetPipeline/AssetRegistry.h"
#include "AssetPipeline/AssetResolver.h"
#include "Renderer/Material.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sky/SkyParams.h"
#include "Editor/PlayMode.h"
#include "AssetPipeline/TucanoAsset.h"
#include "ECS/PhysicsSync.h"
#include "ECS/RenderSync.h"
#include "ECS/World.h"
#include "Editor/PropertyGrid.h"
#include "Editor/SceneTool.h"
#include "Editor/EditorContext.h"
#include "Editor/InspectorPanel.h"
#include "Editor/TypeEditingRules.h"
#include "Renderer/Scene.h"
#include "Generated/Reflection.g.h"
#include "Editor/DialogManager.h"
#include "Editor/ToolHost.h"
#include "Editor/UndoStack.h"
#include "Editor/UI/CurveEditor.h"
#include "Editor/UI/Notifications.h"
#include "Editor/UI/Pickers.h"
#include "Editor/UI/TreeListView.h"
#include "Editor/UI/Widgets.h"
#include "Editor/UI/Icons.h"
#include "Runtime/Screenshot.h"

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

using namespace tucano;

namespace tucano {
// Defined by the generated Reflection.g.cpp. Named here so the gate can assert the anchor is real
// rather than trusting that it is.
extern const int kGeneratedReflectionAnchor;
} // namespace tucano

namespace {

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;
// Frames to run before sampling: ImGui settles window sizing over the first couple of frames.
constexpr int kSampleFrame = 8;

int g_failures = 0;

struct Probe {
	const char* name;
	int x0, y0, x1, y1;      // expected rect, ImGui coordinates
	uint8_t r, g, b;         // expected colour
};

// One probe per draw-list kind. The y=0 starts are the whole point: that band is what went missing.
const Probe kProbes[] = {
    {"background list", 20, 0, 120, 100, 255, 0, 0},
    {"window list", 160, 0, 260, 100, 0, 255, 0},
    {"foreground list", 300, 0, 400, 100, 0, 0, 255},
    {"window list (offset)", 440, 120, 540, 220, 255, 0, 255},
    {"control (native RHI)", 40, 300, 140, 400, 255, 255, 0},
};

void check(bool cond, const std::string& what) {
	std::cout << (cond ? "  [ok]   " : "  [FAIL] ") << what << "\n";
	if (!cond) ++g_failures;
}

bool pixelMatches(const std::vector<uint8_t>& rgba, int x, int y, const Probe& p) {
	if (x < 0 || y < 0 || x >= static_cast<int>(kWidth) || y >= static_cast<int>(kHeight)) return false;
	const size_t i = (static_cast<size_t>(y) * kWidth + x) * 4;
	// Generous tolerance: the point is "did this quad rasterise", not colour accuracy.
	const int dr = std::abs(static_cast<int>(rgba[i + 0]) - p.r);
	const int dg = std::abs(static_cast<int>(rgba[i + 1]) - p.g);
	const int db = std::abs(static_cast<int>(rgba[i + 2]) - p.b);
	return dr < 40 && dg < 40 && db < 40;
}

// Measures the actual drawn extent of a probe so a failure says *how* it is wrong (clipped at the
// top? shifted? absent?) rather than just "not found".
void verifyProbe(const std::vector<uint8_t>& rgba, const Probe& p) {
	int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1, count = 0;
	for (int y = 0; y < static_cast<int>(kHeight); ++y) {
		for (int x = 0; x < static_cast<int>(kWidth); ++x) {
			if (pixelMatches(rgba, x, y, p)) {
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
				++count;
			}
		}
	}
	if (count == 0) {
		check(false, std::string(p.name) + ": nothing rasterised (expected " + std::to_string(p.x0) + "," +
		                 std::to_string(p.y0) + " to " + std::to_string(p.x1) + "," + std::to_string(p.y1) + ")");
		return;
	}
	// ImGui rects are half-open, so the last covered pixel is x1-1 / y1-1.
	const bool ok = minX == p.x0 && minY == p.y0 && maxX == p.x1 - 1 && maxY == p.y1 - 1;
	check(ok, std::string(p.name) + ": drawn (" + std::to_string(minX) + "," + std::to_string(minY) + ")-(" +
	              std::to_string(maxX) + "," + std::to_string(maxY) + "), expected (" + std::to_string(p.x0) + "," +
	              std::to_string(p.y0) + ")-(" + std::to_string(p.x1 - 1) + "," + std::to_string(p.y1 - 1) + ")");
}

// A 1x1x1 cube centred on the origin, for the picking checks. Built here rather than loaded so the
// bounds under test are known exactly — `Mesh::create` recomputes each submesh's AABB from the
// vertices it actually indexes, which is what the ray is tested against.
std::shared_ptr<Mesh> makeUnitCube(rhi::Device& device) {
	std::vector<Vertex> vertices;
	vertices.reserve(8);
	for (int i = 0; i < 8; ++i) {
		Vertex v{};
		v.position = {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
		v.normal = glm::normalize(v.position);
		vertices.push_back(v);
	}
	// Winding does not matter here: nothing rasterises this, the ray only reads the bounds.
	const std::vector<uint32_t> indices = {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1,
	                                       2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
	SubMesh sub{};
	sub.indexOffset = 0;
	sub.indexCount = static_cast<uint32_t>(indices.size());
	return Mesh::create(device, vertices, indices, {sub});
}

// Minimal tool, so the EditorTool contract is tested without dragging in a real one.
class ProbeTool final : public editor::EditorTool {
public:
	explicit ProbeTool(const char* type) : m_type(type) {}
	const char* toolTypeName() const override { return m_type; }
	void onInitialize() override {
		addWindow("Alpha", [] {});
		addWindow("Beta", [] {});
	}

private:
	const char* m_type;
};

} // namespace

namespace tucano {
// Two plain structs to reflect over. Nested on purpose: walking into a struct the grid has never
// heard of is the whole point of having a registry.
struct ProbeInner {
	float x = 1.0f;
	float y = 2.0f;
};
struct ProbeOuter {
	bool enabled = true;
	float roughness = 0.25f;
	int32_t count = 7;
	ProbeInner inner;
	float samples[4] = {10.0f, 11.0f, 12.0f, 13.0f};
};
} // namespace tucano

TUCANO_REFLECT_TYPE_BEGIN(ProbeInner)
	TUCANO_PROPERTY(x, Float, .label = "X")
	TUCANO_PROPERTY(y, Float, .label = "Y")
TUCANO_REFLECT_TYPE_END(ProbeInner)

TUCANO_REFLECT_TYPE_BEGIN(ProbeOuter)
	TUCANO_PROPERTY(enabled, Bool, .label = "Enabled")
	TUCANO_PROPERTY(roughness, Float, .label = "Roughness", .minValue = 0.0f, .maxValue = 1.0f)
	TUCANO_PROPERTY(count, Int32)
	TUCANO_PROPERTY_STRUCT(inner, ProbeInner, .label = "Inner")
TUCANO_REFLECT_TYPE_END(ProbeOuter)

int main(int argc, char** argv) {
	std::string outPath;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--out" && i + 1 < argc) outPath = argv[++i];
	}

	// Unbuffered: when a check crashes the process, the lines already printed are the only clue to
	// where it got, and a buffered stdout throws exactly those away.
	std::cout << std::unitbuf;
	std::cout << "TucanoUITest - ImGui rasterisation gate\n\n";

	try {
		Window window({kWidth, kHeight, "Tucano UI Gate"});
		auto device = rhi::Device::create(true);
		auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), false);

		DebugUI ui;
		ui.init(window, *device);

		// Two toasts, posted before the loop so they are past their fade-in by the sampled frame.
		editor::ui::notifyInfo("Gate: info toast");
		editor::ui::notifyError("Gate: error toast");

		std::vector<uint8_t> rgba;
		uint32_t sampledWidth = 0;
		uint32_t sampledHeight = 0;

		for (int frame = 0; frame <= kSampleFrame; ++frame) {
			window.pollEvents();
			ui.beginFrame();

			// Draw the probes. Each goes through a different ImGui draw list so a backend bug that
			// only affects one of them is still caught.
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(20, 0), ImVec2(120, 100), IM_COL32(255, 0, 0, 255));
			ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(300, 0), ImVec2(400, 100), IM_COL32(0, 0, 255, 255));
			// Text samples opaque texels of the font atlas; a filled rect samples the atlas' white
			// pixel. If text draws and rects do not, the atlas is fine and the white-pixel UV is not.
			ImGui::GetForegroundDrawList()->AddText(ImVec2(20, 240), IM_COL32(255, 255, 255, 255), "TUCANO");
			// Icon glyph: proves the Material Design font merged into the atlas AND that the >U+FFFF
			// codepoints survive (they need IMGUI_USE_WCHAR32; without it this draws nothing).
			ImGui::GetForegroundDrawList()->AddText(ImVec2(20, 280), IM_COL32(255, 255, 255, 255),
			                                        TUCANO_ICON_CONTENT_SAVE);
			// One label per size/weight, each with an icon glued to it: catches a face that failed to
			// load and a face whose icon merge silently did not happen.
			for (int f = 0; f < static_cast<int>(editor::Font::Count); ++f) {
				editor::ScopedFont scoped{static_cast<editor::Font>(f)};
				ImGui::GetForegroundDrawList()->AddText(
				    ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(220.0f, 120.0f + f * 21.0f),
				    IM_COL32(255, 255, 255, 255), TUCANO_ICON_PALETTE " Tucano");
			}

			// A borderless, transparent window so only our own rects land in its draw list. Padding and
			// border go to zero so the window's clip rect starts at 0,0 — otherwise the probe at y=0 is
			// legitimately clipped to y=1 and the gate would be asserting ImGui's border, not the bug.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
			ImGui::Begin("##probes", nullptr,
			             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
			                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
			                 ImGuiWindowFlags_NoBringToFrontOnFocus);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(160, 0), ImVec2(260, 100), IM_COL32(0, 255, 0, 255));
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(440, 120), ImVec2(540, 220), IM_COL32(255, 0, 255, 255));
			ImGui::End();
			ImGui::PopStyleVar(2);

			editor::ui::drawNotifications();

			auto* cmd = device->beginFrame();
			auto& bb = swapChain->backBuffer();
			// Deliberately not black: a black frame cannot be told apart from a frame that was never
			// rendered into, and that ambiguity already cost a debugging session once.
			const float clear[4] = {0.10f, 0.20f, 0.40f, 1.0f};
			cmd->transition(bb, rhi::ResourceState::RenderTarget);
			cmd->clearRenderTarget(bb, clear);
			// Control: a rectangle painted through Tucano's own path. If this one is exact while the
			// ImGui probes are not, the fault is in the ImGui integration; if both are wrong, it is
			// the command list / present path underneath both.
			const float ctrl[4] = {1.0f, 1.0f, 0.0f, 1.0f};
			cmd->clearRenderTargetRect(bb, ctrl, 40, 300, 100, 100);
			ui.endFrame(*cmd, bb);

			ScreenshotPending shot;
			if (frame == kSampleFrame) {
				shot = beginScreenshot(*device, *cmd, bb);
				sampledWidth = shot.width;
				sampledHeight = shot.height;
			}
			cmd->transition(bb, rhi::ResourceState::Present);
			device->endFrame(*swapChain);
			if (shot.impl) {
				device->waitIdle();
				rgba = readScreenshotPixels(shot);
				if (!outPath.empty()) finalizeScreenshot(shot, outPath);
			}
		}

		// ── ToolHost lifetime ────────────────────────────────────────────────
		{
			editor::ToolHost host;
			auto clean = std::make_unique<ProbeTool>("Clean");
			auto dirty = std::make_unique<ProbeTool>("Dirty");
			dirty->setDocumentPath("Assets/Materials/A.tmat");
			editor::EditorTool* cleanTool = host.open(std::move(clean));
			editor::EditorTool* dirtyTool = host.open(std::move(dirty));
			dirtyTool->markDirty();

			// Opening the same document again must focus the existing tool, not fork it.
			auto duplicate = std::make_unique<ProbeTool>("Dirty");
			duplicate->setDocumentPath("Assets/Materials/A.tmat");
			editor::EditorTool* dup = host.open(std::move(duplicate));
			check(dup == dirtyTool && host.tools().size() == 2,
			      "open() do mesmo documento foca o existente em vez de duplicar");

			check(host.hasUnsavedTools(), "hasUnsavedTools ve a ferramenta suja");

			host.requestClose(cleanTool);
			host.requestClose(dirtyTool);

			// Lifetime advances without drawing anything — which is the point of update() being
			// separate. The prompt itself is UI and is verified visually in the editor.
			host.update();

			check(host.tools().size() == 1, "tool limpa fecha direto; a suja fica esperando resposta");
			check(host.tools().front()->isDirty(), "quem sobrou e a ferramenta com alteracoes pendentes");

			check(host.saveAll() && !host.hasUnsavedTools(), "saveAll limpa o estado sujo de todas");
		}

		// ── UndoStack ────────────────────────────────────────────────────────
		{
			editor::UndoStack stack;
			float roughness = 0.4f;

			check(!stack.canUndo() && !stack.canRedo(), "pilha nasce vazia");

			stack.pushValue("Roughness", &roughness, 0.4f, 0.6f);
			roughness = 0.6f;
			check(stack.canUndo() && stack.undoName() == "Roughness", "undoName nomeia o que sera desfeito");

			check(stack.undo() && roughness == 0.4f, "undo restaura o valor anterior");
			check(stack.canRedo() && stack.redo() && roughness == 0.6f, "redo reaplica");

			// Editing after undoing forks the history; the abandoned branch must go.
			stack.undo();
			check(stack.canRedo(), "undo deixa algo para refazer");
			stack.pushValue("Roughness", &roughness, roughness, 0.9f);
			check(!stack.canRedo(), "editar apos desfazer descarta o ramo abandonado");

			// Coalescing: a drag is one step, not one per frame.
			editor::UndoStack drag;
			float v = 0.0f;
			for (int i = 1; i <= 20; ++i) {
				drag.pushValue("Slider", &v, v, static_cast<float>(i));
				v = static_cast<float>(i);
			}
			check(drag.undoDepth() == 1, "arrasto contiguo vira um unico passo (" +
			                                 std::to_string(drag.undoDepth()) + ")");
			check(drag.undo() && v == 0.0f, "desfazer o arrasto volta ao valor de antes de comecar");

			// breakMerge ends the gesture, so the next edit is its own step.
			editor::UndoStack gestures;
			float g = 0.0f;
			gestures.pushValue("Slider", &g, 0.0f, 1.0f);
            g = 1.0f;
			gestures.breakMerge();
			gestures.pushValue("Slider", &g, 1.0f, 2.0f);
			g = 2.0f;
			check(gestures.undoDepth() == 2, "breakMerge separa dois gestos");

			// Different fields never merge, even back to back.
			editor::UndoStack fields;
			float a = 0.0f;
			float b = 0.0f;
			fields.pushValue("A", &a, 0.0f, 1.0f);
			fields.pushValue("B", &b, 0.0f, 1.0f);
			check(fields.undoDepth() == 2, "campos diferentes nao se fundem");

			// Compound: several changes, one step, undone in reverse order.
			editor::UndoStack comp;
			int order = 0;
			int first = -1;
			int second = -1;
			{
				editor::UndoStack::Compound c(comp, "Delete selection");
				comp.push(std::make_unique<editor::LambdaAction>(
				    "one", [&] { first = order++; }, [] {}));
				comp.push(std::make_unique<editor::LambdaAction>(
				    "two", [&] { second = order++; }, [] {}));
			}
			check(comp.undoDepth() == 1 && comp.undoName() == "Delete selection",
			      "compound vira um passo so, com o nome da operacao");
			comp.undo();
			check(second == 0 && first == 1, "compound desfaz na ordem inversa");

			// An empty compound must not become an Edit menu entry that undoes nothing.
			editor::UndoStack emptyComp;
			{ editor::UndoStack::Compound c(emptyComp, "Nothing"); }
			check(!emptyComp.canUndo(), "compound vazio nao vira passo");

			// Depth limit drops the oldest, not the newest.
			editor::UndoStack bounded;
			bounded.setMaxDepth(3);
			float x = 0.0f;
			for (int i = 1; i <= 10; ++i) {
				bounded.pushValue(std::string("Field") + std::to_string(i), &x, 0.0f, 1.0f);
			}
			check(bounded.undoDepth() == 3, "limite de profundidade respeitado");
			check(bounded.undoName() == "Field10", "o passo mais recente e o que sobrevive");
		}

		// ── Atalhos Ctrl+Z / Ctrl+Y ──────────────────────────────────────────
		// The roadmap's criterion literally. Keys are injected into ImGui's IO, so the wiring is
		// exercised end to end instead of being taken on trust.
		{
			editor::ToolHost host;
			auto tool = std::make_unique<ProbeTool>("Undoable");
			editor::EditorTool* t = host.open(std::move(tool)); // open() focuses it
			float value = 1.0f;
			t->undoStack().pushValue("Value", &value, 1.0f, 2.0f);
			value = 2.0f;

			// One frame of the editor, with whatever keys are currently held.
			const auto frame = [&] {
				window.pollEvents();
				ui.beginFrame();
				host.handleUndoShortcuts();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			};

			// Ctrl stays held across the whole sequence, the way a hand does. Releasing and pressing a
			// key inside one frame makes ImGui defer the second transition (input trickling), so the
			// modifier would read as up exactly when the shortcut is checked.
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			frame();

			const auto tap = [&](ImGuiKey key) {
				io.AddKeyEvent(key, true);
				frame();
				io.AddKeyEvent(key, false);
				frame();
			};

			tap(ImGuiKey_Z);
			check(value == 1.0f, "Ctrl+Z desfaz a edicao de propriedade");

			tap(ImGuiKey_Y);
			check(value == 2.0f, "Ctrl+Y refaz");

			tap(ImGuiKey_Z);
			io.AddKeyEvent(ImGuiMod_Shift, true);
			frame();
			tap(ImGuiKey_Z);
			check(value == 2.0f, "Ctrl+Shift+Z tambem refaz");

			io.AddKeyEvent(ImGuiMod_Shift, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			frame();
		}

		// ── DialogManager ────────────────────────────────────────────────────
		{
			editor::DialogManager dialogs;
			check(!dialogs.hasActiveDialog(), "gerenciador nasce sem dialogo");

			int resolved = 0;
			// Bodies that close themselves on the first frame: enough to drive the whole state
			// machine (open, resolve, dequeue, open the next) without simulating a click.
			dialogs.custom("First", [&] {
				++resolved;
				return true;
			});
			dialogs.custom("Second", [&] {
				++resolved;
				return true;
			});
			check(dialogs.queued() == 2, "pedidos enfileiram: " + std::to_string(dialogs.queued()));

			const auto dialogFrame = [&] {
				window.pollEvents();
				ui.beginFrame();
				dialogs.draw();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			};

			// A couple of frames each: the popup opens on one frame and its body runs on the next.
			for (int i = 0; i < 8 && dialogs.hasActiveDialog(); ++i) {
				dialogFrame();
			}
			check(resolved == 2, "os dois dialogos resolveram, um de cada vez (" +
			                         std::to_string(resolved) + ")");
			check(!dialogs.hasActiveDialog(), "fila esvazia depois de resolver");

			// clear() drops without answering — for teardown.
			editor::DialogManager other;
			other.message("A", "text");
			other.message("B", "text");
			check(other.queued() == 2, "message() enfileira");
			other.clear();
			check(!other.hasActiveDialog() && other.queued() == 0, "clear() descarta a fila");
		}

		// ── PropertyGrid (P4-01) ─────────────────────────────────────────────
		// Most of the grid is pixels, so what is asserted here is the contract around it: it refuses
		// types it knows nothing about, and drawing a real engine struct is crash-free and does not
		// invent edits nobody made.
		{
			editor::PropertyGrid grid;
			editor::UndoStack undo;
			grid.setUndoStack(&undo);

			WaterParams water;
			const float levelBefore = water.waterLevel;
			bool anyChange = false;
			for (int i = 0; i < 3; ++i) {
				window.pollEvents();
				ui.beginFrame();
				ImGui::Begin("##gridProbe");
				if (grid.draw(water)) anyChange = true;
				// An unregistered type is a *compile* error, not a runtime false: draw<T>() needs
				// TypeName<T>, which only the registration macro defines — a stronger guarantee than
				// any test. What is checked here is the runtime edge: a type with nothing to show.
				// Inside the frame, because the grid draws its "nothing to edit" line.
				if (i == 0) {
					TypeInfo empty;
					empty.name = "Empty";
					check(!grid.draw(empty, &water), "tipo sem propriedades nao finge ter editor");
				}
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}
			check(!anyChange, "sem input, o grid nao reporta alteracao");
			check(water.waterLevel == levelBefore, "sem input, o valor nao e tocado");
			check(!undo.canUndo(), "sem input, nada entra na pilha de undo");

			// Uma linha de enum desenhada de verdade. `drawEnum` era codigo morto ate agora — nao
			// havia enum registrado — e a busca do rotulo roda a cada frame, aberta a combo ou nao.
			// Sem isto, um TypeID errado so apareceria quando alguem clicasse.
			{
				ecs::LightComponent light;
				light.type = LightType::Spot;
				bool enumChanged = false;
				for (int i = 0; i < 2; ++i) {
					window.pollEvents();
					ui.beginFrame();
					ImGui::Begin("##enumProbe");
					if (grid.draw(light)) enumChanged = true;
					ImGui::End();
					auto* cmd = device->beginFrame();
					auto& bb = swapChain->backBuffer();
					cmd->transition(bb, rhi::ResourceState::RenderTarget);
					ui.endFrame(*cmd, bb);
					cmd->transition(bb, rhi::ResourceState::Present);
					device->endFrame(*swapChain);
				}
				check(!enumChanged, "desenhar uma linha de enum sem input nao reporta alteracao");
				check(light.type == LightType::Spot, "e nao mexe no valor");
			}

			// Read-only is presentation, not a lock on the data — but it must at least be honoured
			// as state the caller set.
			grid.setReadOnly(true);
			check(grid.readOnly(), "modo somente leitura registrado");
		}

		// ── Reflexao gerada (P3-03) ──────────────────────────────────────────
		// Material, Transform e RenderObject nao tem mais um arquivo de reflexao escrito a mao:
		// vem das anotacoes nos proprios headers, via Tools/Reflector. O que se afirma aqui e o que
		// so o runtime pode dizer — que o registro chegou vivo e que o layout bate com a struct.
		{
			// A ancora e o que impede o linker de descartar o objeto gerado. Ele registra a partir
			// de variaveis inline e ninguem o referencia; sem isto, os grids ficariam vazios sem uma
			// linha de erro em lugar nenhum.
			check(kGeneratedReflectionAnchor == 1,
			      "a ancora mantem o objeto gerado ligado ao binario");

			struct LayoutCase {
				const char* name;
				size_t size;
				size_t alignment;
			};
			const LayoutCase cases[] = {
			    {"Material", sizeof(Material), alignof(Material)},
			    {"Transform", sizeof(Transform), alignof(Transform)},
			    {"RenderObject", sizeof(RenderObject), alignof(RenderObject)},
			};

			int layoutOk = 0;
			int boundsOk = 0;
			for (const LayoutCase& c : cases) {
				const TypeInfo* info = TypeRegistry::instance().find(TypeID{c.name});
				if (info == nullptr) continue;
				// O gerador emite offsetof/sizeof em vez de numeros — se algum dia passar a assar
				// valores, e aqui que aparece.
				if (info->size == c.size && info->alignment == c.alignment) ++layoutOk;

				bool inside = true;
				for (size_t i = 0; i < info->propertyCount; ++i) {
					const PropertyInfo& p = info->properties[i];
					if (static_cast<size_t>(p.offset) + p.size > c.size) inside = false;
				}
				if (inside) ++boundsOk;
			}
			check(layoutOk == 3,
			      "o TypeInfo gerado bate com sizeof/alignof da struct (" + std::to_string(layoutOk) +
			          "/3)");
			check(boundsOk == 3,
			      "toda propriedade gerada cabe dentro da struct (" + std::to_string(boundsOk) + "/3)");
		}

		// ── TypeEditingRules (P4-03) ─────────────────────────────────────────
		// Regras que dependem do *valor*, nao da declaracao. Sao avaliaveis sem frame nenhum, que e
		// o que permite afirma-las de verdade em vez de olhar pixels.
		{
			const editor::TypeEditingRules* materialRules = editor::TypeEditingRules::find(TypeID{"Material"});
			const editor::TypeEditingRules* waterRules = editor::TypeEditingRules::find(TypeID{"WaterParams"});
			check(materialRules != nullptr, "as regras de Material sao registradas sozinhas");
			check(waterRules != nullptr, "as regras de WaterParams sao registradas sozinhas");

			const TypeInfo* materialType = TypeRegistry::instance().find(TypeID{"Material"});
			const TypeInfo* waterType = TypeRegistry::instance().find(TypeID{"WaterParams"});

			if (materialRules != nullptr && materialType != nullptr) {
				// Um nome de propriedade errado numa regra falha em silencio: a regra so nunca dispara.
				check(materialRules->unmatchedRules(*materialType).empty(),
				      "toda regra de Material aponta para uma propriedade que existe");

				const PropertyInfo* cutoff = materialType->findProperty("alphaCutoff");
				const PropertyInfo* ccRough = materialType->findProperty("clearcoatRoughness");
				Material material;

				material.alphaMask = false;
				check(cutoff != nullptr &&
				          materialRules->hidden(*cutoff, &material) == editor::RuleState::Yes,
				      "alphaCutoff some quando alphaMask esta desligado");
				material.alphaMask = true;
				check(cutoff != nullptr && materialRules->hidden(*cutoff, &material) == editor::RuleState::No,
				      "alphaCutoff volta quando alphaMask liga");

				material.clearcoat = 0.0f;
				check(ccRough != nullptr &&
				          materialRules->hidden(*ccRough, &material) == editor::RuleState::Yes,
				      "clearcoatRoughness some sem clearcoat");
				material.clearcoat = 0.5f;
				check(ccRough != nullptr && materialRules->hidden(*ccRough, &material) == editor::RuleState::No,
				      "clearcoatRoughness volta com clearcoat");

				// Uma propriedade sem regra tem que sair Unhandled, nao "editavel": senao a regra de
				// um campo passaria por cima da metadata de todos os outros.
				const PropertyInfo* metallic = materialType->findProperty("metallicFactor");
				check(metallic != nullptr &&
				          materialRules->hidden(*metallic, &material) == editor::RuleState::Unhandled &&
				          materialRules->locked(*metallic, &material) == editor::RuleState::Unhandled,
				      "propriedade sem regra fica Unhandled nos dois estados");
			}

			if (waterRules != nullptr && waterType != nullptr) {
				check(waterRules->unmatchedRules(*waterType).empty(),
				      "toda regra de WaterParams aponta para uma propriedade que existe");

				WaterParams water;
				const PropertyInfo* enabled = waterType->findProperty("enabled");
				const PropertyInfo* level = waterType->findProperty("waterLevel");

				water.enabled = false;
				check(level != nullptr && waterRules->locked(*level, &water) == editor::RuleState::Yes,
				      "o master switch trava os campos abaixo dele");
				// A excecao existe para o proprio switch: sem ela ele se trancaria desligado e nao
				// haveria como religar.
				check(enabled != nullptr &&
				          waterRules->locked(*enabled, &water) != editor::RuleState::Yes,
				      "o proprio master switch nao se tranca");
				// Travar nao e esconder: as 29 linhas continuam legiveis, so nao editaveis.
				check(level != nullptr && waterRules->hidden(*level, &water) == editor::RuleState::Unhandled,
				      "travado nao implica escondido");

				water.enabled = true;
				check(level != nullptr && waterRules->locked(*level, &water) == editor::RuleState::No,
				      "religar o master switch destrava os campos");
			}

			// Duas condicoes sobre o mesmo campo tem que valer as duas — noiseStrength depende de
			// `enabled` (pela regra geral) e de `volumetric` (pela propria).
			const editor::TypeEditingRules* fogRules = editor::TypeEditingRules::find(TypeID{"FogParams"});
			const TypeInfo* fogType = TypeRegistry::instance().find(TypeID{"FogParams"});
			if (fogRules != nullptr && fogType != nullptr) {
				check(fogRules->unmatchedRules(*fogType).empty(),
				      "toda regra de FogParams aponta para uma propriedade que existe");

				FogParams fog;
				const PropertyInfo* noise = fogType->findProperty("noiseStrength");
				fog.enabled = true;
				fog.volumetric = true;
				check(noise != nullptr && fogRules->locked(*noise, &fog) == editor::RuleState::No,
				      "noiseStrength editavel com fog ligada e volumetrica");
				fog.volumetric = false;
				check(noise != nullptr && fogRules->locked(*noise, &fog) == editor::RuleState::Yes,
				      "noiseStrength trava no caminho analitico");
				fog.volumetric = true;
				fog.enabled = false;
				check(noise != nullptr && fogRules->locked(*noise, &fog) == editor::RuleState::Yes,
				      "noiseStrength trava com a fog desligada, mesmo volumetrica");
			}

			// E3-02: chuva e nuvens sao dados refletidos como qualquer outro bloco.
			for (const char* name : {"RainParams", "CloudParams"}) {
				const TypeInfo* info = TypeRegistry::instance().find(TypeID{name});
				check(info != nullptr && info->propertyCount > 0,
				      std::string(name) + " esta refletido e tem propriedades");
			}
			{
				const TypeInfo* rain = TypeRegistry::instance().find(TypeID{"RainParams"});
				const TypeInfo* cloud = TypeRegistry::instance().find(TypeID{"CloudParams"});
				check(rain != nullptr && rain->size == sizeof(RainParams) &&
				          cloud != nullptr && cloud->size == sizeof(CloudParams),
				      "o TypeInfo de chuva e nuvens bate com a struct");
			}

			// Um tipo sem regra nenhuma nao pode inventar uma.
			check(editor::TypeEditingRules::find(TypeID{"Transform"}) == nullptr,
			      "tipo sem regras declaradas nao ganha regras");
		}

		// ── Serializacao por reflection (C-01) ───────────────────────────────
		// O editor so vira editor quando o que se ajusta sobrevive a fechar o processo. O que se
		// afirma aqui e o round-trip exato e as quatro regras de versao do formato.
		{
			// Round-trip de um tipo real, com aninhamento e array.
			ProbeOuter original;
			original.enabled = false;
			original.roughness = 0.123456789f;   // valor que so volta igual com precisao suficiente
			original.count = -4242;
			original.inner.x = 3.14159265f;
			original.inner.y = -2.71828182f;

			const std::string text = serializeToJson(original);
			check(!text.empty() && text.front() == '{', "serializeToJson devolve um objeto JSON");

			ProbeOuter restored;  // comeca nos defaults, nao numa copia
			std::string err;
			check(deserializeFromJson(text, restored, &err), "deserializeFromJson aceita o proprio texto");
			check(err.empty(), "round-trip nao reporta problema de campo (" + err + ")");

			check(restored.enabled == original.enabled && restored.count == original.count,
			      "bool e int voltam identicos");
			// Igualdade exata de proposito: se o float nao volta bit a bit, a cena deriva um pouco a
			// cada vez que e aberta e salva.
			check(restored.roughness == original.roughness, "float volta identico (sem deriva)");
			check(restored.inner.x == original.inner.x && restored.inner.y == original.inner.y,
			      "struct aninhada volta identica");

			// A chave e o `name` da propriedade, nunca o `label`. Renomear "Roughness" na UI nao
			// pode orfanar o dado gravado.
			check(text.find("\"roughness\"") != std::string::npos &&
			          text.find("\"Roughness\"") == std::string::npos,
			      "a chave gravada e o nome da propriedade, nao o label");

			// Regra 2: campo ausente mantem o valor atual. E o que faz um save antigo carregar numa
			// struct que ganhou campos novos, em vez de zerar tudo.
			{
				ProbeOuter partial;
				partial.count = 999;
				partial.roughness = 0.5f;
				check(deserializeFromJson("{ \"enabled\": false }", partial, &err),
				      "carrega um objeto com um campo so");
				check(partial.count == 999 && partial.roughness == 0.5f,
				      "campo ausente no arquivo mantem o valor atual");
				check(partial.enabled == false, "campo presente e aplicado");
			}

			// Regra 3: chave desconhecida e ignorada — um save de uma versao mais nova ainda abre.
			{
				ProbeOuter forward;
				check(deserializeFromJson("{ \"campoDoFuturo\": 7, \"count\": 12 }", forward, &err),
				      "chave desconhecida nao quebra o carregamento");
				check(forward.count == 12, "as chaves conhecidas continuam sendo aplicadas");
			}

			// Tipo errado: o campo e pulado e reportado, sem custar o resto do arquivo.
			{
				ProbeOuter mixed;
				mixed.count = 5;
				err.clear();
				check(deserializeFromJson("{ \"count\": \"texto\", \"enabled\": false }", mixed, &err),
				      "tipo errado num campo nao falha o arquivo inteiro");
				check(mixed.count == 5, "o campo de tipo errado fica com o valor anterior");
				check(mixed.enabled == false, "os outros campos do mesmo arquivo sao aplicados");
				check(!err.empty(), "o campo pulado e reportado em err");
			}

			// JSON invalido falha de verdade — isso nao e um campo ruim, e um arquivo ruim.
			{
				ProbeOuter bad;
				check(!deserializeFromJson("{ isto nao e json", bad, &err),
				      "JSON invalido falha o carregamento");
				check(!deserializeFromJson("[1, 2, 3]", bad, &err),
				      "raiz que nao e objeto falha o carregamento");
			}

			// Um bloco real da engine, com 29 campos, vetor e cor.
			{
				WaterParams water;
				water.waterLevel = 12.5f;
				water.waveAmplitude = 0.375f;
				water.absorption = glm::vec3(0.1f, 0.25f, 0.5f);
				water.enabled = false;

				WaterParams back;
				check(deserializeFromJson(serializeToJson(water), back, &err),
				      "WaterParams faz round-trip");
				check(back.waterLevel == water.waterLevel && back.waveAmplitude == water.waveAmplitude,
				      "os floats de WaterParams voltam identicos");
				check(back.absorption == water.absorption, "o vec3 volta identico");
				check(back.enabled == water.enabled, "o master switch volta identico");
			}

			// Arquivo: escrita atomica e leitura de volta.
			{
				const std::string path = "uitest_serial_probe.json";
				Material material;
				material.name = "Probe \"quoted\" \\ material";  // exercita o escape
				material.metallicFactor = 0.6180339887f;
				material.baseColorFactor = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);

				check(saveToFile(path, material, &err), "saveToFile grava (" + err + ")");
				Material loaded;
				check(loadFromFile(path, loaded, &err), "loadFromFile le de volta");
				check(loaded.name == material.name, "string com aspas e barra sobrevive ao round-trip");
				check(loaded.metallicFactor == material.metallicFactor, "float do arquivo volta identico");
				check(loaded.baseColorFactor == material.baseColorFactor, "a cor volta identica");

				check(!loadFromFile("nao_existe_este_arquivo.json", loaded, &err),
				      "arquivo inexistente falha em vez de fingir sucesso");
				std::remove(path.c_str());
			}
		}

		// ── Cena: componentes de autoria e .tuscene (C-02a, C-03) ────────────
		// O passo 9 da Definition of Done: salvar, fechar, abrir, tudo igual. E o que se afirma
		// aqui — nao que o formato existe, mas que o round-trip preserva o que foi autorado.
		{
			using namespace tucano::ecs;

			// Os componentes de autoria sao refletidos como qualquer outro tipo.
			for (const char* name : {"NameComponent", "MeshComponent", "LightComponent",
			                          "TransformComponent"}) {
				const TypeInfo* info = TypeRegistry::instance().find(TypeID{name});
				check(info != nullptr && info->propertyCount > 0,
				      std::string(name) + " esta refletido");
			}

			// O historico de interpolacao e transient: e o estado do frame anterior de uma execucao
			// que nao existe mais quando o arquivo for aberto.
			{
				const TypeInfo* t = TypeRegistry::instance().find(TypeID{"TransformComponent"});
				const PropertyInfo* prev = t != nullptr ? t->findProperty("prevPosition") : nullptr;
				check(prev != nullptr && prev->meta.transient,
				      "prevPosition e transient (nao vai para o arquivo)");
			}

			// FixedString: o ECS exige trivially copyable, entao nome e caminho sao buffers inline.
			check(std::is_trivially_copyable_v<NameComponent> &&
			          std::is_trivially_copyable_v<MeshComponent> &&
			          std::is_trivially_copyable_v<LightComponent>,
			      "os componentes de autoria continuam trivially copyable");
			{
				NameString truncated;
				check(!truncated.assign(std::string(200, 'x')),
				      "FixedString reporta quando o texto nao coube");
				check(truncated.size() == NameString::capacity() - 1,
				      "FixedString trunca ate a capacidade e mantem o terminador");
			}

			// Round-trip de uma cena real.
			World world;
			// Valores esperados em locais, nao lidos de volta pelo ponteiro do componente: adicionar
			// um componente migra a entidade de archetype e invalida qualquer ponteiro anterior.
			const glm::vec3 kTorrePos(1.5f, -2.25f, 3.125f);
			const glm::vec3 kTorreScale(2.0f);
			const Entity mesh = world.create();
			auto* meshName = world.add<NameComponent>(mesh);
			meshName->name = "Torre";
			auto* meshTransform = world.add<TransformComponent>(mesh);
			meshTransform->position = kTorrePos;
			meshTransform->scale = kTorreScale;
			meshTransform->prevPosition = glm::vec3(999.0f);  // transient: nao pode voltar
			auto* meshRef = world.add<MeshComponent>(mesh);
			// Identity, not a path (CP-31): the value has to survive the file being renamed.
			asset::AssetGuid kTorreMesh;
			kTorreMesh.hi = 0x0123456789abcdefULL;
			kTorreMesh.lo = 0xfedcba9876543210ULL;
			meshRef->mesh = kTorreMesh;
			meshRef->visible = false;

			const Entity lamp = world.create();
			world.add<NameComponent>(lamp)->name = "Poste";
			world.add<TransformComponent>(lamp)->position = glm::vec3(0.0f, 4.0f, 0.0f);
			auto* light = world.add<LightComponent>(lamp);
			light->type = LightType::Spot;
			light->color = glm::vec3(0.9f, 0.8f, 0.6f);
			light->intensity = 12.5f;
			light->outerCone = 42.5f;

			// Uma entidade so com estado de runtime nao deve aparecer no arquivo.
			const Entity runtimeOnly = world.create();
			world.add<PhysicsBodyComponent>(runtimeOnly);

			WaterParams water;
			water.waterLevel = 7.25f;
			water.enabled = false;
			FogParams fog;
			fog.density = 0.0375f;
			// E-02: a atmosfera e o passo 7 da Definition of Done, e ate agora a hora do dia morria
			// com o processo. `SkyParams` so existe separado de `RendererSettings` para isto (E-01).
			SkyParams sky;
			sky.timeOfDay = 0.8125f;
			sky.useBrunetonAtmosphere = false;
			// E-05: o bloco de nuvem so passou a valer alguma coisa quando `CloudParams` virou dono
			// unico da camada — antes o renderer o sobrescrevia com o gemeo de RendererSettings no
			// frame seguinte ao load.
			CloudParams clouds;
			clouds.coverage = 0.99f;
			clouds.storminess = 0.77f;
			std::string hdri = "IBL/noite.hdr";
			SceneEnvironment environment;
			environment.water = &water;
			environment.fog = &fog;
			environment.sky = &sky;
			environment.clouds = &clouds;
			environment.hdriPath = &hdri;

			const std::string text = sceneToJson(world, environment);
			check(text.find("\"format\": \"tuscene\"") != std::string::npos,
			      "a cena grava o cabecalho de formato");
			check(text.find("\"Torre\"") != std::string::npos &&
			          text.find("\"Poste\"") != std::string::npos,
			      "as entidades autoradas estao no arquivo");
			check(text.find("prevPosition") == std::string::npos,
			      "o campo transient nao foi gravado");
			check(text.find("physics_body") == std::string::npos,
			      "entidade so de runtime nao entra no arquivo");

			// Carrega num mundo novo, como abrir o editor de novo.
			World reopened;
			WaterParams water2;
			FogParams fog2;
			SkyParams sky2;
			CloudParams clouds2;
			// O que o editor teria carregado antes de abrir a cena. Tem que ser diferente do que
			// esta no arquivo, senao o teste nao distingue "aplicou" de "ja estava assim".
			std::string hdri2 = "IBL/default.hdr";
			int hdriApplied = 0;
			std::string hdriAsked;
			SceneEnvironment environment2;
			environment2.water = &water2;
			environment2.fog = &fog2;
			environment2.sky = &sky2;
			environment2.clouds = &clouds2;
			environment2.hdriPath = &hdri2;
			environment2.applyHdri = [&](const std::string& path) {
				++hdriApplied;
				hdriAsked = path;
				return true;
			};

			std::string sceneErr;
			check(sceneFromJson(text, reopened, environment2, &sceneErr),
			      "a cena carrega (" + sceneErr + ")");
			check(sceneErr.empty(), "carregar nao reporta problema de campo");

			int found = 0;
			bool torreOk = false;
			bool posteOk = false;
			for (EntityManager::Archetype& archetype : reopened.entities().archetypes()) {
				for (EntityManager::Chunk& chunk : archetype.chunks) {
					for (uint32_t i = 0; i < chunk.count; ++i) {
						const Entity e = chunk.entities[i];
						auto* n = reopened.get<NameComponent>(e);
						if (n == nullptr) continue;
						++found;
						if (n->name == std::string_view("Torre")) {
							auto* t = reopened.get<TransformComponent>(e);
							auto* m = reopened.get<MeshComponent>(e);
							torreOk = t != nullptr && m != nullptr &&
							          t->position == kTorrePos && t->scale == kTorreScale &&
							          m->mesh == kTorreMesh &&
							          m->visible == false &&
							          // transient: volta no default, nao no valor gravado
							          t->prevPosition != glm::vec3(999.0f);
						} else if (n->name == std::string_view("Poste")) {
							auto* l = reopened.get<LightComponent>(e);
							posteOk = l != nullptr && l->type == LightType::Spot && l->intensity == 12.5f &&
							          l->outerCone == 42.5f && l->color == glm::vec3(0.9f, 0.8f, 0.6f);
						}
					}
				}
			}
			check(found == 2, "as duas entidades autoradas voltaram (" + std::to_string(found) + ")");
			check(torreOk, "o mesh voltou com transform, caminho e visibilidade identicos");
			check(posteOk, "a luz voltou com tipo, cor, intensidade e cone identicos");
			check(water2.waterLevel == 7.25f && water2.enabled == false,
			      "o bloco de ambiente (agua) voltou identico");
			check(fog2.density == 0.0375f, "o bloco de ambiente (fog) voltou identico");

			// ── E-02: atmosfera e HDRI ────────────────────────────────────────
			check(sky2.timeOfDay == 0.8125f, "a hora do dia sobrevive a salvar e reabrir");
			check(clouds2.coverage == 0.99f && clouds2.storminess == 0.77f,
			      "e a camada de nuvem tambem (E-05: agora ela tem um dono so)");
			check(sky2.useBrunetonAtmosphere == false,
			      "e o modelo de atmosfera tambem, nao so os floats");
			check(hdriApplied == 1, "o HDRI da cena foi aplicado uma vez, nao a cada bloco lido");
			check(hdriAsked == "IBL/noite.hdr", "com o caminho que estava no arquivo");
			check(hdri2 == "IBL/noite.hdr", "e o valor corrente passou a ser o da cena");

			// Reaplicar custa recozinhar o IBL, entao carregar a mesma cena de novo — ou dar Stop
			// no play mode, que restaura este bloco de um snapshot — nao pode disparar de novo.
			{
				World again;
				check(sceneFromJson(text, again, environment2, &sceneErr), "a mesma cena carrega de novo");
				check(hdriApplied == 1,
				      "e o HDRI que ja esta carregado nao e recozinhado (o Stop do play mode passa aqui)");
			}

			// Um HDRI que nao carrega nao pode apagar a iluminacao: reloadIBL ja mantem a anterior,
			// e o loader tem que respeitar isso em vez de gravar o caminho quebrado por cima.
			{
				World falha;
				SkyParams sky3;
				std::string hdri3 = "IBL/o_que_estava.hdr";
				SceneEnvironment environment3;
				environment3.sky = &sky3;
				environment3.hdriPath = &hdri3;
				environment3.applyHdri = [](const std::string&) { return false; };
				std::string falhaErr;
				check(sceneFromJson(text, falha, environment3, &falhaErr),
				      "a cena carrega mesmo com o HDRI recusado");
				check(hdri3 == "IBL/o_que_estava.hdr",
				      "e o caminho corrente fica como estava, em vez de apontar para o que nao abriu");
				check(falhaErr.find("HDRI") != std::string::npos,
				      "com o problema reportado, nao engolido");
				check(sky3.timeOfDay == 0.8125f, "e o resto do ambiente carrega assim mesmo");
			}

			// Bloco ausente mantem o que esta rodando (regra 2 da serializacao), que e o que faz uma
			// cena salva por um build sem ceu abrir aqui sem zerar a atmosfera.
			{
				World antiga;
				SkyParams skyCorrente;
				skyCorrente.timeOfDay = 0.25f;
				SceneEnvironment environment4;
				environment4.sky = &skyCorrente;
				check(sceneFromJson(R"({"format":"tuscene","version":1,"entities":[],"environment":{}})",
				                    antiga, environment4, &sceneErr),
				      "uma cena sem bloco de ceu carrega");
				check(skyCorrente.timeOfDay == 0.25f, "e nao zera a atmosfera que ja estava montada");
			}

			// Arquivo em disco, que e o caminho real do editor.
			{
				const std::string path = "uitest_scene_probe.tuscene";
				check(saveScene(path, world, environment, &sceneErr), "saveScene grava");
				World fromDisk;
				check(loadScene(path, fromDisk, environment2, &sceneErr), "loadScene le de volta");
				check(fromDisk.liveCount() == 2, "o mundo lido do disco tem as duas entidades");
				std::remove(path.c_str());
			}

			// Um arquivo que nao e cena deve falhar, nao carregar meia cena.
			{
				World other;
				check(!sceneFromJson("{ \"format\": \"outra\" }", other, {}, &sceneErr),
				      "arquivo que nao e tuscene falha");
				check(!sceneFromJson("{ \"format\": \"tuscene\", \"version\": 999 }", other, {}, &sceneErr),
				      "versao futura e recusada em vez de lida pela metade");
			}
		}

		// ── Documento do SceneTool: New / Open / Save (C-04) ─────────────────
		// A metade com caminho explicito e testavel; os dialogos nativos bloqueiam esperando uma
		// pessoa e ficam de fora. O que se afirma aqui e o ciclo que o menu File dispara.
		{
			using namespace tucano::ecs;

			editor::SceneTool tool;
			editor::EditorContext context;
			World docWorld;
			context.world = &docWorld;
			tool.setContext(&context);
			tool.initialize();

			const std::string docPath = "uitest_doc_probe.tuscene";

			// Uma cena com algo dentro, e uma selecao apontando para ela.
			const Entity e = docWorld.create();
			docWorld.add<NameComponent>(e)->name = "Documento";
			docWorld.add<TransformComponent>(e)->position = glm::vec3(3.0f, 0.0f, -1.0f);
			context.selectedEntity = e;
			tool.markDirty();

			check(tool.isDirty(), "editar marca o documento como sujo");
			check(tool.documentPath().empty(), "cena nova comeca sem caminho");

			check(tool.saveSceneTo(docPath), "saveSceneTo grava (" + tool.error() + ")");
			check(!tool.isDirty(), "salvar limpa o estado sujo");
			check(tool.documentPath() == docPath, "salvar adota o caminho como documento");

			// New esvazia o mundo e solta a selecao — que apontava para o que acabou de sumir.
			tool.newScene();
			check(docWorld.liveCount() == 0, "New esvazia o mundo");
			check(tool.documentPath().empty(), "New esquece o caminho do documento");
			check(!context.hasSelectedEntity(), "New solta a selecao");
			check(!tool.isDirty(), "cena nova nao nasce suja");

			// Reabrir traz tudo de volta.
			check(tool.openSceneFrom(docPath), "openSceneFrom carrega (" + tool.error() + ")");
			check(docWorld.liveCount() == 1, "abrir devolve a entidade");
			check(tool.documentPath() == docPath, "abrir adota o caminho");
			check(!tool.isDirty(), "abrir nao deixa o documento sujo");
			check(!context.hasSelectedEntity(),
			      "abrir solta a selecao (ela apontava para o mundo anterior)");

			// Uma cena que carrega **com ressalva** — um campo que o arquivo nao consegue entregar, ou
			// um HDRI que nao abre (E-02). O load teve sucesso, entao `error()` fica vazio; mas o
			// aviso nao pode se perder, senao a cena abre errada em silencio.
			{
				const std::string warnPath = "uitest_doc_warn.tuscene";
				std::ofstream(warnPath) << R"({"format":"tuscene","version":1,"entities":[
			{"name":{"name":"Aviso"},"transform":{"position":[0,0,0]},
			 "light":{"intensity":"nao e um numero"}}]})";
				const size_t logBefore = context.log.size();
				check(tool.openSceneFrom(warnPath), "uma cena com um campo problematico ainda abre");
				check(tool.error().empty(), "e nao reporta falha, porque nao falhou");
				bool warned = false;
				for (size_t i = logBefore; i < context.log.size(); ++i) {
					if (context.log[i].level == editor::LogEntry::Level::Warning) warned = true;
				}
				check(warned, "mas o aviso chega ao Console em vez de sumir junto com o erro limpo");
				std::remove(warnPath.c_str());
				// Volta ao documento que o resto do bloco assume.
				check(tool.openSceneFrom(docPath), "e reabrir o documento anterior funciona");
			}

			// Um caminho que nao existe falha e diz por que, sem tocar no documento aberto.
			check(!tool.openSceneFrom("nao_existe.tuscene"), "abrir arquivo inexistente falha");
			check(!tool.error().empty(), "a falha explica o motivo");
			check(tool.documentPath() == docPath, "uma abertura que falha nao troca o documento");
			check(docWorld.liveCount() == 1, "uma abertura que falha nao mexe no mundo");

			// Sem mundo ligado, salvar recusa em vez de gravar um arquivo vazio por cima.
			{
				editor::SceneTool orphan;
				editor::EditorContext noWorld;
				orphan.setContext(&noWorld);
				orphan.initialize();
				check(!orphan.saveSceneTo(docPath), "salvar sem mundo recusa");
				check(!orphan.error().empty(), "a recusa explica o motivo");
			}

			std::remove(docPath.c_str());
		}

		// ── Outliner e Inspector sobre entidades (C-02c) ──────────────────────
		// Sao pixels, entao o que se afirma e o contrato: qual caminho o painel escolhe e o que ele
		// faz quando a selecao aponta para o que ja nao existe.
		{
			using namespace tucano::ecs;

			editor::EditorContext context;
			check(!context.hasSelectedEntity(), "sem mundo ligado nao ha selecao de entidade");

			World world;
			context.world = &world;
			const Entity e = world.create();
			world.add<NameComponent>(e)->name = "Selecionavel";
			context.selectedEntity = e;
			check(context.hasSelectedEntity(), "com mundo e entidade, a selecao vale");

			world.destroy(e);
			check(!world.alive(context.selectedEntity),
			      "a entidade destruida deixa a selecao pendurada");

			editor::InspectorPanel inspector;
			editor::OutlinerPanel outliner;
			for (int i = 0; i < 2; ++i) {
				window.pollEvents();
				ui.beginFrame();
				ImGui::Begin("##entityPanelsProbe");
				// O inspector precisa detectar a selecao morta e limpa-la, em vez de ler memoria de
				// uma entidade que nao existe mais.
				inspector.draw(context);
				outliner.draw(context);
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}
			check(!context.hasSelectedEntity(),
			      "o inspector solta uma selecao que aponta para entidade morta");
			check(!inspector.changed(), "sem input, o inspector de entidade nao reporta alteracao");
		}

		// ── Comandos de cena com undo (C-05) ─────────────────────────────────
		// Antes disso o Outliner so sabia deletar, sem volta — Duplicate/Rename eram `{}` vazios.
		{
			using namespace tucano::ecs;

			World world;
			editor::UndoStack undo;
			editor::EditorContext context;
			context.world = &world;
			context.undo = &undo;

			// Criar seleciona o que foi criado: e nele que a proxima acao deve cair.
			const Entity created = editor::createEntity(context, "Primeira");
			check(world.alive(created), "createEntity cria a entidade");
			check(context.selectedEntity == created, "criar seleciona o que foi criado");
			check(world.get<NameComponent>(created) != nullptr &&
			          world.get<TransformComponent>(created) != nullptr,
			      "a entidade nasce com nome e transform");
			check(undo.canUndo(), "criar entra na pilha de undo");

			undo.undo();
			check(!world.alive(created), "undo de criar destroi a entidade");
			undo.redo();
			check(world.liveCount() == 1, "redo traz a entidade de volta");

			// O id NAO sobrevive ao undo: o EntityManager atribui ids e nao tem "criar com este id".
			// Isto esta registrado como limitacao, e o gate afirma a limitacao em vez de escondê-la.
			Entity live = kInvalidEntity;
			for (EntityManager::Archetype& a : world.entities().archetypes()) {
				for (EntityManager::Chunk& c : a.chunks) {
					for (uint32_t i = 0; i < c.count; ++i) live = c.entities[i];
				}
			}
			check(live != kInvalidEntity, "existe exatamente uma entidade viva depois do redo");

			// Renomear, com merge de gesto (digitar e uma acao, nao uma por tecla).
			editor::renameEntity(world, &undo, live, "Renomeada");
			check(world.get<NameComponent>(live)->name == std::string_view("Renomeada"),
			      "renameEntity aplica o nome");
			const size_t depthAfterFirst = undo.undoDepth();
			editor::renameEntity(world, &undo, live, "Renomeada de novo");
			check(undo.undoDepth() == depthAfterFirst,
			      "digitar o nome nao cria um passo de undo por tecla");
			undo.undo();
			check(world.get<NameComponent>(live)->name == std::string_view("Primeira"),
			      "undo do rename volta ao nome original");
			undo.redo();

			// Duplicar copia os componentes, nao o id.
			world.get<TransformComponent>(live)->position = glm::vec3(5.0f, 6.0f, 7.0f);
			context.selectedEntity = live;
			const Entity copy = editor::duplicateSelected(context);
			check(copy != kInvalidEntity && copy != live, "duplicar produz uma entidade nova");
			check(world.liveCount() == 2, "duplicar deixa duas entidades");
			check(world.get<TransformComponent>(copy)->position == glm::vec3(5.0f, 6.0f, 7.0f),
			      "a copia carrega os valores dos componentes");
			check(world.get<NameComponent>(copy)->name ==
			          world.get<NameComponent>(live)->name,
			      "a copia carrega o nome");
			check(context.selectedEntity == copy, "duplicar seleciona a copia");
			undo.undo();
			check(world.liveCount() == 1, "undo de duplicar remove a copia");
			undo.redo();

			// Deletar solta a selecao, porque o undo devolve a entidade com OUTRO id.
			context.selectedEntity = live;
			check(editor::deleteSelected(context), "deleteSelected apaga");
			check(!world.alive(live), "a entidade foi destruida");
			check(!context.hasSelectedEntity(), "deletar solta a selecao");
			undo.undo();
			check(world.liveCount() == 2, "undo de deletar devolve a entidade");

			// Sem pilha de undo a operacao ainda acontece — so nao da para voltar.
			{
				World bare;
				const Entity e = editor::createEntity(bare, nullptr, "Sem undo");
				check(bare.alive(e), "createEntity funciona sem pilha de undo");
			}
		}

		// ── Prompt de nao-salvo em New/Open (C-05) ───────────────────────────
		{
			using namespace tucano::ecs;

			editor::SceneTool tool;
			editor::EditorContext context;
			World world;
			context.world = &world;
			tool.setContext(&context);
			tool.initialize();

			editor::createEntity(context, "Nao salvar isto");
			tool.markDirty();

			// Sem DialogManager nao ha como perguntar, entao a resposta segura e nao descartar.
			tool.requestNewScene();
			check(world.liveCount() == 1,
			      "sem forma de perguntar, New nao descarta trabalho nao salvo");
			check(!tool.error().empty(), "a recusa diz por que");

			// Com o dialogo ligado, a decisao fica pendente ate alguem responder.
			editor::DialogManager dialogs;
			tool.setDialogs(&dialogs);
			tool.requestNewScene();
			check(dialogs.queued() > 0 || dialogs.hasActiveDialog(),
			      "com dialogo ligado, New pergunta antes de descartar");
			check(world.liveCount() == 1, "e nao mexe em nada enquanto a pergunta esta aberta");

			// Documento limpo passa direto, sem perguntar nada.
			dialogs.clear();
			tool.save();
			tool.requestNewScene();
			check(world.liveCount() == 0, "documento limpo dispensa a pergunta");
		}

		// ── Play mode (I-01) ─────────────────────────────────────────────────
		// A garantia inteira e uma so: Stop devolve a cena exatamente ao que era antes do Play. E o
		// que torna seguro apertar Play no meio da autoria.
		{
			using namespace tucano::ecs;

			World world;
			editor::PlayMode play;

			const Entity e = world.create();
			world.add<NameComponent>(e)->name = "Ator";
			auto* transform = world.add<TransformComponent>(e);
			const glm::vec3 authored(1.0f, 2.0f, 3.0f);
			transform->position = authored;

			WaterParams water;
			water.waterLevel = 4.5f;
			SceneEnvironment environment;
			environment.water = &water;
			play.bind(&world, environment);

			check(play.state() == editor::PlayMode::State::Editing, "comeca em modo de edicao");
			check(!play.isPlaying(), "e nao esta jogando");

			// Tick fora do play nao pode mexer em nada.
			int ticks = 0;
			play.onTick = [&](float) { ++ticks; };
			play.tick(1.0f / 60.0f);
			check(ticks == 0, "tick fora do play e no-op");

			check(play.play(), "play comeca (" + play.error() + ")");
			check(play.isPlaying(), "estado passa a jogando");
			check(!play.snapshot().empty(), "play tira um snapshot antes de comecar");

			// A simulacao mexe no mundo — e no ambiente, que tambem tem que voltar.
			play.tick(0.5f);
			play.tick(0.5f);
			check(ticks == 2, "tick roda a simulacao do host");
			check(play.playTime() == 1.0f, "playTime acumula o tempo jogado");

			world.get<TransformComponent>(e)->position = glm::vec3(99.0f);
			world.get<NameComponent>(e)->name = "Renomeado em jogo";
			const Entity spawned = world.create();
			world.add<NameComponent>(spawned)->name = "Spawnado";
			water.waterLevel = 100.0f;
			check(world.liveCount() == 2, "a simulacao pode criar entidades");

			// Pause congela a simulacao sem perder o estado.
			play.pause();
			check(play.isPaused() && play.isPlaying(), "pausado ainda conta como jogando");
			const int ticksAtPause = ticks;
			const float timeAtPause = play.playTime();
			play.tick(1.0f);
			check(ticks == ticksAtPause, "pausado nao roda a simulacao");
			check(play.playTime() == timeAtPause, "pausado nao conta tempo de jogo");
			play.resume();
			check(!play.isPaused(), "resume volta a jogar");

			// O ponto de tudo.
			play.stop();
			check(play.state() == editor::PlayMode::State::Editing, "stop volta para edicao");
			check(world.liveCount() == 1, "stop descarta o que a simulacao criou");
			check(play.snapshot().empty(), "stop descarta o snapshot");
			check(play.playTime() == 0.0f, "stop zera o tempo de jogo");

			Entity restored = kInvalidEntity;
			for (EntityManager::Archetype& a : world.entities().archetypes()) {
				for (EntityManager::Chunk& c : a.chunks) {
					for (uint32_t i = 0; i < c.count; ++i) restored = c.entities[i];
				}
			}
			check(restored != kInvalidEntity, "a entidade autorada continua la");
			check(world.get<TransformComponent>(restored)->position == authored,
			      "stop devolve a posicao autorada, nao a simulada");
			check(world.get<NameComponent>(restored)->name == std::string_view("Ator"),
			      "stop devolve o nome autorado");
			check(water.waterLevel == 4.5f, "stop devolve tambem o bloco de ambiente");

			// Callbacks de entrada e saida, para o host montar e desmontar o que o snapshot nao
			// carrega (corpos de fisica, estado de script).
			int entered = 0;
			int exited = 0;
			play.onEnterPlay = [&] { ++entered; };
			play.onExitPlay = [&] { ++exited; };
			play.play();
			check(entered == 1 && exited == 0, "onEnterPlay roda ao comecar");
			play.stop();
			check(exited == 1, "onExitPlay roda ao parar");

			// Chamadas redundantes nao podem estragar nada.
			play.stop();
			check(exited == 1, "stop duas vezes nao roda o teardown de novo");
			play.play();
			const std::string firstSnapshot = play.snapshot();
			play.play();
			check(play.snapshot() == firstSnapshot, "play duas vezes nao troca o snapshot");
			play.stop();

			// Sem mundo ligado, play recusa em vez de comecar sem rede.
			{
				editor::PlayMode orphan;
				check(!orphan.play(), "play sem mundo recusa");
				check(!orphan.error().empty(), "a recusa diz por que");
				orphan.stop(); // seguro mesmo sem nunca ter jogado
			}
		}

		// Salvar durante o play tem que recusar: o mundo contem o jogo rodando, nao a cena autorada.
		{
			using namespace tucano::ecs;

			editor::SceneTool tool;
			editor::EditorContext context;
			World world;
			context.world = &world;
			tool.setContext(&context);
			tool.initialize();

			tool.playMode().bind(&world, {});
			check(tool.playMode().play(), "o SceneTool entra em play");
			check(!tool.saveSceneTo("uitest_should_not_exist.tuscene"),
			      "salvar durante o play recusa");
			check(tool.error().find("play") != std::string::npos,
			      "a recusa aponta o play mode como motivo");
			tool.playMode().stop();
		}

		// ── AssetRegistry ligado (B-01) ──────────────────────────────────────
		// O registry existia com zero consumidores. O que se afirma aqui e a propriedade que
		// justifica ter GUID: renomear o arquivo NAO quebra a referencia.
		{
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			const fs::path root = fs::temp_directory_path() / "tucano_uitest_assets";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root / "Props", ec);

			const auto touch = [](const fs::path& p) {
				std::ofstream f(p, std::ios::binary);
				f << "x";
			};
			touch(root / "Props" / "Torre.gltf");
			touch(root / "Props" / "Torre.png");
			touch(root / "leiame.txt");   // nao e asset: o scan tem que ignorar

			AssetRegistry registry;
			AssetRegistry::ScanOptions readOnly;
			readOnly.createMissingMeta = false;
			check(registry.scanProject(root.string(), readOnly) == 0,
			      "scan somente-leitura nao indexa asset sem sidecar");
			check(registry.missingMeta().size() == 2,
			      "e reporta o que pulou em vez de indexar nada em silencio (" +
			          std::to_string(registry.missingMeta().size()) + ")");
			check(!fs::exists(AssetRegistry::metaPathFor((root / "Props" / "Torre.gltf").string())),
			      "scan somente-leitura nao escreve na pasta de assets");

			AssetRegistry::ScanOptions writing;
			writing.createMissingMeta = true;
			check(registry.scanProject(root.string(), writing) == 2,
			      "com sidecars, o scan indexa os dois assets e ignora o .txt");
			check(registry.missingMeta().empty(), "nada ficou de fora no segundo scan");

			const RegistryEntry* mesh = registry.findByPath("Props/Torre.gltf");
			const RegistryEntry* texture = registry.findByPath("Props/Torre.png");
			check(mesh != nullptr && mesh->type == AssetType::Mesh, "o .gltf entra como mesh");
			check(texture != nullptr && texture->type == AssetType::Texture,
			      "o .png entra como textura");
			check(mesh != nullptr && texture != nullptr && mesh->guid != texture->guid,
			      "dois assets com o mesmo nome base recebem GUIDs diferentes");
			check(registry.byType(AssetType::Mesh).size() == 1,
			      "byType filtra pelo tipo pedido");

			const AssetGuid meshGuid = mesh != nullptr ? mesh->guid : AssetGuid{};
			check(meshGuid.valid(), "o GUID gerado e valido");
			check(registry.find(meshGuid) != nullptr, "lookup por GUID acha a entrada");

			// O ponto de tudo: renomear o arquivo, levando o sidecar junto, preserva o GUID.
			// `AssetGuid::fromPath` e hash do caminho e mudaria aqui — e por isso que o sidecar
			// existe.
			const AssetGuid pathHashBefore =
			    AssetGuid::fromPath((root / "Props" / "Torre.gltf").string());
			fs::rename(root / "Props" / "Torre.gltf", root / "Props" / "TorreAntiga.gltf", ec);
			fs::rename(AssetRegistry::metaPathFor((root / "Props" / "Torre.gltf").string()),
			           AssetRegistry::metaPathFor((root / "Props" / "TorreAntiga.gltf").string()), ec);
			check(!ec, "renomear asset e sidecar juntos");

			AssetRegistry afterRename;
			afterRename.scanProject(root.string(), writing);
			const RegistryEntry* renamed = afterRename.findByPath("Props/TorreAntiga.gltf");
			check(renamed != nullptr, "o asset renomeado e reindexado");
			check(renamed != nullptr && renamed->guid == meshGuid,
			      "renomear NAO muda o GUID — a referencia sobrevive");
			check(AssetGuid::fromPath((root / "Props" / "TorreAntiga.gltf").string()) != pathHashBefore,
			      "o hash de caminho teria mudado, que e o motivo do sidecar existir");

			// Sidecar corrompido: o asset ganha identidade nova em vez de o scan abortar.
			{
				const std::string metaPath =
				    AssetRegistry::metaPathFor((root / "Props" / "Torre.png").string());
				std::ofstream(metaPath, std::ios::binary | std::ios::trunc) << "isto nao e json";
				AssetRegistry recovered;
				check(recovered.scanProject(root.string(), writing) == 2,
				      "sidecar corrompido nao derruba o scan");
				const RegistryEntry* fixed = recovered.findByPath("Props/Torre.png");
				check(fixed != nullptr && fixed->guid.valid(),
				      "o asset com sidecar corrompido recebe identidade nova");
			}

			// Raiz inexistente devolve zero em vez de lancar.
			AssetRegistry missing;
			check(missing.scanProject((root / "nao_existe").string(), writing) == 0,
			      "raiz inexistente devolve zero, sem excecao");

			fs::remove_all(root, ec);
		}

		// ── Pickers sobre o registry (B-06) ──────────────────────────────────
		// Antes varriam o disco por extensao; um caminho escolhido assim quebra quando o arquivo
		// se move. Com o indice, a escolha carrega GUID e sobrevive ao rename.
		{
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			const fs::path root = fs::temp_directory_path() / "tucano_uitest_pickers";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root / "Meshes", ec);
			fs::create_directories(root / "Sky", ec);
			const auto touch = [](const fs::path& p) {
				std::ofstream f(p, std::ios::binary);
				f << "x";
			};
			touch(root / "Meshes" / "Torre.gltf");
			touch(root / "Meshes" / "Casa.gltf");
			touch(root / "Sky" / "Ceu.hdr");
			touch(root / "Sky" / "Album.png");

			AssetRegistry registry;
			AssetRegistry::ScanOptions options;
			options.createMissingMeta = true;
			registry.scanProject(root.string(), options);

			editor::ui::AssetPicker picker;
			picker.setKind(editor::ui::AssetPicker::Kind::Mesh);
			picker.setRoot(root.string());
			check(!picker.usingRegistry(), "sem registry, o picker fica no modo de varredura");
			picker.scan();
			const size_t scanned = picker.candidates().size();

			picker.setRegistry(&registry);
			check(picker.usingRegistry(), "com registry ligado, o picker muda de fonte");
			picker.scan();
			check(picker.candidates().size() == 2,
			      "o picker de mesh lista os dois .gltf do indice (" +
			          std::to_string(picker.candidates().size()) + ")");
			check(scanned == picker.candidates().size(),
			      "indice e varredura concordam no que existe");

			// Hdri continua separado de Texture: o registry so conhece "textura", e e o picker que
			// impede apontar o IBL para um PNG de 8 bits.
			editor::ui::AssetPicker hdri;
			hdri.setKind(editor::ui::AssetPicker::Kind::Hdri);
			hdri.setRegistry(&registry);
			hdri.scan();
			check(hdri.candidates().size() == 1, "o picker de HDRI nao aceita o png");
			check(editor::ui::AssetPicker::assetTypeFor(editor::ui::AssetPicker::Kind::Hdri) ==
			          AssetType::Texture,
			      "para o registry, HDRI e textura — a distincao e de intencao");

			// O que justifica a B-06: escolher resolve um GUID, e o GUID sobrevive ao rename.
			picker.setPath("Meshes/Torre.gltf");
			const AssetGuid picked = picker.guid();
			check(picked.valid(), "escolher pelo indice resolve um GUID");

			fs::rename(root / "Meshes" / "Torre.gltf", root / "Meshes" / "TorreNova.gltf", ec);
			fs::rename(AssetRegistry::metaPathFor((root / "Meshes" / "Torre.gltf").string()),
			           AssetRegistry::metaPathFor((root / "Meshes" / "TorreNova.gltf").string()), ec);
			registry.scanProject(root.string(), options);

			editor::ui::AssetPicker reopened;
			reopened.setKind(editor::ui::AssetPicker::Kind::Mesh);
			reopened.setRegistry(&registry);
			reopened.setGuid(picked);
			check(reopened.path() == "Meshes/TorreNova.gltf",
			      "o GUID guardado reencontra o asset depois do rename");
			check(reopened.guid() == picked, "e continua sendo o mesmo GUID");

			// Um caminho que o indice nao conhece e reportado como ausente, sem tocar no disco.
			picker.setPath("Meshes/NaoExiste.gltf");
			check(!picker.guid().valid(), "caminho fora do indice nao inventa GUID");

			fs::remove_all(root, ec);
		}

		// ── Content Browser sobre o registry (B-02) ───────────────────────────
		// E painel, entao o que se afirma e o contrato: sem indice ele diz isso em vez de varrer o
		// disco, e com indice nao quebra.
		{
			editor::ContentBrowser browser;
			editor::EditorContext empty;
			check(!empty.assets, "sem projeto escaneado, o contexto nao tem indice");

			asset::AssetRegistry registry;
			editor::EditorContext withAssets;
			withAssets.assets = &registry;

			for (int i = 0; i < 2; ++i) {
				window.pollEvents();
				ui.beginFrame();
				ImGui::Begin("##contentProbe");
				browser.draw(empty);       // caminho "sem projeto"
				browser.draw(withAssets);  // indice vazio
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}
			check(!browser.selected().valid(),
			      "sem clique, o Content Browser nao reporta selecao");
		}

		// ── Referencia de asset por GUID (fecha o ciclo B-01/B-06) ───────────
		// O `MeshComponent` guardava caminho. Isso e o teste que so passa por guardar identidade:
		// renomear o asset e a cena continuar apontando para ele.
		{
			using namespace tucano::ecs;
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			const fs::path root = fs::temp_directory_path() / "tucano_uitest_assetref";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root / "Props", ec);
			{
				std::ofstream f(root / "Props" / "Torre.gltf", std::ios::binary);
				f << "x";
			}

			AssetRegistry registry;
			AssetRegistry::ScanOptions options;
			options.createMissingMeta = true;
			registry.scanProject(root.string(), options);
			const RegistryEntry* source = registry.findByPath("Props/Torre.gltf");
			check(source != nullptr, "o asset de teste esta no indice");
			const AssetGuid meshGuid = source != nullptr ? source->guid : AssetGuid{};

			// O tipo do campo e o que importa: identidade, nao texto.
			const TypeInfo* meshType = TypeRegistry::instance().find(TypeID{"MeshComponent"});
			const PropertyInfo* meshProperty =
			    meshType != nullptr ? meshType->findProperty("mesh") : nullptr;
			check(meshProperty != nullptr && meshProperty->coreType == CoreType::AssetRef,
			      "MeshComponent::mesh e uma referencia de asset, nao um caminho");
			check(meshType != nullptr && meshType->findProperty("meshPath") == nullptr,
			      "o campo de caminho deixou de existir");

			World world;
			const Entity e = world.create();
			world.add<NameComponent>(e)->name = "Torre";
			world.add<TransformComponent>(e);
			world.add<MeshComponent>(e)->mesh = meshGuid;

			// Round-trip: o GUID atravessa o arquivo como texto hex.
			const std::string text = sceneToJson(world);
			check(text.find(meshGuid.toString()) != std::string::npos,
			      "a cena grava o GUID em hex, nao o caminho");

			World reopened;
			std::string sceneErr;
			check(sceneFromJson(text, reopened, {}, &sceneErr), "a cena com referencia carrega");
			Entity restored = kInvalidEntity;
			for (EntityManager::Archetype& a : reopened.entities().archetypes()) {
				for (EntityManager::Chunk& c : a.chunks) {
					for (uint32_t i = 0; i < c.count; ++i) restored = c.entities[i];
				}
			}
			const MeshComponent* restoredMesh =
			    restored != kInvalidEntity ? reopened.get<MeshComponent>(restored) : nullptr;
			check(restoredMesh != nullptr && restoredMesh->mesh == meshGuid,
			      "a referencia volta identica do arquivo");

			// O ponto: renomear o asset e a cena continuar resolvendo.
			fs::rename(root / "Props" / "Torre.gltf", root / "Props" / "TorreV2.gltf", ec);
			fs::rename(AssetRegistry::metaPathFor((root / "Props" / "Torre.gltf").string()),
			           AssetRegistry::metaPathFor((root / "Props" / "TorreV2.gltf").string()), ec);
			registry.scanProject(root.string(), options);

			const RegistryEntry* afterRename =
			    restoredMesh != nullptr ? registry.find(restoredMesh->mesh) : nullptr;
			check(afterRename != nullptr,
			      "depois do rename, a referencia da cena ainda resolve no indice");
			check(afterRename != nullptr && afterRename->relativePath == "Props/TorreV2.gltf",
			      "e resolve para o caminho novo");

			// Um GUID que o indice nao conhece: nao resolve, mas tambem nao inventa nada.
			AssetGuid stranger;
			stranger.hi = 0xDEADBEEFu;
			stranger.lo = 1;
			check(registry.find(stranger) == nullptr, "GUID desconhecido nao resolve");

			// Texto malformado limpa a referencia em vez de aponta-la para outro asset.
			{
				World bad;
				check(sceneFromJson(
				          R"({"format":"tuscene","version":1,"entities":[{"mesh_ref":{"mesh":"nao-e-um-guid"}}]})",
				          bad, {}, &sceneErr),
				      "id malformado nao derruba o carregamento");
				check(!sceneErr.empty(), "e e reportado");
			}

			check(AssetGuid::fromString(meshGuid.toString()) == meshGuid,
			      "toString e fromString fazem round-trip");
			check(!AssetGuid::fromString("curto").valid(), "texto curto nao vira GUID parcial");

			fs::remove_all(root, ec);
		}

		// ── Import pelo editor (B-03) ────────────────────────────────────────
		// A parte que so o runtime pode afirmar: a identidade do que sai do import deriva da origem,
		// entao reimportar depois de renomear produz os MESMOS ids — e as cenas continuam validas.
		{
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			// Identidade de sub-asset, que e o mecanismo por tras disso.
			AssetGuid parent;
			parent.hi = 0xA1B2C3D4E5F60718ULL;
			parent.lo = 0x0F1E2D3C4B5A6978ULL;

			const AssetGuid a = AssetGuid::forSubAsset(parent, "mesh/Torre");
			const AssetGuid b = AssetGuid::forSubAsset(parent, "mesh/Casa");
			check(a.valid() && b.valid(), "sub-assets recebem ids validos");
			check(a != b, "nomes diferentes dao ids diferentes");
			check(AssetGuid::forSubAsset(parent, "mesh/Torre") == a,
			      "o id de sub-asset e reproduzivel — reimportar da o mesmo valor");
			check(a != parent, "o sub-asset nao herda o id do pai");

			AssetGuid otherParent = parent;
			otherParent.lo ^= 1;
			check(AssetGuid::forSubAsset(otherParent, "mesh/Torre") != a,
			      "pais diferentes dao sub-assets diferentes");
			check(!AssetGuid::forSubAsset(AssetGuid{}, "mesh/Torre").valid(),
			      "pai invalido nao produz identidade");

			// O que a B-03 conserta: o id NAO depende do caminho. Este e o contraste com fromPath.
			check(AssetGuid::fromPath("Props/Torre.gltf") != AssetGuid::fromPath("Props/TorreV2.gltf"),
			      "fromPath muda com o rename (era o problema)");

			// O comando do editor: recusa o que nao sabe importar, e diz por que.
			const fs::path root = fs::temp_directory_path() / "tucano_uitest_import";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root, ec);
			{
				std::ofstream f(root / "leiame.txt", std::ios::binary);
				f << "x";
			}

			editor::SceneTool tool;
			editor::EditorContext context;
			tool.setContext(&context);
			tool.initialize();

			check(tool.importAsset((root / "leiame.txt").string()) == 0,
			      "importar sem projeto escaneado recusa");
			check(!tool.error().empty(), "e diz por que");

			tool.scanAssets(root.string(), true);
			check(tool.importAsset((root / "leiame.txt").string()) == 0,
			      "arquivo que nao e modelo e recusado");
			check(tool.error().find("importable") != std::string::npos,
			      "a recusa aponta o formato como motivo");

			check(tool.importAsset((root / "naoexiste.gltf").string()) == 0,
			      "origem inexistente nao finge sucesso");

			// Uma origem sem sidecar ganha um antes de qualquer coisa ser derivada dela — senao o
			// cozido cairia no hash de caminho e o rename voltaria a quebrar.
			{
				std::ofstream f(root / "Modelo.gltf", std::ios::binary);
				f << "{}";  // nao e um glTF valido: o import falha, mas o sidecar tem de existir
			}
			tool.importAsset((root / "Modelo.gltf").string());
			check(fs::exists(AssetRegistry::metaPathFor((root / "Modelo.gltf").string())),
			      "importar da identidade a origem antes de derivar qualquer coisa");

			fs::remove_all(root, ec);
		}

		// ── Material como asset (B-05) ───────────────────────────────────────
		// Passo 3 da Definition of Done: criar um material, ajustar PBR, atribuir ao mesh. Antes um
		// material so existia dentro de um modelo carregado — nao dava para compartilhar nem salvar.
		{
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			// O tipo de autoria e o de runtime sao diferentes de proposito.
			const TypeInfo* assetType = TypeRegistry::instance().find(TypeID{"MaterialAsset"});
			check(assetType != nullptr && assetType->propertyCount > 0,
			      "MaterialAsset esta refletido");
			const PropertyInfo* albedo =
			    assetType != nullptr ? assetType->findProperty("albedo") : nullptr;
			check(albedo != nullptr && albedo->coreType == CoreType::AssetRef,
			      "as texturas do material sao referencias, nao ponteiros nem caminhos");

			const fs::path root = fs::temp_directory_path() / "tucano_uitest_material";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root, ec);

			editor::SceneTool tool;
			editor::EditorContext context;
			tool.setContext(&context);
			tool.initialize();

			check(!tool.createMaterial("Materials/Tijolo.tumat").valid(),
			      "criar material sem projeto escaneado recusa");

			tool.scanAssets(root.string(), true);
			const AssetGuid guid = tool.createMaterial("Materials/Tijolo.tumat");
			check(guid.valid(), "criar material devolve uma identidade (" + tool.error() + ")");
			check(fs::exists(root / "Materials" / "Tijolo.tumat"),
			      "o arquivo e escrito, criando a pasta que faltava");
			check(fs::exists(AssetRegistry::metaPathFor((root / "Materials" / "Tijolo.tumat").string())),
			      "o material nasce com sidecar, como qualquer outro asset");
			check(tool.assets().find(guid) != nullptr,
			      "e ja aparece no indice, sem precisar de outro scan");

			// Editar e salvar.
			MaterialAsset material;
			check(tool.loadMaterial(guid, material), "carregar o material (" + tool.error() + ")");
			check(material.name == "Material", "um material novo nasce nos defaults do tipo");

			material.name = "Tijolo";
			material.roughnessFactor = 0.8125f;
			material.baseColorFactor = glm::vec4(0.6f, 0.3f, 0.2f, 1.0f);
			material.alphaMask = true;
			check(tool.saveMaterial(guid, material), "salvar o material editado");

			MaterialAsset reloaded;
			check(tool.loadMaterial(guid, reloaded), "recarregar por identidade");
			check(reloaded.name == "Tijolo" && reloaded.roughnessFactor == material.roughnessFactor &&
			          reloaded.baseColorFactor == material.baseColorFactor &&
			          reloaded.alphaMask,
			      "os valores voltam identicos do arquivo");

			// O ponto de guardar identidade: renomear o material e continuar resolvendo.
			fs::rename(root / "Materials" / "Tijolo.tumat", root / "Materials" / "TijoloV2.tumat", ec);
			fs::rename(AssetRegistry::metaPathFor((root / "Materials" / "Tijolo.tumat").string()),
			           AssetRegistry::metaPathFor((root / "Materials" / "TijoloV2.tumat").string()), ec);
			tool.scanAssets(root.string(), true);

			MaterialAsset afterRename;
			check(tool.loadMaterial(guid, afterRename),
			      "depois do rename, o mesmo GUID ainda carrega o material");
			check(afterRename.name == "Tijolo", "e com o conteudo certo");

			// Atribuir a um mesh: o slot existe no componente e sobrevive ao round-trip da cena.
			const TypeInfo* meshType = TypeRegistry::instance().find(TypeID{"MeshComponent"});
			const PropertyInfo* slot =
			    meshType != nullptr ? meshType->findProperty("material") : nullptr;
			check(slot != nullptr && slot->coreType == CoreType::AssetRef,
			      "MeshComponent tem um slot de material por identidade");
			check(slot != nullptr && std::string_view(slot->meta.assetKind) == "material",
			      "e o slot so aceita material");

			{
				ecs::World world;
				const ecs::Entity e = world.create();
				world.add<ecs::NameComponent>(e)->name = "Parede";
				auto* meshRef = world.add<ecs::MeshComponent>(e);
				meshRef->material = guid;

				ecs::World reopened;
				std::string sceneErr;
				check(ecs::sceneFromJson(ecs::sceneToJson(world), reopened, {}, &sceneErr),
				      "a cena com material atribuido carrega");
				ecs::Entity restored = ecs::kInvalidEntity;
				for (ecs::EntityManager::Archetype& a : reopened.entities().archetypes()) {
					for (ecs::EntityManager::Chunk& c : a.chunks) {
						for (uint32_t i = 0; i < c.count; ++i) restored = c.entities[i];
					}
				}
				const ecs::MeshComponent* back =
				    restored != ecs::kInvalidEntity ? reopened.get<ecs::MeshComponent>(restored)
				                                    : nullptr;
				check(back != nullptr && back->material == guid,
				      "a atribuicao sobrevive ao salvar e reabrir a cena");
			}

			// GUID desconhecido nao carrega nem finge.
			AssetGuid stranger;
			stranger.hi = 7;
			stranger.lo = 7;
			MaterialAsset nothing;
			check(!tool.loadMaterial(stranger, nothing), "GUID desconhecido nao carrega material");
			check(!tool.error().empty(), "e diz por que");

			fs::remove_all(root, ec);
		}

		// ── Resolver e import de textura ─────────────────────────────────────
		// A metade que faltava: guardar *o que* uma entidade referencia era autoria; isto e o que
		// transforma a referencia em algo que o renderer pode desenhar.
		{
			using namespace tucano::asset;
			namespace fs = std::filesystem;

			const fs::path root = fs::temp_directory_path() / "tucano_uitest_resolver";
			const fs::path outside = fs::temp_directory_path() / "tucano_uitest_outside";
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::remove_all(outside, ec);
			fs::create_directories(root, ec);
			fs::create_directories(outside, ec);

			// Uma imagem de verdade do proprio repositorio, copiada para o projeto de teste. Bytes
			// escritos a mao passariam pelo indice e falhariam no carregamento, testando a coisa
			// errada — o que se quer aqui e o caminho de sucesso do resolver.
			const fs::path realTexture = fs::path(TUCANO_ENGINE_ASSETS_DIR) / "Textures" / "Rain" /
			                             "rain_streak.dds";
			const bool haveRealTexture = fs::exists(realTexture, ec);
			check(haveRealTexture, "o gate encontrou uma textura real para usar");
			const auto writePng = [&](const fs::path& p) {
				fs::copy_file(realTexture, p, fs::copy_options::overwrite_existing, ec);
			};
			writePng(outside / "Tijolo.dds");

			editor::SceneTool tool;
			editor::EditorContext toolContext;
			tool.setContext(&toolContext);
			tool.initialize();

			check(!tool.importTexture((outside / "Tijolo.dds").string()).valid(),
			      "importar textura sem projeto escaneado recusa");

			tool.scanAssets(root.string(), true);
			const AssetGuid textureGuid = tool.importTexture((outside / "Tijolo.dds").string());
			check(textureGuid.valid(), "importar textura de fora devolve identidade (" + tool.error() + ")");
			check(fs::exists(root / "Textures" / "Tijolo.dds"),
			      "a textura de fora e copiada para dentro do projeto");
			check(tool.assets().find(textureGuid) != nullptr, "e entra no indice");

			// Importar de novo o mesmo nome nao pode sobrescrever em silencio.
			check(!tool.importTexture((outside / "Tijolo.dds").string()).valid(),
			      "importar duas vezes o mesmo nome recusa em vez de sobrescrever");
			check(tool.error().find("already") != std::string::npos, "e diz que ja existe");

			// Uma textura que ja esta no projeto so ganha identidade, sem ser duplicada.
			writePng(root / "Chao.dds");
			const size_t before = tool.assets().size();
			const AssetGuid inside = tool.importTexture((root / "Chao.dds").string());
			check(inside.valid(), "textura ja dentro do projeto tambem importa");
			check(!fs::exists(root / "Textures" / "Chao.dds"),
			      "e nao e copiada para outro lugar");
			check(tool.assets().size() == before + 1, "so um asset novo entrou no indice");

			check(!tool.importTexture((root / "naoexiste.png").string()).valid(),
			      "arquivo inexistente recusa");
			writePng(root / "Modelo.gltf");  // extensao errada de proposito
			check(!tool.importTexture((root / "Modelo.gltf").string()).valid(),
			      "arquivo que nao e textura recusa");

			// ── Resolver ──
			AssetResolver resolver(*device, tool.assets(), root.string());

			check(resolver.texture(AssetGuid{}, true) == nullptr, "GUID invalido nao resolve textura");
			AssetGuid stranger;
			stranger.hi = 99;
			stranger.lo = 99;
			check(resolver.texture(stranger, true) == nullptr, "GUID desconhecido nao resolve textura");
			check(resolver.cachedTextureCount() == 0, "falhas nao entram no cache");

			auto first = resolver.texture(textureGuid, /*srgb=*/true);
			check(first != nullptr, "o resolver carrega a textura pelo GUID");
			auto again = resolver.texture(textureGuid, /*srgb=*/true);
			check(first == again, "a segunda pedida vem do cache, nao do disco");
			check(resolver.cachedTextureCount() == 1, "uma entrada de cache para uma textura");

			// sRGB e linear sao objetos de GPU diferentes: carregar albedo como linear e um bug que
			// parece arte ruim.
			auto linear = resolver.texture(textureGuid, /*srgb=*/false);
			check(linear != nullptr && linear != first,
			      "a mesma imagem em sRGB e em linear sao texturas distintas");
			check(resolver.cachedTextureCount() == 2, "e ocupam entradas de cache separadas");

			// ── Material de autoria para material de runtime ──
			MaterialAsset asset;
			asset.name = "Tijolo";
			asset.roughnessFactor = 0.375f;
			asset.baseColorFactor = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
			asset.albedo = textureGuid;
			asset.normal = inside;

			auto runtime = resolver.materialFromAsset(asset);
			check(runtime != nullptr, "materialFromAsset produz um material de runtime");
			check(runtime->name == "Tijolo" && runtime->roughnessFactor == asset.roughnessFactor &&
			          runtime->baseColorFactor == asset.baseColorFactor,
			      "os fatores atravessam sem mudar");
			check(runtime->albedo != nullptr, "o albedo foi resolvido de GUID para textura");
			check(runtime->normal != nullptr, "o normal tambem");
			check(runtime->emissive == nullptr,
			      "um slot sem referencia fica nulo, sem placeholder inventado");

			// E pelo arquivo, que e o caminho real do editor.
			const AssetGuid materialGuid = tool.createMaterial("Materials/Tijolo.tumat", asset);
			check(materialGuid.valid(), "material salvo (" + tool.error() + ")");
			AssetResolver afterSave(*device, tool.assets(), root.string());
			auto fromFile = afterSave.material(materialGuid);
			check(fromFile != nullptr, "o resolver constroi o material a partir do .tumat");
			check(fromFile != nullptr && fromFile->albedo != nullptr,
			      "e resolve as texturas que o arquivo referencia");

			// ── CP-35: o override chegar ao que e desenhado ─────────────────────
			// Ate aqui o material carregava certo e nao mudava pixel nenhum. O que se afirma
			// abaixo e a ponte: o que a entidade referencia vira o que o RenderObject usa.
			{
				using ecs::MaterialSyncState;
				using ecs::syncMeshComponentsToScene;

				Scene scene;
				scene.objects.resize(2);
				// Dois slots de proposito: o renderer escolhe materials[sub.materialIndex], entao
				// um mesh com dois submeshes tem dois materiais e o override precisa valer para os
				// dois. Com um slot so este teste passaria sem provar nada.
				auto imported = std::make_shared<Material>();
				imported->name = "ImportadoDoGltf";
				auto importedSecond = std::make_shared<Material>();
				importedSecond->name = "SegundoSubmesh";
				scene.objects[0].materials = {imported, importedSecond};
				scene.objects[1].name = "SemMaterial";

				ecs::World world;
				const ecs::Entity parede = world.create();
				world.add<ecs::NameComponent>(parede)->name = "Parede";
				world.add<ecs::RenderObjectComponent>(parede)->handle = scene.handleAt(0);
				auto* ref = world.add<ecs::MeshComponent>(parede);
				MaterialSyncState sync;

				// Sem override: o material importado nao pode ser tocado. Uma cena que so foi
				// aberta nao deve mudar de aparencia por ter passado pelo sync.
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[0].materials.size() == 2 &&
				          scene.objects[0].materials[0] == imported &&
				          scene.objects[0].materials[1] == importedSecond,
				      "sem override os materiais importados ficam como estavam");
				check(sync.originals.empty(), "e nada e guardado para desfazer");

				// Atribuir. Este e o check que faltava no CP-34.
				world.get<ecs::MeshComponent>(parede)->material = materialGuid;
				syncMeshComponentsToScene(world, scene, resolver, sync);
				auto applied = scene.objects[0].materials[0];
				check(applied != nullptr && applied != imported,
				      "atribuir um .tumat troca o material do RenderObject");
				check(applied != nullptr && applied->name == "Tijolo",
				      "e o material que chega e o do arquivo atribuido");
				check(applied != nullptr && applied->albedo != nullptr,
				      "com as texturas dele ja resolvidas");
				check(scene.objects[0].materials.size() == 2 &&
				          scene.objects[0].materials[1] == applied,
				      "e vale para todos os submeshes, nao so para o primeiro slot");

				// material() constroi um objeto novo a cada chamada, entao a identidade do ponteiro
				// prova que o segundo frame nao reconstruiu nada.
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[0].materials[0] == applied,
				      "o frame seguinte nao reconstroi o material (nem recarrega as texturas)");

				// Visibilidade: era um checkbox que nao fazia nada.
				check(scene.objects[0].visible, "o objeto comeca visivel");
				world.get<ecs::MeshComponent>(parede)->visible = false;
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(!scene.objects[0].visible, "desmarcar Visible esconde o objeto de verdade");
				world.get<ecs::MeshComponent>(parede)->visible = true;

				// Limpar o override tem que devolver o material do import, nao deixar o ultimo.
				world.get<ecs::MeshComponent>(parede)->material = AssetGuid{};
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[0].materials.size() == 2 &&
				          scene.objects[0].materials[0] == imported &&
				          scene.objects[0].materials[1] == importedSecond,
				      "limpar o override devolve todos os materiais que vieram do import");
				check(sync.originals.empty(), "e o que estava guardado e liberado");

				// Um GUID que nao resolve nao pode apagar o que ja esta desenhando.
				world.get<ecs::MeshComponent>(parede)->material = stranger;
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[0].materials[0] == imported,
				      "GUID que nao resolve nao apaga o material atual");
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[0].materials[0] == imported,
				      "e continua tentando em vez de desistir em silencio");

				// Objeto sem material nenhum: o override tem que criar o slot.
				const ecs::Entity chao = world.create();
				world.add<ecs::RenderObjectComponent>(chao)->handle = scene.handleAt(1);
				world.add<ecs::MeshComponent>(chao)->material = materialGuid;
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(scene.objects[1].materials.size() == 1,
				      "um objeto sem material ganha o slot ao receber um override");

				// Um handle que nao resolve acontece de verdade (deletar objeto, recarregar cena) e
				// nao pode derrubar o editor.
				const ecs::Entity orfa = world.create();
				world.add<ecs::RenderObjectComponent>(orfa)->handle = 999u;
				world.add<ecs::MeshComponent>(orfa)->material = materialGuid;
				syncMeshComponentsToScene(world, scene, resolver, sync);
				check(true, "handle que nao resolve e ignorado sem crash");
			}

			resolver.clearCache();
			check(resolver.cachedTextureCount() == 0, "clearCache esvazia");

			fs::remove_all(root, ec);
			fs::remove_all(outside, ec);
		}

		// ── D-01 / E-01: RendererSettings refletido e o ceu fora dele ────────
		// O que se afirma aqui e o que o painel escrito a mao nao conseguia garantir: que todo
		// campo e alcancavel, e que os campos de ceu **sairam** de RendererSettings em vez de
		// existirem nos dois lugares.
		{
			const TypeInfo* sky = TypeRegistry::instance().find(TypeID{"SkyParams"});
			const TypeInfo* render = TypeRegistry::instance().find(TypeID{"RendererSettings"});
			check(sky != nullptr, "SkyParams esta registrado");
			check(render != nullptr, "RendererSettings esta registrado");

			const auto findProperty = [](const TypeInfo* type, const char* name) -> const PropertyInfo* {
				if (type == nullptr) return nullptr;
				for (size_t i = 0; i < type->propertyCount; ++i) {
					if (std::string_view(type->properties[i].name) == name) return &type->properties[i];
				}
				return nullptr;
			};

			// Os 20 campos que mudaram de casa. Conferidos um a um porque "esta registrado" nao diz
			// nada sobre qual campo, e um esquecido volta a ser inalcancavel em silencio.
			const char* kMoved[] = {"enableAtmosphere", "useBrunetonAtmosphere", "atmosphereDrivesSun",
			                        "timeOfDay",        "turbidity",             "fogDensity",
			                        "fogHeight",        "wind",                  "latitudeDeg",
			                        "dayOfYear",        "enableMoon",            "moonIntensity",
			                        "moonDiscBrightness", "moonAngularRadiusDeg", "enableStars",
			                        "starIntensity",    "starTwinkle",           "starSizeDeg",
			                        "purkinjeStrength", "starCatalogPath"};
			size_t inSky = 0;
			size_t stillInRenderer = 0;
			for (const char* name : kMoved) {
				if (findProperty(sky, name) != nullptr) ++inSky;
				if (findProperty(render, name) != nullptr) ++stillInRenderer;
			}
			check(inSky == std::size(kMoved), "os 20 campos de ceu estao em SkyParams");
			check(stillInRenderer == 0, "e nenhum deles sobrou em RendererSettings (mudou de casa, nao foi copiado)");

			// ── E-05: uma nuvem, um dono ──────────────────────────────────────
			// `RendererSettings` carregava um gemeo de cada campo de `CloudParams`, e o
			// `Renderer::render` copiava um sobre o outro **todo frame**. Efeito: o painel Clouds do
			// editor nao mudava nada, e o bloco CloudParams que o `.tuscene` grava era sobrescrito no
			// frame seguinte ao load. Este check e sobre a causa: se os campos voltarem a existir nos
			// dois lugares, alguem vai copiar de novo.
			{
				const TypeInfo* cloud = TypeRegistry::instance().find(TypeID{"CloudParams"});
				check(cloud != nullptr, "CloudParams esta registrado");
				check(cloud != nullptr && findProperty(cloud, "coverage") != nullptr &&
				          findProperty(cloud, "storminess") != nullptr &&
				          findProperty(cloud, "driveRain") != nullptr,
				      "e e ele que descreve a camada de nuvem");

				// Nenhum campo de nuvem sobrou do outro lado. Por nome, porque foi assim que o gemeo
				// nasceu: alguem precisou de "coverage" perto das outras chaves de render e criou
				// `cloudCoverage` em vez de alcancar o CloudParams.
				size_t cloudFieldsInRenderer = 0;
				for (size_t i = 0; render != nullptr && i < render->propertyCount; ++i) {
					const std::string_view name(render->properties[i].name);
					if (name.find("cloud") != std::string_view::npos ||
					    name.find("Cloud") != std::string_view::npos) {
						++cloudFieldsInRenderer;
					}
				}
				check(cloudFieldsInRenderer == 0,
				      "e RendererSettings nao tem mais nenhum campo de nuvem para discordar dele");
			}

			// Tipos, nao so nomes: `wind` como Float em vez de Vec3 daria tres linhas erradas.
			const PropertyInfo* wind = findProperty(sky, "wind");
			check(wind != nullptr && wind->coreType == CoreType::Vec3, "wind e um Vec3");
			const PropertyInfo* catalog = findProperty(sky, "starCatalogPath");
			check(catalog != nullptr && catalog->coreType == CoreType::String,
			      "o caminho do catalogo de estrelas e uma String editavel");
			const PropertyInfo* timeOfDay = findProperty(sky, "timeOfDay");
			check(timeOfDay != nullptr && timeOfDay->meta.maxValue > timeOfDay->meta.minValue,
			      "timeOfDay tem faixa declarada, entao o editor nao inventa uma");

			// Toda propriedade tem rotulo e categoria: sem categoria a linha cai num grupo sem nome
			// no topo do painel, que e onde as coisas somem.
			size_t uncategorised = 0;
			size_t unlabelled = 0;
			for (const TypeInfo* type : {sky, render}) {
				if (type == nullptr) continue;
				for (size_t i = 0; i < type->propertyCount; ++i) {
					const PropertyMetadata& meta = type->properties[i].meta;
					if (meta.category == nullptr || *meta.category == '\0') ++uncategorised;
					if (meta.label == nullptr || *meta.label == '\0') ++unlabelled;
				}
			}
			check(uncategorised == 0, "nenhuma propriedade de Sky/Rendering cai num grupo sem nome");
			check(unlabelled == 0, "e todas tem rotulo");

			// A separacao autoria/engenharia que a D-01 pede.
			const auto isAdvanced = [&](const TypeInfo* type, const char* name) {
				const PropertyInfo* property = findProperty(type, name);
				return property != nullptr && property->meta.advanced;
			};
			check(isAdvanced(render, "enableMeshlets") && isAdvanced(render, "enableVisibilityBuffer") &&
			          isAdvanced(render, "enableShaderHotReload"),
			      "as chaves de engenharia estao marcadas como avancadas");
			check(!isAdvanced(render, "enableShadows") && !isAdvanced(render, "bloomStrength") &&
			          !isAdvanced(render, "exposureTarget"),
			      "e as de autoria nao estao");
			size_t advancedInSky = 0;
			for (size_t i = 0; sky != nullptr && i < sky->propertyCount; ++i) {
				if (sky->properties[i].meta.advanced) ++advancedInSky;
			}
			check(advancedInSky == 0, "o ceu inteiro e autoria — nada nele e chave de engenharia");

			// E a regra de visibilidade do grid, que e o que transforma a marca em comportamento.
			editor::PropertyGrid grid;
			const PropertyInfo* meshlets = findProperty(render, "enableMeshlets");
			const PropertyInfo* shadows = findProperty(render, "enableShadows");
			check(!grid.showAdvanced(), "o grid comeca escondendo o avancado");
			check(meshlets != nullptr && !grid.visible(*meshlets),
			      "com o avancado desligado, uma chave de engenharia nao e desenhada");
			check(shadows != nullptr && grid.visible(*shadows), "e uma de autoria e");
			grid.setShowAdvanced(true);
			check(meshlets != nullptr && grid.visible(*meshlets), "ligando, ela aparece");
			grid.setShowAdvanced(false);
			// Digitar o nome de uma chave escondida tem que acha-la: filtrar e pedir por ela.
			grid.filter().setText("meshlet");
			check(meshlets != nullptr && grid.visible(*meshlets),
			      "procurar por uma chave escondida a revela, em vez de nao dar resultado nenhum");
			grid.filter().setText("");

			// O tamanho do que ficou alcancavel. O numero exato nao importa; que ele nao encolha
			// sem alguem perceber, sim — e foi exatamente assim que este check fez o seu trabalho na
			// E-05: caiu de 44 para 33 quando os onze campos de nuvem sairam daqui, e a queda teve
			// de ser justificada em vez de passar batido. Eles nao sumiram: viraram os campos de
			// `CloudParams`, que e quem sempre desenhou as nuvens.
			check(sky != nullptr && sky->propertyCount == 20, "SkyParams expoe 20 campos");
			check(render != nullptr && render->propertyCount >= 33,
			      "RendererSettings expoe o resto (>=33, era 44 antes da nuvem mudar de casa)");
		}

		// ── C-08: dar um mesh a uma entidade faz geometria aparecer ──────────
		// O passo 4 da Definition of Done. Ate aqui `Add entity` + `MeshComponent` produzia uma
		// referencia que resolvia para um arquivo e para pixel nenhum, porque o sync de malha casa
		// a entidade com um `RenderObject` que ja existe — e uma entidade criada no editor nao tem.
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path root = fs::temp_directory_path() / "tucano_spawn_test";
			fs::remove_all(root, ec);
			fs::create_directories(root, ec);

			editor::SceneTool tool;
			editor::EditorContext toolContext;
			tool.setContext(&toolContext);
			tool.initialize();
			tool.scanAssets(root.string(), true);

			AssetResolver resolver(*device, tool.assets(), root.string());
			Scene scene;
			ecs::World world;
			ecs::RenderSyncState syncState;

			// Nada para instanciar ainda.
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 0,
			      "sem entidades com mesh, nada e criado");

			const ecs::Entity e = world.create();
			world.add<ecs::NameComponent>(e)->name = "Caixa";
			world.add<ecs::TransformComponent>(e)->position = glm::vec3(4.0f, 1.0f, -2.0f);
			world.add<ecs::MeshComponent>(e);

			// Com o slot vazio nao ha o que criar — e esse e o estado de uma malha recem-adicionada.
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 0,
			      "um MeshComponent sem asset atribuido nao cria objeto");
			check(!world.entities().has(e, ecs::kCompRenderObject),
			      "e a entidade nao ganha componente de render a toa");

			// Um GUID desconhecido nao pode criar geometria nem ser tentado para sempre.
			asset::AssetGuid unknown;
			unknown.hi = 4242;
			unknown.lo = 2424;
			world.get<ecs::MeshComponent>(e)->mesh = unknown;
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 0,
			      "um GUID que o indice nao conhece nao cria objeto");
			check(syncState.failedMeshes.size() == 1,
			      "e a falha e lembrada, para nao reler o arquivo a 60 por segundo");
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 0,
			      "a segunda passada nem tenta");
			check(scene.objects.empty(), "a cena continua vazia");

			// Um .tuasset de verdade. Escrito a mao em vez de importado de um glTF porque o que se
			// afirma aqui e a instanciacao, e um importador quebrado faria este teste falhar pelo
			// motivo errado.
			const asset::AssetGuid meshGuid = asset::AssetGuid::fromPath("gate/spawncube");
			{
				// Um triangulo, que e a menor coisa que o Mesh::create aceita e desenha.
				const float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
				const uint32_t indices[] = {0, 1, 2};
				asset::MeshAssetData md{};
				md.vertexCount = 3;
				md.indexCount = 3;
				md.submeshCount = 1;

				asset::TucanoAssetWriter writer(asset::AssetType::Mesh, meshGuid, "gate");
				writer.setMetadata("{\"name\":\"SpawnCube\"}");
				std::vector<uint8_t> payload;
				payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&md),
				               reinterpret_cast<const uint8_t*>(&md) + sizeof(md));
				payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(positions),
				               reinterpret_cast<const uint8_t*>(positions) + sizeof(positions));
				writer.addChunk(asset::ChunkType::Vert, payload.data(), uint32_t(payload.size()));
				writer.addChunk(asset::ChunkType::Indx, indices, uint32_t(sizeof(indices)));
				check(writer.write((root / "SpawnCube.tuasset").string()),
				      "o gate escreveu um .tuasset valido");
			}
			tool.scanAssets(root.string(), true);
			check(tool.assets().find(meshGuid) != nullptr, "e o indice o encontrou pelo GUID");

			// O resolver sozinho: GUID -> Mesh.
			std::string meshError;
			auto resolvedMesh = resolver.mesh(meshGuid, &meshError);
			check(resolvedMesh != nullptr, "o resolver carrega a malha pelo GUID (" + meshError + ")");
			check(resolver.mesh(meshGuid) == resolvedMesh, "e a segunda pedida vem do cache");
			check(resolver.cachedMeshCount() == 1, "uma entrada de cache para uma malha");

			// Um .gltf de origem resolve para um caminho e nao para geometria: e uma cena de
			// primitivas, nao uma malha. A recusa tem que dizer isso.
			std::string sourceError;
			check(resolver.mesh(unknown, &sourceError) == nullptr, "GUID desconhecido segue nulo");
			check(!sourceError.empty(), "e a recusa explica o motivo");

			// E agora a coisa toda: atribuir e a geometria aparecer.
			syncState.clear();  // o projeto foi re-escaneado; o que falhou antes merece nova chance
			world.get<ecs::MeshComponent>(e)->mesh = meshGuid;
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 1,
			      "atribuir uma malha cria um objeto na cena");
			check(scene.objects.size() == 1, "a cena tem o objeto");
			check(world.entities().has(e, ecs::kCompRenderObject),
			      "e a entidade ganhou o componente que a liga a ele");
			const auto* render = world.get<ecs::RenderObjectComponent>(e);
			check(render != nullptr && scene.resolve(render->handle) == &scene.objects[0],
			      "apontando para o objeto certo");
			check(render != nullptr && render->appliedMesh == meshGuid,
			      "e lembrando qual malha ja foi aplicada");
			check(!scene.objects[0].name.empty() && scene.objects[0].name == "Caixa",
			      "o objeto herda o nome da entidade");
			check(scene.objects[0].mesh != nullptr, "e tem geometria de verdade");

			// Nao pode criar de novo no frame seguinte.
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 0,
			      "o frame seguinte nao cria um segundo objeto para a mesma entidade");
			check(scene.objects.size() == 1, "a cena continua com um");

			// Perder o objeto nao pode ser permanente: um reload de cena zera tudo, e sem esta
			// segunda passada a entidade guardaria um handle morto, a criacao nunca dispararia de
			// novo, e a coisa ficaria invisivel pelo resto da sessao — em silencio, porque os syncs
			// so fazem `continue` num handle que nao resolve.
			scene.clearObjects();
			check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 1,
			      "uma entidade que perdeu seu objeto ganha outro");
			check(scene.objects.size() == 1 && scene.objects[0].mesh != nullptr,
			      "com geometria de novo");
			const auto* repointed = world.get<ecs::RenderObjectComponent>(e);
			check(repointed != nullptr && scene.resolve(repointed->handle) == &scene.objects[0],
			      "e o componente foi reapontado, nao duplicado");

			// Play -> Stop destroi toda entidade e a recria do JSON (PlayMode.cpp:64 ->
			// SceneFile.cpp:238), e `RenderObjectComponent` e estado de runtime que arquivo nenhum
			// carrega. Sem reaproveitar, cada ciclo acrescentaria uma copia da cena inteira.
			{
				const size_t before = scene.objects.size();
				world.destroy(e);
				const ecs::Entity revivida = world.create();
				world.add<ecs::NameComponent>(revivida)->name = "Caixa";
				world.add<ecs::TransformComponent>(revivida);
				world.add<ecs::MeshComponent>(revivida)->mesh = meshGuid;
				check(ecs::spawnMeshObjects(world, scene, resolver, syncState) == 1,
				      "a entidade recriada ganha objeto");
				check(scene.objects.size() == before,
				      "reaproveitando o objeto orfao em vez de acrescentar outro");
				const auto* r = world.get<ecs::RenderObjectComponent>(revivida);
				const RenderObject* revived = r != nullptr ? scene.resolve(r->handle) : nullptr;
				check(revived != nullptr, "e o handle novo resolve");
				check(revived != nullptr && revived->visible, "o objeto reaproveitado volta visivel");
				check(revived != nullptr && revived->name == "Caixa",
				      "e nao carrega sobras do ocupante anterior");
			}

			// E a posicao da entidade chega ao objeto, que e o que faz "colocar" significar algo.
			ecs::syncTransformsToScene(world, scene, 1.0f);
			check(scene.objects[0].worldMatrix[3][0] == 0.0f,
			      "e o transform da entidade recriada chega ao objeto");

			fs::remove_all(root, ec);
		}

		// ── C-09: handles estaveis para Scene::objects ───────────────────────
		// `RenderObjectComponent` guardava um **indice** num vetor que era compactado em quatro
		// lugares (descarga de celula, liberacao de tile de terreno, delete do Outliner, reload de
		// cena). Depois de qualquer um deles, todo indice acima do removido nomeava outro objeto — e
		// os syncs so testavam "fora de faixa", que um indice trocado passa. O resultado era uma
		// entidade dirigindo a geometria de outra, em silencio. Handle = indice | geracao.
		{
			const auto cube = makeUnitCube(*device);

			Scene scene;
			RenderObject a;
			a.mesh = cube;
			a.name = "A";
			RenderObject b;
			b.mesh = cube;
			b.name = "B";
			RenderObject c;
			c.mesh = cube;
			c.name = "C";
			const RenderObjectHandle ha = scene.addObject(std::move(a));
			const RenderObjectHandle hb = scene.addObject(std::move(b));
			const RenderObjectHandle hc = scene.addObject(std::move(c));

			check(scene.resolve(ha) != nullptr && scene.resolve(ha)->name == "A",
			      "um handle recem-criado resolve para o seu objeto");
			check(scene.liveObjectCount() == 3, "tres objetos vivos");

			// **O bug inteiro, num check.** Remover o do meio nao pode fazer o handle de C nomear
			// outra coisa. Com indices crus, C passava a ser B.
			check(scene.removeObject(hb), "remover o do meio funciona");
			check(scene.resolve(hc) != nullptr && scene.resolve(hc)->name == "C",
			      "e o handle de quem estava acima continua no MESMO objeto");
			check(scene.resolve(ha) != nullptr && scene.resolve(ha)->name == "A",
			      "e o de quem estava abaixo tambem");

			// O removido le como "sumiu", nao como o objeto do vizinho.
			check(scene.resolve(hb) == nullptr, "o handle do removido nao resolve para nada");
			check(!scene.removeObject(hb), "e remover de novo e recusado em vez de liberar o slot duas vezes");
			check(scene.liveObjectCount() == 2 && scene.objects.size() == 3,
			      "o vetor nao encolhe — e isso que mantem os indices no lugar");

			// O slot vazio nao pode desenhar: todo laco do renderer testa mesh e visible.
			check(scene.objects[1].mesh == nullptr && !scene.objects[1].visible,
			      "o slot liberado fica sem malha e invisivel, entao nenhum passe o desenha");

			// ABA: o slot volta a ser usado, e o handle antigo **nao** pode ressuscitar apontando
			// para o novo dono. E para isso que serve a geracao.
			RenderObject d;
			d.mesh = cube;
			d.name = "D";
			const RenderObjectHandle hd = scene.addObject(std::move(d));
			check(renderObjectIndex(hd) == 1, "o objeto novo reaproveita o slot liberado");
			check(scene.resolve(hd) != nullptr && scene.resolve(hd)->name == "D", "e o handle novo resolve");
			check(scene.resolve(hb) == nullptr,
			      "enquanto o handle antigo do mesmo slot continua morto (geracao)");

			// Um componente zerado. `EntityManager::create` devolve memoria zerada, entao um
			// RenderObjectComponent nascido de `createWith` tem handle 0 — que com geracao comecando
			// em 0 seria um handle valido para o objeto zero, exatamente o erro que isto conserta.
			check(scene.resolve(0u) == nullptr, "um handle zero-inicializado nao resolve para o objeto 0");
			check(scene.resolve(kInvalidRenderObject) == nullptr, "e o handle invalido tambem nao");

			// Reload: tudo morre, inclusive o que vier a ocupar o mesmo indice.
			const RenderObjectHandle antes = ha;
			scene.clearObjects();
			check(scene.objects.empty() && scene.liveObjectCount() == 0, "clearObjects esvazia");
			RenderObject reposto;
			reposto.mesh = cube;
			reposto.name = "Reposto";
			scene.addObject(std::move(reposto));
			check(scene.resolve(antes) == nullptr,
			      "um handle guardado antes do reload nao resolve para quem ocupou o indice depois");

			// Rede de seguranca para o codigo que ainda mexe no vetor direto. Encolher por fora e
			// indistinguivel de "os indices agora significam outra coisa", entao **todo** handle e
			// invalidado — falha visivel e recuperavel em vez de troca silenciosa.
			{
				Scene raw;
				RenderObject um;
				um.mesh = cube;
				um.name = "Um";
				RenderObject dois;
				dois.mesh = cube;
				dois.name = "Dois";
				const RenderObjectHandle h1 = raw.addObject(std::move(um));
				const RenderObjectHandle h2 = raw.addObject(std::move(dois));
				check(raw.resolve(h1) != nullptr && raw.resolve(h2) != nullptr, "dois handles vivos");
				raw.objects.erase(raw.objects.begin());  // o que o codigo legado fazia
				check(raw.resolve(h1) == nullptr && raw.resolve(h2) == nullptr,
				      "um erase por fora invalida todos os handles, em vez de deixar um apontar para o vizinho");

				// E crescer por fora continua funcionando, que e como boa parte da engine ainda
				// popula a cena.
				RenderObject solto;
				solto.mesh = cube;
				solto.name = "Solto";
				raw.objects.push_back(std::move(solto));
				const RenderObjectHandle depois = raw.handleAt(uint32_t(raw.objects.size() - 1));
				check(depois != kInvalidRenderObject && raw.resolve(depois) != nullptr &&
				          raw.resolve(depois)->name == "Solto",
				      "um push_back direto continua ganhando handle utilizavel");
			}

			// removeObjectsIf: o que os providers de streaming chamam.
			{
				Scene streamed;
				for (int i = 0; i < 4; ++i) {
					RenderObject object;
					object.mesh = cube;
					object.name = (i % 2 == 0) ? "cell#7" : "outro";
					streamed.addObject(std::move(object));
				}
				const RenderObjectHandle sobrevivente = streamed.handleAt(1);
				check(streamed.removeObjectsIf(
				          [](const RenderObject& ro) { return ro.name == "cell#7"; }) == 2,
				      "removeObjectsIf devolve quantos saiu");
				check(streamed.liveObjectCount() == 2, "e so os que casaram sairam");
				check(streamed.resolve(sobrevivente) != nullptr &&
				          streamed.resolve(sobrevivente)->name == "outro",
				      "quem nao casou fica onde estava, com o handle valendo");
			}

			// E a ponta que importa: duas entidades, uma perde o objeto, e a outra **nao** passa a
			// dirigir geometria alheia. Este e o cenario que acontecia de verdade a cada descarga de
			// celula de streaming.
			{
				Scene twin;
				ecs::World world;
				RenderObject primeiro;
				primeiro.mesh = cube;
				primeiro.name = "Descarregado";
				RenderObject segundo;
				segundo.mesh = cube;
				segundo.name = "Sobrevivente";
				const RenderObjectHandle hFirst = twin.addObject(std::move(primeiro));
				const RenderObjectHandle hSecond = twin.addObject(std::move(segundo));

				const ecs::Entity vitima = world.create();
				world.add<ecs::TransformComponent>(vitima);
				world.add<ecs::RenderObjectComponent>(vitima)->handle = hFirst;
				const ecs::Entity vizinha = world.create();
				world.add<ecs::TransformComponent>(vizinha)->position = glm::vec3(9.0f, 0.0f, 0.0f);
				world.add<ecs::RenderObjectComponent>(vizinha)->handle = hSecond;

				twin.removeObject(hFirst);
				ecs::syncTransformsToScene(world, twin, 1.0f);

				check(twin.objects[0].worldMatrix[3][0] == 0.0f,
				      "o objeto do slot liberado nao e movido por ninguem");
				check(twin.objects[1].worldMatrix[3][0] == 9.0f,
				      "e a entidade vizinha move o SEU objeto, nao o do vizinho");
			}
		}

		// ── Gizmo e picking no viewport (fecha o passo 4 da DoD) ─────────────
		// "Colocar o objeto na cena, mover com gizmo, duplicar, renomear": colocar, duplicar e
		// renomear ja existiam; mover so dava para fazer digitando numeros no Inspector. O que se
		// testa aqui e a parte que quebra em silencio — a matematica do raio e da matriz. A
		// manipulacao em si e o ImGuizmo, que so um humano arrasta.
		{
			using editor::decomposeMatrix;
			using editor::entityForSceneObject;
			using editor::pickSceneObject;
			using editor::rayHitsAabb;

			const glm::vec3 boxMin(-1.0f);
			const glm::vec3 boxMax(1.0f);
			float t = -1.0f;

			check(rayHitsAabb({0, 0, -5}, {0, 0, 1}, boxMin, boxMax, t) && std::fabs(t - 4.0f) < 1e-4f,
			      "o raio entra na caixa na distancia certa");
			check(!rayHitsAabb({0, 5, -5}, {0, 0, 1}, boxMin, boxMax, t), "e passa por cima sem tocar");
			check(rayHitsAabb({0, 0, 0}, {0, 0, 1}, boxMin, boxMax, t) && t == 0.0f,
			      "de dentro da caixa a distancia e zero, nao negativa");
			// Nada atras da origem conta: uma caixa que a camera ja passou nao pode ser clicavel.
			check(!rayHitsAabb({0, 0, 5}, {0, 0, 1}, boxMin, boxMax, t),
			      "uma caixa atras da origem nao e acertada");
			// Rasante: paralelo a um eixo **e** exatamente no plano da caixa, que e onde o calculo
			// produz 0 * inf = NaN. Este check e afirmacao de comportamento, nao rede de seguranca —
			// mutar o guard de paralelismo fora do rayHitsAabb nao o faz falhar, porque os std::min /
			// std::max absorvem o NaN. Ver o comentario em ViewportInteraction.cpp.
			check(rayHitsAabb({1, 0, -5}, {0, 0, 1}, boxMin, boxMax, t),
			      "rasante a face, paralelo ao eixo, ainda acerta");

			// Picking sobre malha de verdade.
			const auto cube = makeUnitCube(*device);
			check(cube != nullptr && !cube->submeshes().empty(), "o gate tem um cubo com bounds");

			Scene scene;
			ecs::World world;

			const auto addCube = [&](const glm::mat4& matrix, bool visible) {
				RenderObject object;
				object.mesh = cube;
				object.worldMatrix = matrix;
				object.visible = visible;
				scene.objects.push_back(object);
				return static_cast<int>(scene.objects.size() - 1);
			};

			const int near_ = addCube(glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 5.0f)), true);
			const int far_ = addCube(glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 20.0f)), true);
			check(pickSceneObject(scene, {0, 0, 0}, {0, 0, 1}) == near_,
			      "entre dois na mesma linha, ganha o mais proximo");

			scene.objects[near_].visible = false;
			check(pickSceneObject(scene, {0, 0, 0}, {0, 0, 1}) == far_,
			      "um objeto escondido nao e selecionavel — o usuario esta olhando o que esta atras");
			scene.objects[near_].visible = true;
			check(pickSceneObject(scene, {0, 3, 0}, {0, 0, 1}) == -1, "e um raio que passa ao lado nao acerta nada");

			// Espaco do objeto, nao do mundo. A AABB de um cubo girado 45 graus, medida no mundo, e
			// 41% maior que o cubo: com ela o objeto seria selecionavel de onde ele visivelmente nao
			// esta. O ponto (0.65, 0.65) em XZ esta dentro dessa caixa inflada e fora do cubo.
			{
				Scene rotated;
				RenderObject object;
				object.mesh = cube;
				object.worldMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0));
				rotated.objects.push_back(object);
				check(pickSceneObject(rotated, {0.0f, 5.0f, 0.0f}, {0, -1, 0}) == 0,
				      "de cima, o centro do cubo girado e acertado");
				check(pickSceneObject(rotated, {0.65f, 5.0f, 0.65f}, {0, -1, 0}) == -1,
				      "e o canto vazio da caixa girada nao — o teste e no espaco do objeto");
			}

			// A escala do objeto tem que valer: um cubo unitario com escala 3 e clicavel bem alem de
			// onde o cubo unitario acaba.
			{
				Scene scaled;
				RenderObject object;
				object.mesh = cube;
				object.worldMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(3.0f));
				scaled.objects.push_back(object);
				check(pickSceneObject(scaled, {1.2f, 5.0f, 0.0f}, {0, -1, 0}) == 0,
				      "a escala do objeto entra na conta do picking");
			}

			// Do objeto de render de volta para a entidade, que e quem o editor seleciona.
			const ecs::Entity dono = world.create();
			world.add<ecs::NameComponent>(dono)->name = "Dono";
			world.add<ecs::TransformComponent>(dono);
			world.add<ecs::RenderObjectComponent>(dono)->handle =
			    scene.handleAt(static_cast<uint32_t>(far_));
			check(entityForSceneObject(world, scene.handleAt(static_cast<uint32_t>(far_))) == dono,
			      "o objeto de render encontra sua entidade");
			check(entityForSceneObject(world, scene.handleAt(static_cast<uint32_t>(near_))) ==
			          ecs::kInvalidEntity,
			      "e um objeto sem dono (celula, terreno) nao inventa uma");

			// Decompor: o que o gizmo devolve e uma matriz, e o que a entidade guarda sao tres campos.
			{
				const glm::vec3 position(3.0f, -2.0f, 7.5f);
				const glm::quat rotation = glm::angleAxis(glm::radians(37.0f), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
				const glm::vec3 scale(2.0f, 0.5f, 1.25f);
				const glm::mat4 built = glm::translate(glm::mat4(1.0f), position) *
				                        glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);

				glm::vec3 outPosition(0.0f);
				glm::quat outRotation(1, 0, 0, 0);
				glm::vec3 outScale(1.0f);
				decomposeMatrix(built, outPosition, outRotation, outScale);
				check(glm::length(outPosition - position) < 1e-4f, "a posicao volta identica");
				check(glm::length(outScale - scale) < 1e-4f, "e a escala nao-uniforme tambem");
				// Comparado pela matriz e nao pelo quaternion: q e -q sao a mesma rotacao, e um teste
				// que ignora isso reprova por um sinal.
				const glm::mat4 rebuilt = glm::translate(glm::mat4(1.0f), outPosition) *
				                          glm::mat4_cast(outRotation) * glm::scale(glm::mat4(1.0f), outScale);
				float worst = 0.0f;
				for (int c = 0; c < 4; ++c) {
					for (int r = 0; r < 4; ++r) worst = std::max(worst, std::fabs(rebuilt[c][r] - built[c][r]));
				}
				check(worst < 1e-4f, "e recompor devolve a mesma matriz");

				// Espelhado: o quaternion nao sabe refletir, entao a reflexao vai para a escala. Sem
				// isso o objeto viraria do avesso a cada vez que o valor fosse escrito e lido.
				const glm::mat4 mirrored = built * glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
				decomposeMatrix(mirrored, outPosition, outRotation, outScale);
				check(outScale.x < 0.0f, "uma matriz espelhada volta com escala negativa, nao com rotacao invalida");
				check(!std::isnan(outRotation.w), "e sem NaN no quaternion");
			}

			// Escrever de volta na entidade, que e o que um arrasto faz sessenta vezes por segundo.
			{
				ecs::World w;
				editor::UndoStack undo;
				const ecs::Entity e = editor::createEntity(w, &undo, "Movida");
				const glm::mat4 alvo = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
				check(editor::applyMatrixToTransform(w, e, alvo), "a matriz do gizmo chega ao transform");
				const auto* transform = w.get<ecs::TransformComponent>(e);
				check(transform != nullptr && std::fabs(transform->position.x - 10.0f) < 1e-4f,
				      "com a posicao que o gizmo produziu");
				// prevPosition existe para interpolar entre passos fixos. Arrastar e teletransporte:
				// deixar o prev para tras faz o objeto entrar deslizando de onde ele estava.
				check(transform != nullptr && transform->prevPosition == transform->position,
				      "e o estado de interpolacao acompanha, em vez de puxar o objeto de volta");
				check(!editor::applyMatrixToTransform(w, ecs::kInvalidEntity, alvo),
				      "numa entidade que nao existe nao escreve nada");
			}

			// Undo: um passo por gesto, e ele tem que sobreviver ao que mexe na memoria do ECS.
			{
				ecs::World w;
				editor::UndoStack undo;
				const ecs::Entity a = editor::createEntity(w, &undo, "A");
				const ecs::Entity b = editor::createEntity(w, &undo, "B");
				w.get<ecs::TransformComponent>(b)->position = glm::vec3(-50.0f, 0.0f, 0.0f);

				const ecs::TransformComponent antes = *w.get<ecs::TransformComponent>(a);
				check(!editor::pushTransformEdit(w, &undo, a, antes, "Move"),
				      "clicar num handle sem arrastar nao empilha um passo que nao faz nada");

				w.get<ecs::TransformComponent>(a)->position = glm::vec3(5.0f, 1.0f, 2.0f);
				check(editor::pushTransformEdit(w, &undo, a, antes, "Move"), "e um arrasto de verdade empilha");
				check(undo.undoName() == "Move", "com o nome do gesto, nao do codigo");

				// **O motivo de a acao guardar a entidade e nao um ponteiro.** `EntityManager` move os
				// componentes para outro archetype com memcpy quando um componente e adicionado, entao
				// um `TransformComponent*` capturado no arrasto passaria a apontar para o que ocupou
				// aquele slot — e o undo escreveria na entidade errada, em silencio.
				const ecs::AuthoringComponentInfo* table = ecs::authoringComponents();
				for (size_t i = 0; i < ecs::authoringComponentCount(); ++i) {
					if (std::string_view(table[i].key) == "light") editor::addComponent(w, &undo, a, table[i]);
				}

				undo.undo();  // desfaz o Add Component
				undo.undo();  // desfaz o movimento
				const auto* restaurada = w.get<ecs::TransformComponent>(a);
				check(restaurada != nullptr && glm::length(restaurada->position - antes.position) < 1e-4f,
				      "undo devolve a posicao autorada mesmo depois de a entidade ter migrado de archetype");
				const auto* vizinha = w.get<ecs::TransformComponent>(b);
				check(vizinha != nullptr && std::fabs(vizinha->position.x + 50.0f) < 1e-4f,
				      "e nao escreve na entidade que estava do lado");

				undo.redo();
				const auto* refeita = w.get<ecs::TransformComponent>(a);
				check(refeita != nullptr && std::fabs(refeita->position.x - 5.0f) < 1e-4f, "redo repoe o movimento");
			}

			// Um clique no viewport vira selecao — a ponta que liga tudo isto ao editor.
			{
				editor::EditorContext context;
				Camera camera;
				camera.setPerspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 300.0f);
				camera.setPosition({0.0f, 0.0f, -10.0f});
				camera.lookAt({0.0f, 0.0f, 0.0f});

				Scene viewScene;
				RenderObject object;
				object.mesh = cube;
				object.worldMatrix = glm::mat4(1.0f);
				viewScene.objects.push_back(object);

				ecs::World w;
				const ecs::Entity e = w.create();
				w.add<ecs::NameComponent>(e)->name = "Alvo";
				w.add<ecs::TransformComponent>(e);
				w.add<ecs::RenderObjectComponent>(e)->handle = viewScene.handleAt(0);

				context.scene = &viewScene;
				context.world = &w;
				context.camera = &camera;

				check(editor::pickAtViewportPosition(context, 320.0f, 240.0f, 640.0f, 480.0f),
				      "o clique no centro do viewport e respondido");
				check(context.selectedEntity == e,
				      "e seleciona a **entidade**, nao o objeto de render — e ela que o editor edita");
				check(context.selectedObject == 0, "guardando tambem o indice, para hosts sem mundo");

				// Clicar no ceu limpa. Sem isso nao ha como parar de olhar para um gizmo.
				check(editor::pickAtViewportPosition(context, 2.0f, 2.0f, 640.0f, 480.0f),
				      "o clique no vazio tambem e respondido");
				check(!context.hasSelectedEntity() && context.selectedObject == -1,
				      "e limpa a selecao em vez de manter o gizmo no meio da tela");

				// Desenhar o gizmo nao pode, por si so, mover nada: um ImGuizmo mal alimentado devolve
				// a matriz alterada e o objeto anda sozinho ao abrir o painel.
				context.selectedEntity = e;
				const ecs::TransformComponent antes = *w.get<ecs::TransformComponent>(e);
				editor::ViewportGizmoState gizmo;
				bool changed = true;
				for (int i = 0; i < 2; ++i) {
					window.pollEvents();
					ui.beginFrame();
					ImGui::Begin("##gizmoProbe");
					editor::drawViewportGizmo(context, gizmo, 0.0f, 0.0f, 640.0f, 480.0f, changed);
					ImGui::End();
					auto* cmd = device->beginFrame();
					auto& bb = swapChain->backBuffer();
					cmd->transition(bb, rhi::ResourceState::RenderTarget);
					ui.endFrame(*cmd, bb);
					cmd->transition(bb, rhi::ResourceState::Present);
					device->endFrame(*swapChain);
				}
				check(!changed, "desenhar o gizmo sem ninguem arrastando nao marca mudanca");
				const auto* depois = w.get<ecs::TransformComponent>(e);
				check(depois != nullptr && depois->position == antes.position &&
				          depois->scale == antes.scale,
				      "e nao mexe no transform da entidade selecionada");
			}
		}

		// ── C-07: Add / Remove Component ─────────────────────────────────────
		// Ate aqui `Add entity` criava nome + transform e nada mais: nao havia como dar uma luz ou
		// uma malha a uma entidade nova. O editor **editava** o que um import de glTF produziu, e
		// nao **criava** — o que trava os passos 4 e 5 da Definition of Done.
		{
			ecs::World world;
			editor::UndoStack undo;
			const ecs::Entity e = editor::createEntity(world, &undo, "Nova");

			// A lista e uma so. Tres copias dela (arquivo de cena, Inspector, menu de Add) eram tres
			// chances de discordar, e a falha e silenciosa: um componente que salva mas nao aparece.
			check(ecs::authoringComponentCount() == 4, "quatro componentes autoraveis");
			const ecs::AuthoringComponentInfo* table = ecs::authoringComponents();
			const ecs::AuthoringComponentInfo* light = nullptr;
			const ecs::AuthoringComponentInfo* mesh = nullptr;
			const ecs::AuthoringComponentInfo* transform = nullptr;
			for (size_t i = 0; i < ecs::authoringComponentCount(); ++i) {
				const std::string_view key(table[i].key);
				if (key == "light") light = &table[i];
				if (key == "mesh_ref") mesh = &table[i];
				if (key == "transform") transform = &table[i];
			}
			check(light != nullptr && mesh != nullptr && transform != nullptr,
			      "a tabela tem luz, malha e transform");

			// O que `Add entity` deixa de fora, que e o problema inteiro.
			check(!world.entities().has(e, *light->id), "uma entidade nova nao tem luz");

			check(editor::addComponent(world, &undo, e, *light), "adicionar uma luz funciona");
			check(world.entities().has(e, *light->id), "e ela esta la");

			// **Default-construida, nao zerada.** `EntityManager::add(entity, id)` devolve memoria
			// zerada, e uma LightComponent zerada e uma luz preta de alcance zero — pareceria que o
			// Add Component esta quebrado.
			const auto* added = world.get<ecs::LightComponent>(e);
			check(added != nullptr && added->intensity == 1.0f && added->range == 10.0f &&
			          added->type == LightType::Point,
			      "a luz nasce com os padroes do tipo, nao zerada");

			// Adicionar duas vezes nao pode resetar valores que alguem acabou de ajustar.
			world.get<ecs::LightComponent>(e)->intensity = 42.0f;
			check(!editor::addComponent(world, &undo, e, *light), "adicionar de novo recusa");
			check(world.get<ecs::LightComponent>(e)->intensity == 42.0f,
			      "e nao mexe no que ja estava ajustado");

			// Undo e redo do add.
			undo.undo();
			check(!world.entities().has(e, *light->id), "undo tira a luz");
			undo.redo();
			check(world.entities().has(e, *light->id), "redo devolve");
			check(world.get<ecs::LightComponent>(e) != nullptr &&
			          world.get<ecs::LightComponent>(e)->intensity == 42.0f,
			      "com o valor que tinha, nao com o padrao");

			// Remover, e desfazer a remocao trazendo os bytes exatos de volta.
			world.get<ecs::LightComponent>(e)->range = 7.5f;
			check(editor::removeComponent(world, &undo, e, *light), "remover a luz funciona");
			check(!world.entities().has(e, *light->id), "e ela sai");
			undo.undo();
			check(world.entities().has(e, *light->id), "undo da remocao devolve a luz");
			const auto* restored = world.get<ecs::LightComponent>(e);
			check(restored != nullptr && restored->intensity == 42.0f && restored->range == 7.5f,
			      "com os valores exatos que tinha antes");

			// Nome e transform nao saem: sem transform a entidade nao tem onde estar, e sem nome o
			// Outliner nao tem como se referir a ela.
			check(transform != nullptr && transform->essential, "transform e essencial");
			check(!editor::removeComponent(world, &undo, e, *transform),
			      "e remover um essencial e recusado mesmo chamando direto");
			check(world.entities().has(e, *transform->id), "o transform continua la");

			// Uma malha adicionada tambem nasce com padrao util.
			check(editor::addComponent(world, &undo, e, *mesh), "adicionar uma malha funciona");
			const auto* meshRef = world.get<ecs::MeshComponent>(e);
			check(meshRef != nullptr && meshRef->visible, "a malha nasce visivel");
			check(meshRef != nullptr && !meshRef->mesh.valid(),
			      "e sem asset atribuido, que e o estado honesto de uma malha recem-criada");

			// Numa entidade morta nada acontece, em vez de escrever em memoria de ninguem.
			const ecs::Entity dead = world.create();
			world.destroy(dead);
			check(!editor::addComponent(world, &undo, dead, *light),
			      "adicionar numa entidade destruida recusa");
		}

		// ── Viewport ─────────────────────────────────────────────────────────
		// A cena virou uma textura num painel porque nao da para mostra-la *atras* dos paineis: a
		// janela da tool fica ancorada no no central do shell, e um no ocupado pinta por cima de
		// tudo que foi desenhado antes do ImGui. Testado nas duas pontas: o painel pede um tamanho,
		// e o registro da textura devolve um id utilizavel.
		{
			editor::SceneTool tool;
			editor::EditorContext context;
			tool.setContext(&context);
			tool.initialize();
			check(tool.findWindow("Viewport") != nullptr, "o SceneTool declara a janela de Viewport");
			// Sem textura, o painel diz isso em vez de desenhar um retangulo preto que parece render
			// quebrado. E o pedido de tamanho tem que sair mesmo assim, senao o host nunca cria o
			// alvo e o painel nunca recebe textura — um impasse.
			context.sceneTexture = 0;
			context.requestedViewportW = 0;
			context.requestedViewportH = 0;
			for (int i = 0; i < 2; ++i) {
				window.pollEvents();
				ui.beginFrame();
				ImGui::Begin("##viewportProbe");
				editor::EditorTool::ToolWindow* viewport = tool.findWindow("Viewport");
				if (viewport != nullptr && viewport->draw) viewport->draw();
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}
			check(context.requestedViewportW > 0 && context.requestedViewportH > 0,
			      "o painel pede um tamanho mesmo sem textura ainda ligada");

			// E o registro da textura: um alvo offscreen tem que virar um id que o ImGui aceita.
			rhi::TextureDesc desc;
			desc.width = 320;
			desc.height = 200;
			desc.format = rhi::Format::R8G8B8A8_UNORM;
			desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
			desc.debugName = "GateViewportTarget";
			auto target = device->createTexture(desc);
			check(target != nullptr, "o alvo offscreen do viewport e criavel");
			const uint64_t first = target != nullptr ? ui.sceneTextureId(*device, *target) : 0;
			check(first != 0, "e registra como textura do ImGui");
			// Um descritor por frame em voo, rotacionando: sobrescrever um que a GPU ainda pode estar
			// lendo de um frame anterior e indefinido, e o sintoma seria corrupcao intermitente.
			const uint64_t second = target != nullptr ? ui.sceneTextureId(*device, *target) : 0;
			check(second != 0 && second != first,
			      "chamadas seguidas usam descritores diferentes (um por frame em voo)");
		}

		// ── D-02: as luzes que o editor edita sao as que iluminam ────────────
		// Antes disto, as entidades de luz eram criadas *a partir* da cena e nada voltava: mexer em
		// intensidade no Inspector nao mudava pixel nenhum. Mesma forma do problema de material do
		// CP-35, e a mesma verificacao — o que a entidade diz tem que virar o que o renderer usa.
		{
			using ecs::syncLightsToScene;

			Scene scene;
			// Uma luz plantada direto na cena: quem chama o sync esta declarando que o ECS e o dono,
			// entao ela tem que sumir. E a regra, e ela precisa ser visivel num teste.
			scene.addPoint({99.0f, 99.0f, 99.0f}, {1, 1, 1}, 1.0f, 1.0f);

			ecs::World world;
			const ecs::Entity sol = world.create();
			world.add<ecs::NameComponent>(sol)->name = "Sol";
			auto* solTransform = world.add<ecs::TransformComponent>(sol);
			solTransform->position = glm::vec3(0.0f, 10.0f, 0.0f);
			auto* solLight = world.add<ecs::LightComponent>(sol);
			solLight->type = LightType::Directional;
			solLight->intensity = 8.0f;
			solLight->color = glm::vec3(1.0f, 0.9f, 0.8f);

			const ecs::Entity poste = world.create();
			auto* posteTransform = world.add<ecs::TransformComponent>(poste);
			posteTransform->position = glm::vec3(3.0f, 4.0f, -2.0f);
			auto* posteLight = world.add<ecs::LightComponent>(poste);
			posteLight->type = LightType::Spot;
			posteLight->range = 25.0f;
			posteLight->outerCone = 40.0f;

			// Uma entidade de luz sem transform nao tem posicao; coloca-la na origem seria invencao.
			const ecs::Entity semTransform = world.create();
			world.add<ecs::LightComponent>(semTransform)->intensity = 5.0f;

			syncLightsToScene(world, scene);
			check(scene.lights.size() == 2,
			      "o sync reconstroi a lista a partir das entidades (2, nao 3 nem 4)");
			check(scene.lights.size() == 2 &&
			          scene.lights[0].position != glm::vec3(99.0f, 99.0f, 99.0f) &&
			          scene.lights[1].position != glm::vec3(99.0f, 99.0f, 99.0f),
			      "uma luz posta direto na cena nao sobrevive — o ECS e o dono");

			const Light* directional = nullptr;
			const Light* spot = nullptr;
			for (const Light& light : scene.lights) {
				if (light.type == LightType::Directional) directional = &light;
				if (light.type == LightType::Spot) spot = &light;
			}
			check(directional != nullptr && spot != nullptr, "os dois tipos atravessaram");
			check(directional != nullptr && directional->intensity == 8.0f &&
			          directional->color == glm::vec3(1.0f, 0.9f, 0.8f),
			      "intensidade e cor vem do componente");
			check(directional != nullptr && directional->position == glm::vec3(0.0f, 10.0f, 0.0f),
			      "e a posicao vem do transform, nao do componente");
			check(spot != nullptr && spot->range == 25.0f && spot->outerCone == 40.0f,
			      "alcance e cone do spot tambem");

			// Editar o componente muda o que o renderer recebe. Este e o check que faltava existir.
			world.get<ecs::LightComponent>(sol)->intensity = 2.5f;
			syncLightsToScene(world, scene);
			const Light* dimmed = nullptr;
			for (const Light& light : scene.lights) {
				if (light.type == LightType::Directional) dimmed = &light;
			}
			check(dimmed != nullptr && dimmed->intensity == 2.5f,
			      "mudar a intensidade no componente chega ao renderer");

			// Direcao: sem rotacao aponta para baixo, e girar a entidade mira a luz. E o que permite
			// que nao exista um segundo lugar para editar a direcao.
			check(dimmed != nullptr &&
			          glm::length(dimmed->direction - glm::vec3(0.0f, -1.0f, 0.0f)) < 0.001f,
			      "sem rotacao a luz aponta para baixo");
			const glm::vec3 alvo = glm::normalize(glm::vec3(-0.45f, -1.0f, 0.15f));
			world.get<ecs::TransformComponent>(sol)->rotation = ecs::lightRotationFor(alvo);
			syncLightsToScene(world, scene);
			for (const Light& light : scene.lights) {
				if (light.type == LightType::Directional) dimmed = &light;
			}
			// A ida e volta que o host faz ao popular o mundo: direcao -> rotacao -> direcao. Se esta
			// conta estiver errada, toda luz direcional gira no primeiro frame e a cena continua
			// iluminada, so que errado — o tipo de bug mais dificil de notar.
			check(dimmed != nullptr && glm::length(dimmed->direction - alvo) < 0.001f,
			      "girar a entidade mira a luz, e a conta fecha nos dois sentidos");

			// Apagar uma luz tem que remove-la, sem deixar buraco nem desalinhar as outras. E a razao
			// de reconstruir em vez de indexar.
			world.destroy(poste);
			syncLightsToScene(world, scene);
			check(scene.lights.size() == 1, "apagar a entidade apaga a luz");
			check(scene.lights.size() == 1 && scene.lights[0].type == LightType::Directional,
			      "e a que sobrou continua sendo a certa");

			// Sem entidade de luz nenhuma, a lista fica vazia — nao guarda a ultima.
			world.destroy(sol);
			syncLightsToScene(world, scene);
			check(scene.lights.empty(), "sem entidades de luz a cena fica sem luzes");
		}

		// ── Reflexao de enum ─────────────────────────────────────────────────
		// O consumidor (TypeInfo, grid, serializacao) ja existia e nada produzia: `ENUM_MARK` estava
		// declarado no Reflector e nunca usado. O que se afirma aqui e a ponta que faltava.
		{
			const TypeInfo* lightType = TypeRegistry::instance().find(TypeID{"LightType"});
			const TypeInfo* giTier = TypeRegistry::instance().find(TypeID{"GITier"});
			check(lightType != nullptr && lightType->isEnum(), "LightType esta registrado como enum");
			check(giTier != nullptr && giTier->isEnum(), "GITier tambem");
			check(lightType != nullptr && lightType->enumConstantCount == 3,
			      "LightType tem 3 constantes");
			check(giTier != nullptr && giTier->enumConstantCount == 4, "GITier tem 4");

			const EnumConstant* spot =
			    lightType != nullptr ? lightType->findEnumConstant(2) : nullptr;
			check(spot != nullptr && std::string_view(spot->name) == "Spot",
			      "o valor 2 resolve para o nome Spot");
			check(spot != nullptr && std::string_view(spot->label) == "Spot",
			      "e o rotulo mostrado e esse nome");
			check(lightType != nullptr && lightType->findEnumConstant(99) == nullptr,
			      "um valor sem constante nao inventa um nome");

			// O campo tem que *apontar* para o enum. Sem o TypeID o grid cai num spinner de inteiro
			// e o arquivo guarda numero — que era exatamente o estado anterior.
			const PropertyInfo* typeProperty = nullptr;
			if (const TypeInfo* light = TypeRegistry::instance().find(TypeID{"LightComponent"})) {
				typeProperty = light->findProperty("type");
			}
			check(typeProperty != nullptr && typeProperty->coreType == CoreType::Enum,
			      "LightComponent::type e um Enum, nao mais um int32 com faixa 0..2");
			check(typeProperty != nullptr &&
			          TypeRegistry::instance().find(typeProperty->typeId) == lightType,
			      "e o TypeID dele encontra o LightType");

			const PropertyInfo* tierProperty = nullptr;
			if (const TypeInfo* settings = TypeRegistry::instance().find(TypeID{"RendererSettings"})) {
				tierProperty = settings->findProperty("giTier");
			}
			check(tierProperty != nullptr && tierProperty->coreType == CoreType::Enum,
			      "giTier voltou para a superficie refletida como enum");

			// Acesso ciente do tamanho: era `valueIn<int32_t>` em todo lugar, o que so funcionava
			// porque todo enum refletido tem 4 bytes — uma propriedade que ninguem declarou.
			{
				ecs::LightComponent light;
				light.type = LightType::Directional;
				check(typeProperty != nullptr && typeProperty->enumValueIn(&light) == 0,
				      "enumValueIn le o valor pelo tamanho declarado");
				if (typeProperty != nullptr) typeProperty->setEnumValueIn(&light, 2);
				check(light.type == LightType::Spot, "e setEnumValueIn escreve de volta");

				// Um enum estreito: ler 4 bytes aqui pegaria o vizinho e escrever o corromperia.
				struct Narrow {
					uint8_t before = 0xAA;
					uint8_t value = 1;
					uint8_t after = 0xBB;
				} narrow;
				PropertyInfo narrowProperty;
				narrowProperty.coreType = CoreType::Enum;
				narrowProperty.offset = 1;
				narrowProperty.size = 1;
				check(narrowProperty.enumValueIn(&narrow) == 1, "um enum de 1 byte le so aquele byte");
				narrowProperty.setEnumValueIn(&narrow, 2);
				check(narrow.value == 2 && narrow.before == 0xAA && narrow.after == 0xBB,
				      "e escrever nele nao toca nos vizinhos");
			}

			// Ida e volta pelo arquivo: o nome, nao o numero. Inserir uma constante no meio
			// renumera tudo depois dela, e um arquivo de inteiros passaria a significar outra coisa.
			{
				ecs::LightComponent light;
				light.type = LightType::Spot;
				const TypeInfo* type = TypeRegistry::instance().find(TypeID{"LightComponent"});
				const std::string json =
				    type != nullptr ? serializeToJson(*type, &light) : std::string();
				check(!json.empty(), "serializar um componente com enum");
				check(json.find("\"Spot\"") != std::string::npos,
				      "o arquivo guarda o nome da constante, nao o numero");

				ecs::LightComponent restored;
				restored.type = LightType::Directional;
				std::string err;
				check(type != nullptr && deserializeFromJson(json, *type, &restored, &err),
				      "e volta a carregar (" + err + ")");
				check(restored.type == LightType::Spot, "com o mesmo valor");

				// Compatibilidade: cenas escritas antes disto guardavam o inteiro.
				ecs::LightComponent legacy;
				legacy.type = LightType::Directional;
				check(deserializeFromJson("{\"type\":2}", *type, &legacy, nullptr) &&
				          legacy.type == LightType::Spot,
				      "um arquivo antigo com numero ainda carrega");

				// Um nome que o enum nao tem mais: manter o valor atual e reclamar, em vez de
				// chutar um numero e deixar o objeto num estado que ninguem escolheu.
				ecs::LightComponent unknown;
				unknown.type = LightType::Point;
				std::string unknownErr;
				deserializeFromJson("{\"type\":\"Volumetric\"}", *type, &unknown, &unknownErr);
				check(unknown.type == LightType::Point, "um nome desconhecido nao muda o valor");
				check(!unknownErr.empty(), "e e reportado em vez de ignorado");
			}
		}

		// ── Pickers (P4-04) ──────────────────────────────────────────────────
		// O que se afirma aqui e a parte que nao e pixel: a varredura acha os arquivos certos,
		// filtra pelo proposito do campo, e o picker de tipos enxerga o TypeRegistry ordenado.
		{
			using editor::ui::AssetPicker;
			using editor::ui::TypePicker;

			// Classificacao por proposito, sem tocar em disco. Hdri e separado de Texture de
			// proposito: apontar IBL para um PNG de 8 bits da uma imagem plausivel e luz errada.
			check(AssetPicker::matchesKind("Sponza/Sponza.gltf", AssetPicker::Kind::Mesh),
			      "gltf conta como mesh");
			check(!AssetPicker::matchesKind("Sponza/Sponza.gltf", AssetPicker::Kind::Texture),
			      "gltf nao conta como textura");
			check(AssetPicker::matchesKind("IBL/default.hdr", AssetPicker::Kind::Hdri),
			      "hdr conta como hdri");
			check(!AssetPicker::matchesKind("T_Brick.png", AssetPicker::Kind::Hdri),
			      "png nao conta como hdri");
			check(AssetPicker::matchesKind("T_Brick.PNG", AssetPicker::Kind::Texture),
			      "a extensao e comparada sem diferenciar maiusculas");
			check(AssetPicker::matchesKind("qualquer.coisa", AssetPicker::Kind::Any),
			      "Kind::Any aceita tudo");

			// Varredura real do proprio repositorio. Um picker que nao acha os assets do projeto
			// esta quebrado do unico jeito que importa, e uma lista falsa esconderia isso.
			AssetPicker meshes;
			meshes.setRoot("Assets");
			meshes.setKind(AssetPicker::Kind::Mesh);
			meshes.scan();
			check(!meshes.candidates().empty(),
			      "a varredura acha meshes sob Assets/ (" +
			          std::to_string(meshes.candidates().size()) + ")");

			bool allMeshes = true;
			bool separatorsNormalised = true;
			for (const std::string& candidate : meshes.candidates()) {
				if (!AssetPicker::matchesKind(candidate, AssetPicker::Kind::Mesh)) allMeshes = false;
				// O caminho e guardado relativo a raiz e com barras normais: e ele que vai para o
				// disco, e tem que sobreviver a ser lido numa maquina que nunca ouviu falar de '\'.
				if (candidate.find('\\') != std::string::npos) separatorsNormalised = false;
				if (candidate.find(':') != std::string::npos) separatorsNormalised = false;
			}
			check(allMeshes, "toda a lista passa no filtro de proposito");
			check(separatorsNormalised, "os caminhos saem relativos e com barras normais");

			// Ordenada, porque uma lista na ordem de um hash map e inutilizavel.
			check(std::is_sorted(meshes.candidates().begin(), meshes.candidates().end()),
			      "a lista de assets sai ordenada");

			AssetPicker missingRoot;
			missingRoot.setRoot("NaoExiste/DeJeitoNenhum");
			missingRoot.scan();
			check(missingRoot.candidates().empty(), "raiz inexistente da lista vazia, nao crash");

			// Trocar o proposito invalida a varredura em vez de manter a lista antiga.
			meshes.setKind(AssetPicker::Kind::Hdri);
			meshes.scan();
			bool noMeshLeft = true;
			for (const std::string& candidate : meshes.candidates()) {
				if (AssetPicker::matchesKind(candidate, AssetPicker::Kind::Mesh)) noMeshLeft = false;
			}
			check(noMeshLeft, "trocar o Kind refaz a lista");

			TypePicker types;
			types.scan();
			check(types.candidates().size() == TypeRegistry::instance().size(),
			      "sem base, o picker de tipos oferece todos os registrados (" +
			          std::to_string(types.candidates().size()) + ")");
			bool typesSorted = true;
			for (size_t i = 1; i < types.candidates().size(); ++i) {
				if (std::strcmp(types.candidates()[i - 1]->name, types.candidates()[i]->name) > 0) {
					typesSorted = false;
				}
			}
			check(typesSorted, "os tipos saem em ordem alfabetica");

			types.setSelected(TypeID{"Material"});
			check(types.selectedInfo() != nullptr &&
			          std::string_view(types.selectedInfo()->name) == "Material",
			      "o tipo selecionado resolve para o TypeInfo");

			// Sem heranca declarada, uma base que ninguem estende produz so ela mesma — um tipo
			// deriva de si proprio, que e como o registry responde a pergunta.
			types.setBaseType(TypeID{"Material"});
			types.scan();
			check(types.candidates().size() == 1,
			      "com base, o picker filtra pela cadeia de heranca (" +
			          std::to_string(types.candidates().size()) + ")");

			// A metadata que liga o grid ao picker.
			const TypeInfo* materialType = TypeRegistry::instance().find(TypeID{"Material"});
			if (materialType != nullptr) {
				const PropertyInfo* name = materialType->findProperty("name");
				check(name != nullptr && std::string_view(name->meta.assetKind).empty(),
				      "uma string comum nao vira picker por acidente");
			}
		}

		// ── Inspector gerado (P4-05) ─────────────────────────────────────────
		// O painel escrito a mao virou dois grids sobre a reflexao gerada. O que se afirma
		// aqui e o contrato: os tipos estao registrados, os campos que o painel antigo nunca expunha
		// agora existem, e desenhar sobre uma cena real e estavel.
		{
			const auto& registry = TypeRegistry::instance();
			const TypeInfo* transformType = registry.find(TypeID{"Transform"});
			const TypeInfo* objectType = registry.find(TypeID{"RenderObject"});
			const TypeInfo* materialType = registry.find(TypeID{"Material"});

			check(transformType != nullptr, "Transform esta registrado");
			check(objectType != nullptr, "RenderObject esta registrado");
			check(materialType != nullptr, "Material esta registrado");

			if (transformType != nullptr) {
				const PropertyInfo* rotation = transformType->findProperty("rotation");
				check(rotation != nullptr && rotation->coreType == CoreType::Quat,
				      "Transform::rotation e um Quat (o grid o mostra em graus de Euler)");
			}

			if (objectType != nullptr) {
				check(objectType->findProperty("transform") != nullptr &&
				          objectType->findProperty("transform")->coreType == CoreType::Struct,
				      "RenderObject::transform e um struct aninhado");
				// Nao registrados de proposito: sem Pickers (P4-04) uma linha de ponteiro seria pior
				// que nada, e worldMatrix/skinningMatrices sao derivados por frame.
				check(objectType->findProperty("mesh") == nullptr &&
				          objectType->findProperty("worldMatrix") == nullptr &&
				          objectType->findProperty("skinningMatrices") == nullptr,
				      "recursos e dados derivados ficam fora do grid");
			}

			if (materialType != nullptr) {
				// O painel antigo mostrava 5 campos de material. Estes sete eram parametros vivos que
				// nenhuma linha de UI alcancava — aparecem agora porque foram declarados.
				const char* newlyReachable[] = {"aoFactor",    "reflectance", "clearcoatRoughness",
				                                "fuzzColor",   "detailScale", "alphaMask",
				                                "alphaCutoff"};
				int reachable = 0;
				for (const char* name : newlyReachable) {
					if (materialType->findProperty(name) != nullptr) ++reachable;
				}
				check(reachable == 7,
				      "os 7 parametros de material que o painel escrito a mao nunca expos estao no grid (" +
				          std::to_string(reachable) + "/7)");
				// 14 hoje contra os 5 campos do painel antigo. O numero e afirmado para que remover uma
				// propriedade por acidente falhe aqui em vez de sumir da tela em silencio.
				check(materialType->propertyCount == 14,
				      "Material expoe 14 propriedades (" +
				          std::to_string(materialType->propertyCount) + ")");
			}

			// Desenhar sobre uma cena real: um objeto com duas slots de material, que e o caso que o
			// painel antigo tratava mal (mostrava a slot 0 e ignorava o resto).
			Scene scene;
			RenderObject& object = scene.objects.emplace_back();
			object.name = "ProbeObject";
			object.transform.translation = glm::vec3(1.0f, 2.0f, 3.0f);
			object.materials.push_back(std::make_shared<Material>());
			object.materials.push_back(std::make_shared<Material>());
			object.materials[0]->name = "ProbeMaterialA";
			object.materials[1]->name = "ProbeMaterialB";

			editor::EditorContext inspectorContext;
			inspectorContext.scene = &scene;
			inspectorContext.selectedObject = 0;

			editor::InspectorPanel inspector;
			editor::UndoStack inspectorUndo;
			inspector.setUndoStack(&inspectorUndo);

			const glm::vec3 positionBefore = object.transform.translation;
			const glm::quat rotationBefore = object.transform.rotation;
			const float roughnessBefore = object.materials[0]->roughnessFactor;
			bool inspectorReportedChange = false;

			// Uma entidade de verdade para o outro caminho do Inspector.
			ecs::World entityWorld;
			const ecs::Entity inspected = entityWorld.create();
			entityWorld.add<ecs::NameComponent>(inspected)->name = "Inspecionada";
			entityWorld.add<ecs::TransformComponent>(inspected);
			entityWorld.add<ecs::LightComponent>(inspected);
			editor::EditorContext entityContext;
			entityContext.world = &entityWorld;
			entityContext.selectedEntity = inspected;
			editor::InspectorPanel entityInspector;
			editor::UndoStack entityUndo;
			entityInspector.setUndoStack(&entityUndo);
			bool entityDrew = false;

			for (int i = 0; i < 3; ++i) {
				window.pollEvents();
				ui.beginFrame();
				ImGui::Begin("##inspectorProbe");
				inspector.draw(inspectorContext);
				if (inspector.changed()) inspectorReportedChange = true;

				// Os dois caminhos de "nada selecionado" nao podem quebrar: indice fora da faixa e
				// cena ausente. Ambos precisam ser desenhados dentro do frame.
				if (i == 0) {
					editor::EditorContext empty;
					editor::InspectorPanel noScene;
					noScene.draw(empty);
					check(!noScene.changed(), "inspector sem cena nao reporta alteracao");

					editor::EditorContext outOfRange;
					outOfRange.scene = &scene;
					outOfRange.selectedObject = 99;
					editor::InspectorPanel stale;
					stale.draw(outOfRange);
					check(!stale.changed(), "selecao fora da faixa nao reporta alteracao");

					// O caminho de entidade, desenhado de verdade: e onde vivem as linhas de
					// componente, o menu de remover e o botao Add Component (C-07). Sem isto essa UI
					// nunca roda num frame e um erro so apareceria para quem abrisse o editor.
					entityInspector.draw(entityContext);
					entityDrew = true;
				}
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}

			check(entityDrew, "o inspector desenhou o caminho de entidade");
			check(!entityInspector.changed(),
			      "sem input, o inspector de entidade nao reporta alteracao");
			check(entityWorld.entities().has(inspected, ecs::kCompLight),
			      "e nao adiciona nem remove componente sozinho");

			check(!inspectorReportedChange, "sem input, o inspector nao reporta alteracao");
			check(object.transform.translation == positionBefore, "sem input, a posicao nao e tocada");
			// A rotacao e o caso delicado: ela passa por uma conversao quaternion→Euler→quaternion na
			// tela. Se essa ida e volta fosse feita fora de um drag, o valor derivaria sozinho.
			check(object.transform.rotation == rotationBefore,
			      "sem input, a rotacao nao deriva pela conversao de Euler");
			check(object.materials[0]->roughnessFactor == roughnessBefore,
			      "sem input, o material nao e tocado");
			check(!inspectorUndo.canUndo(), "sem input, nada entra na pilha de undo do inspector");
		}

		// ── SceneTool (P2-06) ────────────────────────────────────────────────
		// The panels that used to float loose in the shell are now this tool's windows.
		{
			editor::SceneTool tool;
			tool.initialize();

			// The eight migrated panels, plus the ones the PropertyGrid generates from reflection
			// with no UI written for them: Water and Fog (P4-01), Clouds and Rain (E3-02).
			const char* migrated[] = {"Outliner", "Inspector", "Content Browser", "Console",
			                          "Environment", "Tools",  "Animation",       "Stats"};
			// Sky e Rendering entraram na D-01/E-01: o painel de Environment escrito a mao deixou de
			// ser o unico caminho, e passou a cobrir so o que um grid nao consegue expressar.
			const char* generated[] = {"Sky", "Rendering", "Water", "Fog", "Clouds", "Rain"};

			int found = 0;
			for (const char* name : migrated) {
				if (tool.findWindow(name) != nullptr) ++found;
			}
			check(found == 8, "os 8 paineis migrados estao registrados (" + std::to_string(found) + "/8)");

			int generatedFound = 0;
			for (const char* name : generated) {
				if (tool.findWindow(name) != nullptr) ++generatedFound;
			}
			check(generatedFound == 6, "as 6 janelas geradas por reflection existem (" +
			                               std::to_string(generatedFound) + "/6)");
			// Viewport entrou junto: a cena virou um painel, ancorado no no central que o layout
			// deixa vazio de proposito.
			check(tool.findWindow("Viewport") != nullptr, "a janela de Viewport existe");
			check(tool.windows().size() == 15,
			      "SceneTool declara 15 janelas (" + std::to_string(tool.windows().size()) + ")");

			// A tool with no context must not crash: the panels report "no scene" instead.
			check(tool.context() == nullptr, "contexto comeca nulo e e tolerado");
			check(tool.dockspaceId() != 0, "SceneTool recebe dockspace proprio");
		}

		// ── TypeSystem (P3-01/P3-02) ─────────────────────────────────────────
		{
			const TypeRegistry& registry = TypeRegistry::instance();

			check(TypeID("A") != TypeID("B"), "ids diferentes para nomes diferentes");
			check(TypeID("WaterParams") == TypeID("WaterParams"), "id estavel para o mesmo nome");
			check(!TypeID().isValid() && TypeID("x").isValid(), "id vazio e invalido");

			const TypeInfo* outer = registry.find("ProbeOuter");
			const TypeInfo* inner = registry.find("ProbeInner");
			check(outer != nullptr && inner != nullptr, "tipos se registram sozinhos na carga");
			check(registry.find<ProbeOuter>() == outer, "find<T>() acha o mesmo tipo que find(nome)");
			check(registry.find(TypeID("NaoExiste")) == nullptr, "tipo desconhecido devolve null");

			check(outer->propertyCount == 4, "propriedades declaradas: " + std::to_string(outer->propertyCount));
			const PropertyInfo* rough = outer->findProperty("roughness");
			check(rough != nullptr && rough->coreType == CoreType::Float, "findProperty por nome");
			check(rough != nullptr && std::string(rough->displayLabel()) == "Roughness",
			      "label de apresentacao");
			check(outer->findProperty("naoExiste") == nullptr, "propriedade inexistente devolve null");

			// Reading and writing through the description, with no knowledge of the type.
			ProbeOuter instance;
			check(rough->valueIn<float>(&instance) == 0.25f, "leitura generica pelo offset");
			rough->valueIn<float>(&instance) = 0.9f;
			check(instance.roughness == 0.9f, "escrita generica chega no campo certo");

			// Creating an instance from an id alone — what a loader does with a type name in a file.
			void* created = registry.createInstance(TypeID("ProbeOuter"));
			check(created != nullptr, "createInstance a partir do id");
			if (created != nullptr) {
				check(static_cast<ProbeOuter*>(created)->count == 7, "instancia criada usa o construtor");
				registry.destroyInstance(TypeID("ProbeOuter"), created);
			}

			// Paths.
			check(PropertyPath("a.b[3].c").toString() == "a.b[3].c", "path faz round-trip");
			check(!PropertyPath("a..b").isValid() && !PropertyPath("a.").isValid() &&
			          !PropertyPath("a[").isValid(),
			      "paths malformados sao invalidos, nao lancam");

			// Walking into a nested struct the caller never named in code.
			const PropertyPath nested("inner.y");
			const PropertyPath::Resolved resolved = nested.resolve(registry, *outer, &instance);
			check(static_cast<bool>(resolved), "path aninhado resolve atraves do registro");
			if (resolved) {
				check(*static_cast<float*>(resolved.address) == 2.0f, "endereco resolvido aponta o campo");
				*static_cast<float*>(resolved.address) = 42.0f;
				check(instance.inner.y == 42.0f, "escrita via path chega no campo aninhado");
			}
			check(!PropertyPath("inner.naoExiste").resolve(registry, *outer, &instance),
			      "path para campo inexistente nao resolve");
			check(!PropertyPath("roughness.x").resolve(registry, *outer, &instance),
			      "descer dentro de um float nao resolve");

			// Registering the same id twice keeps the first.
			check(!TypeRegistry::instance().registerType(outer), "registro duplicado e recusado");

			// ── WeatherReflection migrado (P3-04 parcial) ────────────────────
			// The 46 weather fields moved off the old TUCANO_FIELD_* system onto this one.
			const TypeInfo* water = registry.find("WaterParams");
			const TypeInfo* fog = registry.find("FogParams");
			check(water != nullptr && fog != nullptr, "WaterParams e FogParams registrados");
			if (water != nullptr && fog != nullptr) {
				check(water->propertyCount + fog->propertyCount == 46,
				      "os 46 campos migrados estao declarados (" +
				          std::to_string(water->propertyCount + fog->propertyCount) + ")");
				check(water->size == sizeof(WaterParams), "tamanho do tipo bate com a struct");

				// Ranges survived the move: a slider that lost its bounds is a silent regression.
				const PropertyInfo* level = water->findProperty("waterLevel");
				check(level != nullptr && level->meta.minValue == -200.0f && level->meta.maxValue == 200.0f,
				      "limites de apresentacao preservados");

				// Categories are new — the old format could only express them as blank lines.
				check(level != nullptr && std::string(level->meta.category) == "Surface",
				      "categoria atribuida");

				// Vector and colour fields kept their kind, not collapsed to floats.
				const PropertyInfo* absorption = water->findProperty("absorption");
				const PropertyInfo* tint = fog->findProperty("scatteringColor");
				check(absorption != nullptr && absorption->coreType == CoreType::Vec3, "vec3 preservado");
				check(tint != nullptr && tint->coreType == CoreType::Color, "cor preservada como cor");

				// And the description actually addresses the real field.
				WaterParams params;
				params.waterLevel = 12.5f;
				check(level->valueIn<float>(&params) == 12.5f, "offset aponta o campo certo da struct real");
			}
		}

		device->waitIdle();
		ui.shutdown();

		std::cout << "Frame " << sampledWidth << "x" << sampledHeight << "\n\n";
		// A mismatch here means the swapchain and ImGui disagree on the frame size, which silently
		// shifts everything the backend draws.
		check(sampledWidth == kWidth && sampledHeight == kHeight,
		      "backbuffer matches the requested client size");

		if (rgba.empty()) {
			check(false, "frame readback produced pixels");
		} else {
			for (const Probe& p : kProbes) verifyProbe(rgba, p);

			// Icon glyph: any bright pixel in its box means the merged icon font rasterised. Checking a
			// count rather than an exact rect keeps the gate from breaking when the icon set changes.
			int iconPixels = 0;
			for (int y = 275; y < 305; ++y)
				for (int x = 15; x < 55; ++x) {
					const size_t i = (static_cast<size_t>(y) * kWidth + x) * 4;
					if (rgba[i] > 180 && rgba[i + 1] > 180 && rgba[i + 2] > 180) ++iconPixels;
				}
			check(iconPixels > 20, "merged icon glyph rasterised (" + std::to_string(iconPixels) + " px)");

			// Font matrix: 16 distinct faces must have loaded. Distinct pointers is the cheap proxy —
			// a missing .ttf silently collapses onto the fallback and every face becomes the same one.
			check(editor::fontsReady(), "font atlas built from EngineAssets/Fonts");
			int distinct = 0;
			for (int a = 0; a < static_cast<int>(editor::Font::Count); ++a) {
				bool unique = editor::font(static_cast<editor::Font>(a)) != nullptr;
				for (int b = 0; b < a && unique; ++b) {
					if (editor::font(static_cast<editor::Font>(a)) == editor::font(static_cast<editor::Font>(b)))
						unique = false;
				}
				if (unique) ++distinct;
			}
			check(distinct == static_cast<int>(editor::Font::Count),
			      "16 faces distintas (4 tamanhos x 4 estilos): " + std::to_string(distinct));

			// Toasts: both must still be alive, and must have painted over the clear in the corner
			// they stack into. Counting non-clear pixels catches "posted but never drawn".
			check(editor::ui::notificationCount() == 2,
			      "2 toasts vivos: " + std::to_string(editor::ui::notificationCount()));
			int toastPixels = 0;
			for (int y = kHeight - 140; y < static_cast<int>(kHeight) - 5; ++y)
				for (int x = kWidth - 340; x < static_cast<int>(kWidth) - 5; ++x) {
					const size_t i = (static_cast<size_t>(y) * kWidth + x) * 4;
					// The clear is (26,51,102); anything else in this corner came from a toast.
					if (std::abs(static_cast<int>(rgba[i]) - 26) > 12 ||
					    std::abs(static_cast<int>(rgba[i + 1]) - 51) > 12 ||
					    std::abs(static_cast<int>(rgba[i + 2]) - 102) > 12)
						++toastPixels;
				}
			check(toastPixels > 500, "toasts rasterizados no canto inferior direito (" +
			                             std::to_string(toastPixels) + " px)");

			// TreeListView: behaviour, not pixels. Selection surviving a rebuild and the filter
			// keeping a non-matching parent are the two things that break silently and make the
			// Outliner disagree with the world.
			{
				using Item = editor::ui::TreeListView::Item;
				editor::ui::TreeListView tree;
				Item root;
				root.id = 1;
				root.label = "Scene";
				Item a;
				a.id = 2;
				a.label = "Column_A";
				Item b;
				b.id = 3;
				b.label = "Curtain_Red";
				root.children = {a, b};
				tree.setRoot({root});

				check(tree.find(3) != nullptr && tree.find(99) == nullptr,
				      "find() localiza por id e devolve null para id ausente");

				tree.select(3);
				check(tree.isSelected(3) && tree.selection().size() == 1, "select() substitui a selecao");
				tree.select(2, true);
				check(tree.selection().size() == 2, "select(additive) acumula");
				tree.select(2, true);
				check(tree.selection().size() == 1, "select(additive) alterna o ja selecionado");

				// Rebuild without the selected item: the stale id must not survive.
				Item onlyRoot;
				onlyRoot.id = 1;
				onlyRoot.label = "Scene";
				tree.setRoot({onlyRoot});
				check(tree.selection().empty(), "selecao descarta ids que sumiram no rebuild");

				tree.setRoot({root});
				tree.setExpanded(1, false);
				check(!tree.isExpanded(1), "collapse registra");
				tree.setExpanded(1, true);
				check(tree.isExpanded(1), "expand registra");

				editor::ui::Filter filter;
				check(filter.matches("qualquer coisa"), "filtro vazio aceita tudo");
			}

			// Curve: evaluation is what runtime code depends on, so it is tested independently of
			// any drawing.
			{
				editor::ui::Curve curve;
				check(curve.evaluate(0.0f) == 0.0f && curve.evaluate(1.0f) == 1.0f,
				      "curva padrao passa por (0,0) e (1,1)");
				// Smoothstep is symmetric about the midpoint.
				check(std::abs(curve.evaluate(0.5f) - 0.5f) < 0.001f, "curva padrao vale 0.5 no meio");
				check(curve.evaluate(-5.0f) == 0.0f && curve.evaluate(5.0f) == 1.0f,
				      "evaluate() satura fora do dominio");

				const size_t added = curve.addPoint(0.5f, 0.9f);
				check(curve.size() == 3 && added == 1, "addPoint insere na ordem por x");
				check(curve.evaluate(0.5f) > 0.85f, "o ponto novo altera a avaliacao");

				curve.removePoint(0);
				check(curve.size() == 3, "removePoint recusa o extremo inicial");
				curve.removePoint(1);
				check(curve.size() == 2, "removePoint aceita ponto interior");

				// Dragging a point past a neighbour must not fold the curve.
				editor::ui::Curve folded({{0.0f, 0.0f}, {0.4f, 0.5f}, {0.6f, 0.5f}, {1.0f, 1.0f}});
				folded.movePoint(1, 5.0f, 0.5f);
				const auto& pts = folded.points();
				check(pts[1].x < pts[2].x, "movePoint impede o ponto de cruzar o vizinho");
			}

			// EditorTool: the dockspace id is what keeps two open tools from sharing a layout, and
			// what makes a layout survive a restart. Both properties are pure logic.
			{
				ProbeTool a("Material");
				ProbeTool b("Material");
				ProbeTool c("Scene");
				a.setDocumentPath("Assets/Materials/One.tmat");
				b.setDocumentPath("Assets/Materials/Two.tmat");

				check(a.dockspaceId() != b.dockspaceId(),
				      "mesmo tipo, documentos diferentes -> dockspaces diferentes");

				ProbeTool aAgain("Material");
				aAgain.setDocumentPath("Assets/Materials/One.tmat");
				check(a.dockspaceId() == aAgain.dockspaceId(),
				      "mesmo tipo + mesmo documento -> mesmo dockspace (layout sobrevive ao restart)");

				c.initialize();
				check(c.dockspaceId() != 0, "tool sem documento ainda recebe um dockspace valido");
				check(c.windows().size() == 2, "addWindow registra as janelas declaradas");
				check(c.findWindow("Alpha") != nullptr && c.findWindow("Gamma") == nullptr,
				      "findWindow localiza por nome");

				check(a.displayName().find("One.tmat") != std::string::npos,
				      "displayName mostra o nome do arquivo do documento");
				check(c.displayName().find("Scene") != std::string::npos,
				      "displayName cai no nome do tipo sem documento");

				check(!c.isDirty(), "tool nasce limpa");
				c.markDirty();
				check(c.isDirty(), "markDirty marca");
				check(c.save() && !c.isDirty(), "save limpa o estado sujo");
			}

			// ── Release antes da morte do device ──────────────────────────────
			// Caches de tempo-de-processo que seguram recursos de GPU tem que solta-los enquanto o
			// device vive; se esperarem a destruicao estatica, liberam atraves de um device que ja
			// nao existe. Isso ja foi corrigido uma vez e voltou quando o hook que chamava os
			// release() sumiu. Agora o device chama quem se registrou — e este check e o que impede
			// a regressao de acontecer uma terceira vez em silencio.
			{
				bool ranBeforeDestroy = false;
				bool deviceStillUsable = false;
				rhi::Device* raw = device.get();
				raw->onBeforeDestroy([&]() {
					ranBeforeDestroy = true;
					// O ponto todo do hook: o device ainda atende quando ele roda. Se rodasse tarde
					// demais, esta chamada e que seria o crash.
					deviceStillUsable = raw->createSampler() != nullptr;
				});

				swapChain.reset();
				device.reset();

				check(ranBeforeDestroy, "o device roda os callbacks de release ao ser destruido");
				check(deviceStillUsable, "o callback roda com o device ainda utilizavel");
			}
		}
	} catch (const std::exception& ex) {
		std::cerr << "Fatal: " << ex.what() << "\n";
		return 1;
	}

	std::cout << "\n" << (g_failures == 0 ? "PASS" : "FAIL") << " - " << g_failures << " failure(s)\n";
	return g_failures;
}
