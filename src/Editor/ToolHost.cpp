#include "Editor/ToolHost.h"
#include "Editor/EditorTool.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tucano::editor {
namespace {

constexpr const char* kSavePromptId = "Unsaved changes###toolSavePrompt";

} // namespace

ToolHost::ToolHost() = default;
ToolHost::~ToolHost() {
	for (const std::unique_ptr<EditorTool>& tool : m_tools) {
		tool->shutdown();
	}
}

// ── Opening / closing ───────────────────────────────────────────────────────

EditorTool* ToolHost::findByDocument(const char* typeName, const std::string& documentPath) const {
	for (const std::unique_ptr<EditorTool>& tool : m_tools) {
		if (std::strcmp(tool->toolTypeName(), typeName) == 0 && tool->documentPath() == documentPath) {
			return tool.get();
		}
	}
	return nullptr;
}

EditorTool* ToolHost::open(std::unique_ptr<EditorTool> tool) {
	if (!tool) return nullptr;

	// Same asset already open: focus it rather than creating a second view that can disagree with
	// the first about unsaved state.
	if (EditorTool* existing = findByDocument(tool->toolTypeName(), tool->documentPath())) {
		m_focused = existing;
		return existing;
	}

	EditorTool* raw = tool.get();
	raw->initialize();
	m_tools.push_back(std::move(tool));
	m_focused = raw;
	return raw;
}

void ToolHost::requestClose(EditorTool* tool) {
	if (tool == nullptr) return;
	if (!tool->isDirty()) {
		forceClose(tool);
		return;
	}
	// Queue rather than prompt now: several tabs can be closed in one gesture, and stacking modals
	// is worse than asking about them one at a time.
	if (std::find(m_closeQueue.begin(), m_closeQueue.end(), tool) == m_closeQueue.end()) {
		m_closeQueue.push_back(tool);
	}
}

void ToolHost::forceClose(EditorTool* tool) {
	if (tool == nullptr) return;
	// Deferred: this is usually called from inside the tool's own draw.
	if (std::find(m_pendingDestroy.begin(), m_pendingDestroy.end(), tool) == m_pendingDestroy.end()) {
		m_pendingDestroy.push_back(tool);
	}
}

void ToolHost::requestCloseAll() {
	m_closingAll = true;
	// Copy first: requestClose can destroy entries, and iterating the live vector would invalidate.
	std::vector<EditorTool*> all;
	all.reserve(m_tools.size());
	for (const std::unique_ptr<EditorTool>& tool : m_tools) {
		all.push_back(tool.get());
	}
	for (EditorTool* tool : all) {
		requestClose(tool);
	}
}

void ToolHost::destroy(EditorTool* tool) {
	const auto it = std::find_if(m_tools.begin(), m_tools.end(),
	                             [tool](const std::unique_ptr<EditorTool>& t) { return t.get() == tool; });
	if (it == m_tools.end()) return;

	const uint32_t id = (*it)->dockspaceId();
	m_docked.erase(std::remove(m_docked.begin(), m_docked.end(), id), m_docked.end());
	(*it)->shutdown();
	if (m_focused == tool) m_focused = nullptr;
	m_tools.erase(it);
}

// ── Saving ──────────────────────────────────────────────────────────────────

bool ToolHost::hasUnsavedTools() const {
	return std::any_of(m_tools.begin(), m_tools.end(),
	                   [](const std::unique_ptr<EditorTool>& t) { return t->isDirty(); });
}

bool ToolHost::saveAll() {
	bool allOk = true;
	for (const std::unique_ptr<EditorTool>& tool : m_tools) {
		if (tool->isDirty() && !tool->save()) {
			allOk = false;
		}
	}
	return allOk;
}

// ── Layout ──────────────────────────────────────────────────────────────────

