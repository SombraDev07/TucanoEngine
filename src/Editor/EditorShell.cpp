#include "Editor/EditorShell.h"
#include "Editor/EditorTool.h"
#include "Editor/ToolHost.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Widgets.h"
#include "Editor/WindowChrome.h"
#include "Editor/UI/Style.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tucano::editor {
namespace {

constexpr const char* kHostWindow = "##TucanoEditorDockHost";
constexpr const char* kDockSpaceName = "TucanoDockSpace";
constexpr const char* kSettingsType = "TucanoEditor";
constexpr size_t kPanelCount = static_cast<size_t>(Panel::Count);

constexpr const char* kPlaceholder[kPanelCount] = {
	"Scene hierarchy — coming soon.",
	"Object properties — coming soon.",
	"Asset browser — coming soon.",
	"Log output — coming soon.",
	"Environment settings — coming soon.",
	"Tools and gizmos — coming soon.",
	"Performance stats — coming soon.",
};

// --- settings handler ---
void* settingsReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
	return std::strcmp(name, "Panels") == 0 ? handler->UserData : nullptr;
}

void settingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
	auto* shell = static_cast<EditorShell*>(entry);
	if (!shell) return;
	char key[64] = {};
	int value = 0;
	if (std::sscanf(line, "%63[^=]=%d", key, &value) != 2) return;
	for (size_t i = 0; i < kPanelCount; ++i) {
		const auto p = static_cast<Panel>(i);
		if (std::strcmp(key, EditorShell::panelName(p)) == 0) {
			shell->setVisible(p, value != 0);
			return;
		}
	}
}

void settingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
	auto* shell = static_cast<EditorShell*>(handler->UserData);
	if (!shell) return;
	buf->appendf("[%s][Panels]\n", handler->TypeName);
	for (size_t i = 0; i < kPanelCount; ++i) {
		const auto p = static_cast<Panel>(i);
		buf->appendf("%s=%d\n", EditorShell::panelName(p), shell->isVisible(p) ? 1 : 0);
	}
	buf->append("\n");
}

} // namespace

const char* EditorShell::panelName(Panel p) {
	switch (p) {
	case Panel::Outliner:       return "Outliner";
	case Panel::Inspector:      return "Inspector";
	case Panel::ContentBrowser: return "Content Browser";
	case Panel::Console:        return "Console";
	case Panel::Environment:    return "Environment";
	case Panel::Tools:          return "Tools";
	case Panel::Stats:          return "Stats";
	default: return "Panel";
	}
}

bool EditorShell::init(std::string layoutPath) {
	if (ImGui::GetCurrentContext() == nullptr) return false;

	for (bool& v : m_visible) v = true;

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	m_layoutPath = std::move(layoutPath);
	io.IniFilename = m_layoutPath.c_str();

	ImGuiSettingsHandler handler;
	handler.TypeName = kSettingsType;
	handler.TypeHash = ImHashStr(kSettingsType);
	handler.ReadOpenFn = settingsReadOpen;
	handler.ReadLineFn = settingsReadLine;
	handler.WriteAllFn = settingsWriteAll;
	handler.UserData = this;
	ImGui::AddSettingsHandler(&handler);

	m_ready = true;
	return true;
}

void EditorShell::shutdown() {
	if (!m_ready) return;
	if (!m_layoutPath.empty()) {
		ImGui::SaveIniSettingsToDisk(m_layoutPath.c_str());
	}
	// Undo the window subclassing here, not in the destructor: restoring the frame makes Windows
	// deliver messages synchronously, and by destructor time the objects those messages reach —
	// the scene, the swapchain — may already be gone.
	if (m_chrome != nullptr) m_chrome->shutdown();
	m_ready = false;
}

bool EditorShell::isVisible(Panel p) const {
	const auto i = static_cast<size_t>(p);
	return i < kPanelCount && m_visible[i];
}

void EditorShell::setVisible(Panel p, bool visible) {
	const auto i = static_cast<size_t>(p);
	if (i >= kPanelCount || m_visible[i] == visible) return;
	m_visible[i] = visible;
	if (m_ready) ImGui::MarkIniSettingsDirty();
}

void EditorShell::resetLayout() { m_rebuildLayout = true; }

