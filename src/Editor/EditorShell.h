#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// EditorShell — the docking frame of the native ImGui editor.
//
// Runs in-process with the engine. Every panel reads engine state directly —
// no serialization, no C ABI bridge, no external process.
//
// The central dock node is left empty: the 3D scene renders behind the
// dockspace and shows through the middle.
//
// Deliberately ImGui-free in this header — samples include it without pulling in ImGui.

namespace tucano::editor {

class EditorTool;
class ToolHost;
class WindowChrome;

enum class Panel : uint32_t {
	Outliner,       // Scene hierarchy tree
	Inspector,      // Properties of selected object
	ContentBrowser, // Project assets
	Console,        // Log output
	Environment,    // Atmosphere, clouds, fog, water, rain, post effects
	Tools,          // Transform tools, terrain/veg/material launchers
	Stats,          // FPS counter, frame breakdown
	Count,
};

class EditorShell {
public:
	EditorShell();
	// Out of line: the tool vector holds unique_ptr<EditorTool>, and destroying one needs the
	// complete type — which a translation unit that only includes this header does not have.
	~EditorShell();
	EditorShell(const EditorShell&) = delete;
	EditorShell& operator=(const EditorShell&) = delete;

	// Call AFTER DebugUI::init() — needs ImGui context.
	bool init(std::string layoutPath = "EditorLayout.ini");

	// Call BEFORE DebugUI::shutdown() — flushes layout to disk.
	void shutdown();

	// Host window + menu bar + dockspace.
	void beginFrame();

	// Draws one docked panel. Call this for each panel every frame.
	void panel(Panel p, const std::function<void()>& body);

	// Placeholders for panels nobody drew this frame.
	void endFrame();

	bool ready() const { return m_ready; }
	bool quitRequested() const { return m_quit; }

	bool isVisible(Panel p) const;
	void setVisible(Panel p, bool visible);

	// Status bar text (right-aligned in menu bar).
	void setStatus(std::string text) { m_status = std::move(text); }

	// ── Borderless chrome ────────────────────────────────────────────────────
	// Replaces the OS title bar with one the editor draws: menu, document name, stats and window
	// buttons in a single strip. Pass the native window handle; returns false if it could not be
	// installed, in which case the ordinary OS caption stays and nothing else changes.
	bool enableBorderlessTitleBar(void* nativeWindowHandle);
	bool borderless() const;

	// Discard saved layout; default is rebuilt next frame.
	void resetLayout();

	// Panel name as shown in the tab bar.
	static const char* panelName(Panel p);

	// ── Tools ────────────────────────────────────────────────────────────────
	// A tool is a tab in the main dockspace with its own nested dockspace inside it, so each one
	// carries its own workspace layout instead of sharing a single global arrangement.

	// Takes ownership. Returns a borrowed pointer so the caller can keep talking to it.
	EditorTool* addTool(std::unique_ptr<EditorTool> tool);
	// Asks to close: a tool with unsaved changes raises the save prompt first.
	void closeTool(EditorTool* tool);
	const std::vector<std::unique_ptr<EditorTool>>& tools() const;

	// Draws every tool. Call between beginFrame() and endFrame().
	void drawTools(float deltaSeconds = 0.0f);

	// The host owns tool lifetime; reach for it directly when you need Save All, layout copying or
	// the focused tool.
	ToolHost& toolHost() { return *m_toolHost; }
	const ToolHost& toolHost() const { return *m_toolHost; }

	// ── Menu callbacks (set by host) ──
	std::function<void()> onOpenScene;
	std::function<void()> onSaveScene;
	std::function<void()> onImportAsset;
	std::function<void()> onNewScene;

private:
	void buildDefaultLayout(uint32_t dockspaceId);
	void drawMenuBar();
	void drawWindowButtons();

	bool m_ready = false;
	bool m_quit = false;
	bool m_rebuildLayout = false;
	std::string m_layoutPath;
	std::string m_status;
	bool m_visible[static_cast<size_t>(Panel::Count)] = {};
	uint32_t m_ownedThisFrame = 0;
	std::unique_ptr<ToolHost> m_toolHost;
	std::unique_ptr<WindowChrome> m_chrome;
	std::string m_windowTitle = "Tucano Editor";
	// Resolved inside the host window during beginFrame(). ImGui::GetID() hashes against the current
	// window's id stack, so calling it again from outside that window yields a different id — which
	// silently pointed the tool docking at a dockspace that does not exist.
	uint32_t m_dockspaceId = 0;
};

} // namespace tucano::editor
