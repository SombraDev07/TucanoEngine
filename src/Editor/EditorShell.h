#pragma once

#include <cstdint>
#include <functional>
#include <string>

// -----------------------------------------------------------------------------
// EditorShell — the docking frame of the native ImGui editor.
//
// Runs in-process with the engine. Every panel reads engine state directly —
// no serialization, no C ABI bridge, no external process.
//
// The panels themselves are empty frames here; callers fill the Outliner,
// Inspector, Content Browser, and Console via panel(). Until a panel is
// drawn, endFrame() paints a placeholder so the frame is visible and dockable.
//
// The central dock node is left empty: the 3D scene renders behind the
// dockspace and shows through the middle.
//
// Deliberately ImGui-free in this header — samples include it without pulling in ImGui.
// -----------------------------------------------------------------------------

namespace tucano::editor {

// Docked panels the shell lays out. Order here is the order they appear in the Window menu.
enum class Panel : uint32_t {
	Outliner,       // D2 — scene hierarchy
	Inspector,      // D3 — properties of the selection
	ContentBrowser, // D4 — Assets/ browser
	Console,        // log sink
	Count,
};

class EditorShell {
public:
	// Call AFTER DebugUI::init() — it needs the ImGui context to exist, and it has to run before
	// the first NewFrame() so ImGui loads `layoutPath` (dock layout + panel visibility) on that
	// frame. Returns false if there is no context, in which case every other method is inert.
	bool init(std::string layoutPath = "EditorLayout.ini");

	// Call BEFORE DebugUI::shutdown(): flushes the layout to disk while the context is still alive.
	void shutdown();

	// Host window + menu bar + dockspace. Builds the default layout the first time (or after
	// resetLayout()), i.e. whenever the ini has no node for the dockspace.
	void beginFrame();

	// Draws one docked panel. `body` runs only when the panel is open and not collapsed. Calling
	// this marks the panel as owned for this frame, which suppresses its placeholder.
	void panel(Panel p, const std::function<void()>& body);

	// Placeholders for panels nobody drew this frame. Pairs with beginFrame().
	void endFrame();

	bool ready() const { return m_ready; }
	// True after File > Exit. The sample's loop is what actually closes the window.
	bool quitRequested() const { return m_quit; }

	bool isVisible(Panel p) const;
	void setVisible(Panel p, bool visible);

	// Right-aligned text in the menu bar. The shell has no view of renderer stats, so the host
	// pushes them here — which is also why the floating perf HUD is redundant in editor mode.
	void setStatus(std::string text) { m_status = std::move(text); }

	// Discards the saved dock layout; the default arrangement is rebuilt on the next beginFrame().
	void resetLayout();

	// Panel name as shown in the tab and stored in the ini.
	static const char* panelName(Panel p);

private:
	void buildDefaultLayout(uint32_t dockspaceId);
	void drawMenuBar();

	bool m_ready = false;
	bool m_quit = false;
	bool m_rebuildLayout = false;
	// io.IniFilename keeps the pointer, not the bytes — this string has to outlive the context.
	std::string m_layoutPath;
	std::string m_status;
	bool m_visible[static_cast<size_t>(Panel::Count)] = {};
	// Bitmask of panels a caller drew this frame; reset by beginFrame(), read by endFrame().
	uint32_t m_ownedThisFrame = 0;
};

} // namespace tucano::editor
