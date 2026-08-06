#include "Editor/EditorShell.h"

#include <imgui.h>
#include <imgui_internal.h>

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
		if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
		if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
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
		const float w = ImGui::CalcTextSize(m_status.c_str()).x;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - w - ImGui::GetStyle().FramePadding.x * 2.0f);
		ImGui::TextUnformatted(m_status.c_str());
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
	for (size_t i = 0; i < kPanelCount; ++i) {
		if (m_ownedThisFrame & (1u << i)) continue;
		const auto p = static_cast<Panel>(i);
		panel(p, [i]() { ImGui::TextDisabled("%s", kPlaceholder[i]); });
	}
}

} // namespace tucano::editor
