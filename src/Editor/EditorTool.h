#pragma once

#include "Editor/UndoStack.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// EditorTool — one tool is one tab in the editor, with its own dockspace inside it.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Core/EditorTool.{h,cpp}
//
// This is the difference between "a dockspace with panels" and an editor. In the flat model there is
// one global layout, so opening a material editor either rearranges the scene layout or fights it.
// Here each tool owns a nested dockspace: the Scene tool arranges its Outliner and Inspector however
// it likes, the Material tool arranges its graph and preview however *it* likes, both layouts persist
// independently, and switching tabs switches the whole workspace. It is the model behind Unreal's
// asset editors and Rider's tool windows.
//
// A tool subclasses this and declares its windows once:
//
//   class SceneTool final : public EditorTool {
//     const char* toolTypeName() const override { return "Scene"; }
//     void onInitialize() override {
//       addWindow("Outliner",  [this] { drawOutliner(); });
//       addWindow("Inspector", [this] { drawInspector(); });
//     }
//     void setupDefaultLayout(uint32_t dockspace, float w, float h) override { ... }
//   };
//
// Two instances of the same tool type (two materials open at once) get different dockspace ids from
// their document path, so their layouts do not collide.

struct ImGuiWindowClass;

namespace tucano::editor {

class EditorTool {
public:
	EditorTool() ;
	virtual ~EditorTool();

	EditorTool(const EditorTool&) = delete;
	EditorTool& operator=(const EditorTool&) = delete;

	// ── Identity ─────────────────────────────────────────────────────────────

	// Stable type name, e.g. "Scene". Part of the dockspace id, so changing it resets saved layouts.
	virtual const char* toolTypeName() const = 0;
	// TUCANO_ICON_* shown in the tab, or null.
	virtual const char* icon() const { return nullptr; }
	// Tab label. Defaults to the icon + type name, plus the document's file name when there is one.
	virtual std::string displayName() const;

	// The file this tool is editing, if any. Two tools of the same type editing different documents
	// are distinct instances with distinct layouts.
	const std::string& documentPath() const { return m_documentPath; }
	void setDocumentPath(std::string path);

	// ── Undo ─────────────────────────────────────────────────────────────────
	// One stack per tool: undoing in the material editor must not reach into the scene.
	UndoStack& undoStack() { return m_undoStack; }
	const UndoStack& undoStack() const { return m_undoStack; }

	// ── Dirty state ──────────────────────────────────────────────────────────

	bool isDirty() const { return m_dirty; }
	void markDirty() { m_dirty = true; }
	// Returns false to refuse closing (a failed write, for instance).
	virtual bool save() {
		m_dirty = false;
		return true;
	}

	// ── Lifecycle ────────────────────────────────────────────────────────────

	// Declare windows here, not in the constructor: displayName() and the document are already set.
	virtual void onInitialize() {}
	virtual void onShutdown() {}
	// Runs every frame while the tool exists, whether or not its tab is visible — a tool that is
	// mid-import should keep importing when the user looks at something else.
	virtual void onUpdate(float deltaSeconds, bool isVisible) {
		(void)deltaSeconds;
		(void)isVisible;
	}

	// ── Windows ──────────────────────────────────────────────────────────────

	struct ToolWindow {
		std::string name;
		std::function<void()> draw;
		bool disableScrolling = false;
		bool noPadding = false; // for windows that draw edge to edge, like a viewport
		bool isOpen = true;
	};

	// Call from onInitialize(). Order is the order they appear in the tool's Window menu.
	void addWindow(std::string name, std::function<void()> draw, bool noPadding = false,
	               bool disableScrolling = false);

	const std::vector<ToolWindow>& windows() const { return m_windows; }

	// Bring one of this tool's windows to the front of whatever dock node it shares. Which tab a
	// shared node opens on is otherwise decided by draw order, which is arbitrary from a user's
	// point of view — it lands on whichever panel happens to be registered last.
	void focusWindow(std::string windowName);
	ToolWindow* findWindow(const std::string& name);

	// Arrange the windows on first run or after a layout reset. `dockspaceId` is already created;
	// split it and dock windows with dockWindow(). Default: everything tabbed in the centre.
	virtual void setupDefaultLayout(uint32_t dockspaceId, float width, float height);

	// Docks one of this tool's windows into a node. Handles the per-instance name suffix, so callers
	// pass the plain name they registered.
	void dockWindow(const std::string& windowName, uint32_t nodeId) const;

	// Throws away the saved layout; rebuilt on the next frame.
	void resetLayout() { m_layoutResetPending = true; }

	// ── Host interface ───────────────────────────────────────────────────────
	// Called by the tool host; not part of a tool's own API.

	void initialize();
	void shutdown();
	// Draws the tool's dockspace and every open window. `hostRegion*` is the space the host gave it.
	void submit(float hostWidth, float hostHeight);

	// Hash of type name + document path. Stable across runs, which is what makes the layout persist.
	uint32_t dockspaceId() const { return m_dockspaceId; }

	// Menu contents for this tool's own Window menu, drawn by the host.
	void drawWindowMenu();

protected:
	// Suffix-qualified name, for anything that needs the real ImGui window id.
	std::string qualifiedWindowName(const std::string& name) const;

private:
	void computeDockspaceId();

	std::string m_documentPath;
	std::vector<ToolWindow> m_windows;
	uint32_t m_dockspaceId = 0;
	bool m_dirty = false;
	bool m_initialized = false;
	bool m_layoutResetPending = false;
	std::string m_focusWindow;
	int m_focusFrames = 0;
	UndoStack m_undoStack;
	std::unique_ptr<ImGuiWindowClass> m_windowClass;
};

} // namespace tucano::editor