bool ToolHost::copyLayout(EditorTool* source, EditorTool* dest) {
	if (source == nullptr || dest == nullptr || source == dest) return false;
	if (std::strcmp(source->toolTypeName(), dest->toolTypeName()) != 0) return false;

	// DockBuilderCopyDockSpace wants the source and destination window names paired up. The two
	// tools share window names and differ only by the id suffix, so the mapping is mechanical —
	// but a window the destination does not have must be skipped, or the copy leaves a dangling
	// reference in the destination's node tree.
	ImVector<const char*> remap;
	std::vector<std::string> storage; // ImVector holds raw pointers; these must outlive the call
	storage.reserve(source->windows().size() * 2);

	for (const EditorTool::ToolWindow& w : source->windows()) {
		bool destHasIt = false;
		for (const EditorTool::ToolWindow& dw : dest->windows()) {
			if (dw.name == w.name) {
				destHasIt = true;
				break;
			}
		}
		if (!destHasIt) continue;

		char suffixSrc[16];
		char suffixDst[16];
		std::snprintf(suffixSrc, sizeof(suffixSrc), "##%08X", source->dockspaceId());
		std::snprintf(suffixDst, sizeof(suffixDst), "##%08X", dest->dockspaceId());
		storage.push_back(w.name + suffixSrc);
		storage.push_back(w.name + suffixDst);
	}
	if (storage.empty()) return false;

	for (const std::string& s : storage) {
		remap.push_back(s.c_str());
	}

	ImGui::DockBuilderCopyDockSpace(static_cast<ImGuiID>(source->dockspaceId()),
	                                static_cast<ImGuiID>(dest->dockspaceId()), &remap);
	ImGui::DockBuilderFinish(static_cast<ImGuiID>(dest->dockspaceId()));
	return true;
}

// ── Per-frame ───────────────────────────────────────────────────────────────

void ToolHost::pumpCloseQueue() {
	// Drop entries that went clean or vanished while queued.
	m_closeQueue.erase(std::remove_if(m_closeQueue.begin(), m_closeQueue.end(),
	                                  [this](EditorTool* t) {
		                                  const bool gone =
		                                      std::none_of(m_tools.begin(), m_tools.end(),
		                                                   [t](const std::unique_ptr<EditorTool>& o) {
			                                                   return o.get() == t;
		                                                   });
		                                  return gone || !t->isDirty();
	                                  }),
	                   m_closeQueue.end());

	if (m_promptTool == nullptr && !m_closeQueue.empty()) {
		m_promptTool = m_closeQueue.front();
		m_closeQueue.erase(m_closeQueue.begin());
		raiseSavePrompt(m_promptTool);
	}

	if (m_closingAll && m_closeQueue.empty() && m_promptTool == nullptr && m_tools.empty()) {
		m_closingAll = false;
		if (onAllToolsClosed) onAllToolsClosed();
	}
}

void ToolHost::raiseSavePrompt(EditorTool* tool) {
	// Queued through the DialogManager rather than owning a modal here: one place decides where a
	// dialog appears, what its buttons look like, and what Escape does.
	m_dialogs.choice(
	    "Unsaved changes", tool->displayName() + " has unsaved changes.", "Save", "Discard",
	    [this, tool](DialogManager::Choice choice) {
		    // The tool may already be gone by the time the user answers, so check before touching it.
		    const bool alive = std::any_of(m_tools.begin(), m_tools.end(),
		                                   [tool](const std::unique_ptr<EditorTool>& t) {
			                                   return t.get() == tool;
		                                   });
		    m_promptTool = nullptr;
		    if (!alive) return;

		    switch (choice) {
			    case DialogManager::Choice::Primary:
				    if (tool->save()) forceClose(tool);
				    break;
			    case DialogManager::Choice::Secondary:
				    forceClose(tool);
				    break;
			    case DialogManager::Choice::Cancel:
				    // Cancelling one tool cancels the whole batch — the user said stop, not "ask me
				    // about the next twelve".
				    m_closeQueue.clear();
				    m_closingAll = false;
				    m_dialogs.clear();
				    break;
		    }
	    });
}

void ToolHost::update() {
	pumpCloseQueue();
	for (EditorTool* tool : m_pendingDestroy) {
		destroy(tool);
	}
	m_pendingDestroy.clear();
}

