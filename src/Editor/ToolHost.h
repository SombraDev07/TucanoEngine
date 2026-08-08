#pragma once

#include "Editor/DialogManager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ToolHost — owns the open tools and everything about their lifetime.
//
// Derived from Esoterica (MIT) — Code/Applications/Editor/EditorUI.{h,cpp}
//
// Split out of EditorShell on purpose: the shell is about the frame (dockspace, panels, menu bar),
// the host is about tools (which are open, which is focused, what happens to unsaved work). Letting
// the shell grow both jobs is how a 1,400-line editor class happens.
//
// The part that earns its keep is the close flow. A tool with unsaved changes cannot just vanish
// when its tab's X is clicked, and the prompt cannot be a blocking dialog either — it has to be an
// ImGui modal, which means closing is a small state machine spread over frames rather than a
// function call.

namespace tucano::editor {

class EditorTool;

class ToolHost {
public:
	ToolHost();
	~ToolHost();
	ToolHost(const ToolHost&) = delete;
	ToolHost& operator=(const ToolHost&) = delete;

	// ── Opening / closing ────────────────────────────────────────────────────

	// Takes ownership and initialises the tool. Returns a borrowed pointer.
	// If a tool of the same type is already editing the same document, that one is focused instead
	// and the new one is discarded — opening the same asset twice should not fork it.
	EditorTool* open(std::unique_ptr<EditorTool> tool);

	// Starts closing. Clean tools go immediately; dirty ones raise the save prompt first.
	void requestClose(EditorTool* tool);
	// Closes without asking. For code that has already resolved the unsaved question.
	void forceClose(EditorTool* tool);
	// Asks to close every tool, one prompt at a time. Returns true once nothing is left.
	void requestCloseAll();

	// ── Saving ───────────────────────────────────────────────────────────────

	bool hasUnsavedTools() const;
	// Saves every dirty tool. Returns false if any refused.
	bool saveAll();

	// ── Query ────────────────────────────────────────────────────────────────

	const std::vector<std::unique_ptr<EditorTool>>& tools() const { return m_tools; }
	// Last tool whose window had focus — what "the current tool" means for menu commands.
	EditorTool* focusedTool() const { return m_focused; }
	EditorTool* findByDocument(const char* typeName, const std::string& documentPath) const;

	// ── Layout ───────────────────────────────────────────────────────────────

	// Copies `source`'s window arrangement onto `dest`. Only sensible between tools of the same type,
	// since it maps window names one to one; mismatched pairs are skipped rather than left dangling.
	bool copyLayout(EditorTool* source, EditorTool* dest);

	// ── Per-frame ────────────────────────────────────────────────────────────

	// Advances tool lifetime: retires closed tools and moves the close queue forward. Separate from
	// draw() so lifetime is testable — and usable — without a frame; draw() calls it for you.
	void update();

	// Draws every tool as a tab in `mainDockspaceId`'s central node, plus any pending modal.
	// `deltaSeconds` is forwarded to each tool's onUpdate. Does nothing without a dockspace to dock
	// into: tools are tabs, and there is nowhere to put a tab.
	void draw(uint32_t mainDockspaceId, float deltaSeconds);

	// Menu contents listing the open tools, for the shell's menu bar.
	void drawToolsMenu();

	// Undo/Redo entries for the shell's Edit menu, labelled with what they will undo.
	void drawEditMenu();

	// Ctrl+Z / Ctrl+Y on the focused tool. Called by draw(); exposed so a host with its own input
	// routing can decide when the editor gets the keys.
	void handleUndoShortcuts();

	// Modals the host raises (the unsaved-changes prompt) and that tools can raise too, so every
	// dialog in the editor queues through one place instead of stacking.
	DialogManager& dialogs() { return m_dialogs; }

	// Called when the last prompt resolves and a close-all was in progress.
	std::function<void()> onAllToolsClosed;

private:
	void raiseSavePrompt(EditorTool* tool);
	void destroy(EditorTool* tool);
	void pumpCloseQueue();

	std::vector<std::unique_ptr<EditorTool>> m_tools;
	std::vector<EditorTool*> m_pendingDestroy;
	// Tools waiting their turn at the save prompt; only one modal is up at a time.
	std::vector<EditorTool*> m_closeQueue;
	EditorTool* m_promptTool = nullptr;
	EditorTool* m_focused = nullptr;
	bool m_closingAll = false;
	DialogManager m_dialogs;
	uint32_t m_mainDockspaceId = 0;
	std::vector<uint32_t> m_docked;
};

} // namespace tucano::editor
