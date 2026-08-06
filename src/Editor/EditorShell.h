#pragma once

#include <cstdint>
#include <functional>
#include <string>

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

	// Discard saved layout; default is rebuilt next frame.
	void resetLayout();

	// Panel name as shown in the tab bar.
	static const char* panelName(Panel p);

	// ── Menu callbacks (set by host) ──
	std::function<void()> onOpenScene;
	std::function<void()> onSaveScene;
	std::function<void()> onImportAsset;
	std::function<void()> onNewScene;

private:
	void buildDefaultLayout(uint32_t dockspaceId);
	void drawMenuBar();

	bool m_ready = false;
	bool m_quit = false;
	bool m_rebuildLayout = false;
	std::string m_layoutPath;
	std::string m_status;
	bool m_visible[static_cast<size_t>(Panel::Count)] = {};
	uint32_t m_ownedThisFrame = 0;
};

} // namespace tucano::editor