void ToolHost::draw(uint32_t mainDockspaceId, float deltaSeconds) {
	m_mainDockspaceId = mainDockspaceId;
	const ImGuiID dockspaceId = static_cast<ImGuiID>(mainDockspaceId);
	if (dockspaceId == 0) {
		// No dockspace means no tabs to dock into. Still advance lifetime, so a close requested by
		// code outside the frame is not stranded.
		update();
		return;
	}
	ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId);

	for (const std::unique_ptr<EditorTool>& toolPtr : m_tools) {
		EditorTool* tool = toolPtr.get();

		// "###" keeps the window id stable while the label changes — otherwise the dirty marker
		// appearing reads as a brand new window to ImGui and resets its docking.
		char suffix[24];
		std::snprintf(suffix, sizeof(suffix), "###tool%08X", tool->dockspaceId());
		std::string label = tool->displayName();
		if (tool->isDirty()) label += " *";
		label += suffix;

		if (centralNode != nullptr &&
		    std::find(m_docked.begin(), m_docked.end(), tool->dockspaceId()) == m_docked.end()) {
			ImGui::DockBuilderDockWindow(label.c_str(), centralNode->ID);
			ImGui::DockBuilderFinish(dockspaceId);
			m_docked.push_back(tool->dockspaceId());
		}

		bool open = true;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool visible = ImGui::Begin(label.c_str(), &open, ImGuiWindowFlags_MenuBar);
		ImGui::PopStyleVar();

		if (visible) {
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
				m_focused = tool;
			}
			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu("Window")) {
					tool->drawWindowMenu();
					ImGui::EndMenu();
				}
				if (tool->isDirty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(Style::kAccent0));
					ImGui::TextUnformatted(TUCANO_ICON_CONTENT_SAVE_ALERT "  unsaved");
					ImGui::PopStyleColor();
				}
				ImGui::EndMenuBar();
			}

			const ImVec2 avail = ImGui::GetContentRegionAvail();
			tool->onUpdate(deltaSeconds, true);
			tool->submit(avail.x, avail.y);
		} else {
			// Hidden tab: keep the dockspace alive, or every window docked in it undocks and the
			// layout is lost the moment the user looks at another tool.
			ImGui::DockSpace(static_cast<ImGuiID>(tool->dockspaceId()), ImVec2(0, 0),
			                 ImGuiDockNodeFlags_KeepAliveOnly);
			tool->onUpdate(deltaSeconds, false);
		}
		ImGui::End();

		if (!open) requestClose(tool);
	}

	handleUndoShortcuts();
	pumpCloseQueue();
	m_dialogs.draw();

	for (EditorTool* tool : m_pendingDestroy) {
		destroy(tool);
	}
	m_pendingDestroy.clear();
}



void ToolHost::handleUndoShortcuts() {
	if (m_focused == nullptr) return;
	// Not while a text field has the keyboard: Ctrl+Z there means "undo my typing", which is ImGui's
	// job, not the editor's.
	if (ImGui::GetIO().WantTextInput) return;

	const bool ctrl = ImGui::GetIO().KeyCtrl;
	if (!ctrl) return;

	UndoStack& stack = m_focused->undoStack();
	if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		// Ctrl+Shift+Z redoes as well: half the world expects that instead of Ctrl+Y.
		if (ImGui::GetIO().KeyShift) {
			stack.redo();
		} else {
			stack.undo();
		}
	} else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
		stack.redo();
	}
}

void ToolHost::drawEditMenu() {
	UndoStack* stack = m_focused != nullptr ? &m_focused->undoStack() : nullptr;

	const std::string undoLabel =
	    stack != nullptr && stack->canUndo() ? "Undo " + stack->undoName() : std::string("Undo");
	const std::string redoLabel =
	    stack != nullptr && stack->canRedo() ? "Redo " + stack->redoName() : std::string("Redo");

	ImGui::BeginDisabled(stack == nullptr || !stack->canUndo());
	if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z")) stack->undo();
	ImGui::EndDisabled();

	ImGui::BeginDisabled(stack == nullptr || !stack->canRedo());
	if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y")) stack->redo();
	ImGui::EndDisabled();
}

void ToolHost::drawToolsMenu() {
	if (m_tools.empty()) {
		ImGui::TextDisabled("No tools open");
		return;
	}

	for (const std::unique_ptr<EditorTool>& tool : m_tools) {
		ImGui::PushID(static_cast<int>(tool->dockspaceId()));
		std::string label = tool->displayName();
		if (tool->isDirty()) label += " *";
		if (ImGui::MenuItem(label.c_str(), nullptr, m_focused == tool.get())) {
			char suffix[24];
			std::snprintf(suffix, sizeof(suffix), "###tool%08X", tool->dockspaceId());
			ImGui::SetWindowFocus((tool->displayName() + (tool->isDirty() ? " *" : "") + suffix).c_str());
			m_focused = tool.get();
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	ImGui::BeginDisabled(!hasUnsavedTools());
	if (ImGui::MenuItem(TUCANO_ICON_CONTENT_SAVE_ALL "  Save All", "Ctrl+Shift+S")) {
		saveAll();
	}
	ImGui::EndDisabled();
	if (ImGui::MenuItem(TUCANO_ICON_CLOSE_BOX_MULTIPLE "  Close All")) {
		requestCloseAll();
	}
}

} // namespace tucano::editor
