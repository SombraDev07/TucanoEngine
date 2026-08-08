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


#include <algorithm>
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

			// Um tipo sem regra nenhuma nao pode inventar uma.
			check(editor::TypeEditingRules::find(TypeID{"Transform"}) == nullptr,
			      "tipo sem regras declaradas nao ganha regras");
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
				}
				ImGui::End();
				auto* cmd = device->beginFrame();
				auto& bb = swapChain->backBuffer();
				cmd->transition(bb, rhi::ResourceState::RenderTarget);
				ui.endFrame(*cmd, bb);
				cmd->transition(bb, rhi::ResourceState::Present);
				device->endFrame(*swapChain);
			}

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

			// The eight migrated panels, plus Water and Fog which the PropertyGrid generates from
			// reflection (P4-01) with no UI written for them.
			const char* migrated[] = {"Outliner", "Inspector", "Content Browser", "Console",
			                          "Environment", "Tools",  "Animation",       "Stats"};
			const char* generated[] = {"Water", "Fog"};

			int found = 0;
			for (const char* name : migrated) {
				if (tool.findWindow(name) != nullptr) ++found;
			}
			check(found == 8, "os 8 paineis migrados estao registrados (" + std::to_string(found) + "/8)");

			int generatedFound = 0;
			for (const char* name : generated) {
				if (tool.findWindow(name) != nullptr) ++generatedFound;
			}
			check(generatedFound == 2, "as 2 janelas geradas por reflection existem (" +
			                               std::to_string(generatedFound) + "/2)");
			check(tool.windows().size() == 10,
			      "SceneTool declara 10 janelas (" + std::to_string(tool.windows().size()) + ")");

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
