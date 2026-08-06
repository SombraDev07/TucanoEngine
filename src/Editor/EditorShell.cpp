#include "Editor/EditorShell.h"

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* + AddSettingsHandler live in the internal API

#include <cstdio>
#include <cstring>

namespace tucano::editor {
namespace {

constexpr const char* kHostWindow = "##TucanoEditorDockHost";
constexpr const char* kDockSpaceName = "TucanoDockSpace";
constexpr const char* kSettingsType = "TucanoEditor";

constexpr size_t kPanelCount = static_cast<size_t>(Panel::Count);

// What each empty frame tells the user, so an unfinished panel reads as "not built yet" rather
// than "broken". Keep in sync with the roadmap phase that fills it in.
// ASCII only: the default ImGui font atlas has no glyphs beyond Latin-1, so a dash or accent here
// renders as '?'. Panel bodies that need real text have to grow the atlas first.
constexpr const char* kPlaceholder[kPanelCount] = {
    "Scene hierarchy - Track D2.",
    "Selection properties - Track D3.",
    "Assets/ browser with thumbnails - Track D4.",
    "Engine log - pending a log sink.",
};

// --- ImGui settings handler -------------------------------------------------
// ImGui's ini already persists dock layout and window geometry, but not the caller-owned "is this
// panel open" bools. Registering a handler keeps those in the same file, so one ini fully restores
// the editor instead of the layout and the visibility drifting apart.

void* settingsReadOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
	// One section, "Panels". Returning null for anything else makes ImGui skip its lines.
	return std::strcmp(name, "Panels") == 0 ? handler->UserData : nullptr;
}

void settingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
	auto* shell = static_cast<EditorShell*>(entry);
	if (!shell) {
		return;
	}
	char key[64] = {};
	int value = 0;
	if (std::sscanf(line, "%63[^=]=%d", key, &value) != 2) {
		return;
	}
	for (size_t i = 0; i < kPanelCount; ++i) {
		const auto p = static_cast<Panel>(i);
		if (std::strcmp(key, EditorShell::panelName(p)) == 0) {
			shell->setVisible(p, value != 0);
			return;
		}
	}
	// Unknown key: an ini written by a newer build. Ignoring it is what keeps old builds usable.
}

void settingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
	auto* shell = static_cast<EditorShell*>(handler->UserData);
	if (!shell) {
		return;
	}
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
	case Panel::Outliner:
		return "Outliner";
	case Panel::Inspector:
		return "Inspector";
	case Panel::ContentBrowser:
		return "Content Browser";
	case Panel::Console:
		return "Console";
	default:
		return "Panel";
	}
}

bool EditorShell::init(std::string layoutPath) {
	if (ImGui::GetCurrentContext() == nullptr) {
		return false; // DebugUI::init() failed (no device / no heap) — stay inert.
	}
	for (bool& v : m_visible) {
		v = true;
	}

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
	if (!m_ready) {
		return;
	}
	// ImGui flushes on a timer (io.IniSavingRate) and again in DestroyContext, but only when the
	// settings are dirty. Forcing the write here makes "quit right after moving a panel" reliable.
	if (!m_layoutPath.empty()) {
		ImGui::SaveIniSettingsToDisk(m_layoutPath.c_str());
	}
	m_ready = false;
}

bool EditorShell::isVisible(Panel p) const {
	const auto i = static_cast<size_t>(p);
	return i < kPanelCount && m_visible[i];
}

void EditorShell::setVisible(Panel p, bool visible) {
	const auto i = static_cast<size_t>(p);
	if (i >= kPanelCount || m_visible[i] == visible) {
		return;
	}
	m_visible[i] = visible;
	if (m_ready) {
		ImGui::MarkIniSettingsDirty();
	}
}

void EditorShell::resetLayout() {
	m_rebuildLayout = true;
}

void EditorShell::beginFrame() {
	if (!m_ready) {
		return;
	}
	m_ownedThisFrame = 0;

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::SetNextWindowViewport(vp->ID);

	// NoBackground + PassthruCentralNode is what lets the 3D scene show through the middle; without
	// it the host window would paint over the frame the renderer just produced.
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
	// No node for this id means either a first run or a wiped ini — either way, lay it out.
	if (m_rebuildLayout || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
		buildDefaultLayout(dockspaceId);
		m_rebuildLayout = false;
	}
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	drawMenuBar();

	ImGui::End();
}

void EditorShell::buildDefaultLayout(uint32_t dockspaceId) {
	// daEditor-style arrangement: scene in the middle, hierarchy over properties on the right,
	// content/log strip along the bottom.
	const ImGuiID root = dockspaceId;
	ImGui::DockBuilderRemoveNode(root);
	ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

	ImGuiID center = root;
	ImGuiID right = 0;
	ImGuiID bottom = 0;
	ImGuiID rightLower = 0;
	ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);
	ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, &bottom, &center);
	ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, &rightLower, &right);

	ImGui::DockBuilderDockWindow(panelName(Panel::Outliner), right);
	ImGui::DockBuilderDockWindow(panelName(Panel::Inspector), rightLower);
	ImGui::DockBuilderDockWindow(panelName(Panel::ContentBrowser), bottom);
	ImGui::DockBuilderDockWindow(panelName(Panel::Console), bottom);
	// DebugUI's tools window is a plain ImGui window, so docking it here just works and it stops
	// floating over the scene in editor mode.
	ImGui::DockBuilderDockWindow("Tucano Tools", rightLower);

	ImGui::DockBuilderFinish(root);
}

void EditorShell::drawMenuBar() {
	if (!ImGui::BeginMenuBar()) {
		return;
	}
	if (ImGui::BeginMenu("File")) {
		// Scene IO is Track D6; the entries are here disabled so the menu shape is stable.
		ImGui::BeginDisabled();
		ImGui::MenuItem("Open Scene...");
		ImGui::MenuItem("Save Scene");
		ImGui::EndDisabled();
		ImGui::Separator();
		if (ImGui::MenuItem("Exit")) {
			m_quit = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window")) {
		for (size_t i = 0; i < kPanelCount; ++i) {
			const auto p = static_cast<Panel>(i);
			bool visible = m_visible[i];
			if (ImGui::MenuItem(panelName(p), nullptr, &visible)) {
				setVisible(p, visible);
			}
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Layout")) {
		if (ImGui::MenuItem("Reset to Default Layout")) {
			resetLayout();
		}
		ImGui::EndMenu();
	}
	if (!m_status.empty()) {
		const float w = ImGui::CalcTextSize(m_status.c_str()).x;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - w - ImGui::GetStyle().FramePadding.x * 2.0f);
		ImGui::TextUnformatted(m_status.c_str());
	}
	ImGui::EndMenuBar();
}

void EditorShell::panel(Panel p, const std::function<void()>& body) {
	const auto i = static_cast<size_t>(p);
	if (!m_ready || i >= kPanelCount) {
		return;
	}
	m_ownedThisFrame |= (1u << i);
	if (!m_visible[i]) {
		return;
	}
	bool open = true;
	// The window's own close button feeds back into the Window menu state.
	if (ImGui::Begin(panelName(p), &open) && body) {
		body();
	}
	ImGui::End();
	if (!open) {
		setVisible(p, false);
	}
}

void EditorShell::endFrame() {
	if (!m_ready) {
		return;
	}
	for (size_t i = 0; i < kPanelCount; ++i) {
		if (m_ownedThisFrame & (1u << i)) {
			continue;
		}
		const auto p = static_cast<Panel>(i);
		panel(p, [i]() { ImGui::TextDisabled("%s", kPlaceholder[i]); });
	}
}

} // namespace tucano::editor