void EditorShell::beginFrame() {
	if (!m_ready) return;
	m_ownedThisFrame = 0;

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::SetNextWindowViewport(vp->ID);

	const ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
	                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
	                               ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(kHostWindow, nullptr, flags);
	ImGui::PopStyleVar(3);

	const ImGuiID dockspaceId = ImGui::GetID(kDockSpaceName);
	m_dockspaceId = static_cast<uint32_t>(dockspaceId);
	if (m_rebuildLayout || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
		buildDefaultLayout(dockspaceId);
		m_rebuildLayout = false;
	}
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	drawMenuBar();
	ImGui::End();
}

void EditorShell::buildDefaultLayout(uint32_t dockspaceId) {
	// Layout: center=viewport, right=Outliner+Inspector, bottom-left=Content+Console,
	//         bottom-right=Environment+Tools, right-lower=Stats
	const ImGuiID root = dockspaceId;
	ImGui::DockBuilderRemoveNode(root);
	ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

	ImGuiID center = root;
	ImGuiID right = 0, bottom = 0, rightLower = 0, bottomRight = 0;
	ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);
	ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, &bottom, &center);
	ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, &rightLower, &right);
	ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.50f, &bottomRight, &bottom);

	ImGui::DockBuilderDockWindow(panelName(Panel::Outliner), right);
	ImGui::DockBuilderDockWindow(panelName(Panel::Inspector), rightLower);
	ImGui::DockBuilderDockWindow(panelName(Panel::ContentBrowser), bottom);
	ImGui::DockBuilderDockWindow(panelName(Panel::Console), bottom);
	ImGui::DockBuilderDockWindow(panelName(Panel::Environment), bottomRight);
	ImGui::DockBuilderDockWindow(panelName(Panel::Tools), bottomRight);
	ImGui::DockBuilderDockWindow(panelName(Panel::Stats), rightLower);

	ImGui::DockBuilderFinish(root);
}

void EditorShell::drawMenuBar() {
	if (!ImGui::BeginMenuBar()) return;

	// Brand on the left, ahead of the menus — where every borderless editor puts it, and the one
	// place in a menu bar whose position is not fought over by SameLine offsets.
	if (borderless()) {
		ui::textColored(Style::kAccent0, TUCANO_ICON_BIRD "  Tucano");
		ImGui::Spacing();
		ImGui::SameLine();
	}

	// ── File ──
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
			if (onNewScene) onNewScene();
		}
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
			if (onOpenScene) onOpenScene();
		}
		if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
			if (onSaveScene) onSaveScene();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Import Asset...")) {
			if (onImportAsset) onImportAsset();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", "Alt+F4")) {
			m_quit = true;
		}
		ImGui::EndMenu();
	}

	// ── Edit ──
	if (ImGui::BeginMenu("Edit")) {
		// Undo/Redo come from the focused tool's own stack, labelled with what they will undo.
		m_toolHost->drawEditMenu();
		ImGui::Separator();
		if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {}
		if (ImGui::MenuItem("Delete", "Del")) {}
		ImGui::EndMenu();
	}

	// ── View ──
	if (ImGui::BeginMenu("View")) {
		for (size_t i = 0; i < kPanelCount; ++i) {
			const auto p = static_cast<Panel>(i);
			bool visible = m_visible[i];
			if (ImGui::MenuItem(panelName(p), nullptr, &visible)) {
				setVisible(p, visible);
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout")) {
			resetLayout();
		}
		ImGui::EndMenu();
	}

	// ── Tools ──
	if (ImGui::BeginMenu("Tools")) {
		m_toolHost->drawToolsMenu();
		ImGui::Separator();
		if (ImGui::MenuItem("Terrain Sculpt")) {}
		if (ImGui::MenuItem("Vegetation Paint")) {}
		if (ImGui::MenuItem("Material Editor")) {}
		if (ImGui::MenuItem("Animation Graph")) {}
		ImGui::Separator();
		if (ImGui::MenuItem("Screenshot (F12)")) {}
		ImGui::EndMenu();
	}

	// ── Help ──
	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("About Tucano Engine")) {}
		ImGui::EndMenu();
	}

	// Status bar
	if (!m_status.empty()) {
		// Leave room for the window buttons when they are present, or the status text slides under
		// them and the close button becomes unreadable.
		const float buttonRoom = borderless() ? ImGui::GetFrameHeight() * 4.8f : 0.0f;
		const float w = ImGui::CalcTextSize(m_status.c_str()).x;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - w - buttonRoom -
		                ImGui::GetStyle().FramePadding.x * 2.0f);
		ImGui::TextUnformatted(m_status.c_str());
	}

	drawWindowButtons();

	if (borderless()) {
		// Windows needs both numbers to decide whether a click drags the window: how tall the bar we
		// drew is, and whether the cursor is over something in it that should be clicked instead.
		m_chrome->setTitleBarHeight(ImGui::GetFrameHeight());
		m_chrome->setInteractiveHovered(ImGui::IsAnyItemHovered() ||
		                                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
	}

	ImGui::EndMenuBar();
}

