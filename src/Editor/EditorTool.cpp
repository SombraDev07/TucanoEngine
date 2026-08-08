#include "Editor/EditorTool.h"
#include "Editor/UI/Style.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>

namespace tucano::editor {
namespace {

// FNV-1a: a stable hash is required, not a fast one. The dockspace id ends up in the layout ini, so
// it has to produce the same value across runs and builds — std::hash does not promise that.
uint32_t hashString(const std::string& s, uint32_t seed = 2166136261u) {
	uint32_t h = seed;
	for (unsigned char c : s) {
		h ^= c;
		h *= 16777619u;
	}
	return h;
}

std::string fileNameOf(const std::string& path) {
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

EditorTool::EditorTool() : m_windowClass(std::make_unique<ImGuiWindowClass>()) {}
EditorTool::~EditorTool() = default;

std::string EditorTool::displayName() const {
	std::string name;
	if (icon() != nullptr) {
		name += icon();
		name += "  ";
	}
	if (!m_documentPath.empty()) {
		name += fileNameOf(m_documentPath);
	} else {
		name += toolTypeName();
	}
	return name;
}

void EditorTool::setDocumentPath(std::string path) {
	m_documentPath = std::move(path);
	// The id derives from the document, so it has to be recomputed — and the old layout belongs to
	// the old document.
	computeDockspaceId();
}

void EditorTool::computeDockspaceId() {
	// Type + document: two Material tools on different files are different workspaces; the same file
	// reopened later gets its layout back.
	const uint32_t id = hashString(m_documentPath, hashString(toolTypeName()));
	// Zero is ImGui's "no id"; nudge rather than risk a silent no-op dockspace.
	m_dockspaceId = id != 0 ? id : 1u;
}

void EditorTool::addWindow(std::string name, std::function<void()> draw, bool noPadding,
                           bool disableScrolling) {
	ToolWindow w;
	w.name = std::move(name);
	w.draw = std::move(draw);
	w.noPadding = noPadding;
	w.disableScrolling = disableScrolling;
	m_windows.push_back(std::move(w));
}

EditorTool::ToolWindow* EditorTool::findWindow(const std::string& name) {
	const auto it = std::find_if(m_windows.begin(), m_windows.end(),
	                             [&](const ToolWindow& w) { return w.name == name; });
	return it != m_windows.end() ? &*it : nullptr;
}

std::string EditorTool::qualifiedWindowName(const std::string& name) const {
	// Two open tools of the same type would otherwise collide on window ids, and ImGui would treat
	// them as the same window — docking one would move the other.
	char suffix[16];
	std::snprintf(suffix, sizeof(suffix), "##%08X", m_dockspaceId);
	return name + suffix;
}

void EditorTool::dockWindow(const std::string& windowName, uint32_t nodeId) const {
	ImGui::DockBuilderDockWindow(qualifiedWindowName(windowName).c_str(), nodeId);
}

void EditorTool::setupDefaultLayout(uint32_t dockspaceId, float width, float height) {
	(void)width;
	(void)height;
	// Everything tabbed in the centre. A tool that wants a real arrangement overrides this.
	for (const ToolWindow& w : m_windows) {
		dockWindow(w.name, dockspaceId);
	}
}

void EditorTool::initialize() {
	if (m_initialized) return;
	if (m_dockspaceId == 0) computeDockspaceId();
	onInitialize();
	m_initialized = true;
}

void EditorTool::shutdown() {
	if (!m_initialized) return;
	onShutdown();
	m_initialized = false;
}

void EditorTool::drawWindowMenu() {
	for (ToolWindow& w : m_windows) {
		ImGui::MenuItem(w.name.c_str(), nullptr, &w.isOpen);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Reset Layout")) {
		resetLayout();
	}
}

void EditorTool::submit(float hostWidth, float hostHeight) {
	if (!m_initialized) initialize();

	const ImGuiID dockspace = static_cast<ImGuiID>(m_dockspaceId);

	// Constrains this tool's windows to this tool's dockspace: without a class, a panel dragged a
	// little too far escapes into the main dockspace and ends up living outside its own tool.
	m_windowClass->ClassId = dockspace;
	m_windowClass->DockingAllowUnclassed = false;

	const float w = std::max(hostWidth, 1.0f);
	const float h = std::max(hostHeight, 1.0f);

	if (m_layoutResetPending || ImGui::DockBuilderGetNode(dockspace) == nullptr) {
		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace, ImVec2(w, h));
		setupDefaultLayout(m_dockspaceId, w, h);
		ImGui::DockBuilderFinish(dockspace);
		m_layoutResetPending = false;
	}

	ImGui::DockSpace(dockspace, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, m_windowClass.get());

	for (ToolWindow& w : m_windows) {
		if (!w.isOpen) continue;

		ImGui::SetNextWindowClass(m_windowClass.get());
		if (m_focusFrames > 0 && w.name == m_focusWindow) {
			// Held for several frames on purpose. Every window that first appears in a shared dock
			// node makes itself the selected tab as it Begins, so a focus applied once is undone by
			// whichever sibling is drawn after it.
			ImGui::SetNextWindowFocus();
		}
		if (w.noPadding) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		}

		ImGuiWindowFlags flags = ImGuiWindowFlags_None;
		if (w.disableScrolling) {
			flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		}

		const bool visible = ImGui::Begin(qualifiedWindowName(w.name).c_str(), &w.isOpen, flags);
		if (w.noPadding) {
			ImGui::PopStyleVar();
		}
		if (visible && w.draw) {
			w.draw();
		}
		ImGui::End();
	}

	if (m_focusFrames > 0) --m_focusFrames;
}

void EditorTool::focusWindow(std::string windowName) {
	m_focusWindow = std::move(windowName);
	m_focusFrames = 3;
}

} // namespace tucano::editor