void EditorShell::panel(Panel p, const std::function<void()>& body) {
	const auto i = static_cast<size_t>(p);
	if (!m_ready || i >= kPanelCount) return;
	m_ownedThisFrame |= (1u << i);
	if (!m_visible[i]) return;
	bool open = true;
	if (ImGui::Begin(panelName(p), &open) && body) {
		body();
	}
	ImGui::End();
	if (!open) setVisible(p, false);
}

void EditorShell::endFrame() {
	if (!m_ready) return;
	// Once a tool is open it owns these panels as tool windows (P2-06). Drawing the shell's own
	// copies too would put two "Outliner" windows on screen, one of them permanently empty.
	if (!m_toolHost->tools().empty()) return;
	for (size_t i = 0; i < kPanelCount; ++i) {
		if (m_ownedThisFrame & (1u << i)) continue;
		const auto p = static_cast<Panel>(i);
		panel(p, [i]() { ImGui::TextDisabled("%s", kPlaceholder[i]); });
	}
}

// ── Tools ───────────────────────────────────────────────────────────────────
// Lifetime lives in ToolHost; the shell only supplies the dockspace they dock into.

EditorShell::EditorShell() : m_toolHost(std::make_unique<ToolHost>()) {}
EditorShell::~EditorShell() = default;

EditorTool* EditorShell::addTool(std::unique_ptr<EditorTool> tool) {
	return m_toolHost->open(std::move(tool));
}

void EditorShell::closeTool(EditorTool* tool) { m_toolHost->requestClose(tool); }

const std::vector<std::unique_ptr<EditorTool>>& EditorShell::tools() const {
	return m_toolHost->tools();
}

void EditorShell::drawTools(float deltaSeconds) {
	if (!m_ready) return;
	m_toolHost->draw(m_dockspaceId, deltaSeconds);
}

// ── Borderless chrome ───────────────────────────────────────────────────────

bool EditorShell::enableBorderlessTitleBar(void* nativeWindowHandle) {
	if (m_chrome == nullptr) m_chrome = std::make_unique<WindowChrome>();
	return m_chrome->install(nativeWindowHandle);
}

bool EditorShell::borderless() const { return m_chrome != nullptr && m_chrome->installed(); }

void EditorShell::drawWindowButtons() {
	if (!borderless()) return;

	const ImGuiStyle& style = ImGui::GetStyle();
	const float buttonWidth = ImGui::GetFrameHeight() * 1.6f;
	// Right-aligned, in the order Windows uses — muscle memory puts close in the corner.
	const float total = buttonWidth * 3.0f;
	ImGui::SameLine(ImGui::GetContentRegionMax().x - total - style.FramePadding.x);

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

	if (ImGui::Button(TUCANO_ICON_WINDOW_MINIMIZE, ImVec2(buttonWidth, 0.0f))) {
		m_chrome->minimize();
	}
	ImGui::SameLine(0.0f, 0.0f);
	if (ImGui::Button(m_chrome->isMaximized() ? TUCANO_ICON_WINDOW_RESTORE : TUCANO_ICON_WINDOW_MAXIMIZE,
	                  ImVec2(buttonWidth, 0.0f))) {
		m_chrome->toggleMaximize();
	}
	ImGui::SameLine(0.0f, 0.0f);
	// Close gets the destructive hover colour, the way every window manager marks it.
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.79f, 0.13f, 0.13f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
	if (ImGui::Button(TUCANO_ICON_CLOSE, ImVec2(buttonWidth, 0.0f))) {
		m_chrome->requestClose();
	}
	ImGui::PopStyleColor(2);

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

} // namespace tucano::editor
