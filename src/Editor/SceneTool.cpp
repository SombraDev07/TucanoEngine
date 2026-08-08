#include "Editor/SceneTool.h"

#include "Editor/AnimationPanel.h"
#include "Editor/ConsolePanel.h"
#include "Editor/ContentBrowser.h"
#include "Editor/EditorContext.h"
#include "Editor/EnvironmentPanel.h"
#include "Editor/InspectorPanel.h"
#include "Editor/OutlinerPanel.h"
#include "Editor/StatsPanel.h"
#include "Editor/ToolsPanel.h"
#include "Editor/PropertyGrid.h"
#include "Editor/UI/Icons.h"
#include "Renderer/Renderer.h"
#include "Generated/Reflection.g.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace tucano::editor {

// Held by pointer so the panel headers stay out of SceneTool.h — they pull in ImGui, the renderer
// and std::filesystem, and every translation unit that merely wants to open the tool would inherit
// all of it.
struct SceneTool::Panels {
	OutlinerPanel outliner;
	InspectorPanel inspector;
	ContentBrowser contentBrowser;
	ConsolePanel console;
	EnvironmentPanel environment;
	ToolsPanel tools;
	AnimationPanel animation;
	StatsPanel stats;
	// One grid per block: each keeps its own filter and column widths, which is what a user expects
	// when they narrow one panel and not the other.
	PropertyGrid waterGrid;
	PropertyGrid fogGrid;
};

SceneTool::SceneTool() : m_panels(std::make_unique<Panels>()) {}
SceneTool::~SceneTool() = default;

const char* SceneTool::icon() const { return TUCANO_ICON_FILE_TREE; }

void SceneTool::onInitialize() {
	// Each window is the panel that already existed; the tool only decides where it lives.
	// A null context is drawn as "no scene" by the panels themselves, so there is no guard here.
	const auto withContext = [this](auto&& drawPanel) {
		return [this, drawPanel]() {
			if (m_context == nullptr) {
				ImGui::TextDisabled("No editor context bound.");
				return;
			}
			drawPanel(*m_context);
		};
	};

	addWindow("Outliner", withContext([this](EditorContext& c) { m_panels->outliner.draw(c); }));
	addWindow("Inspector", withContext([this](EditorContext& c) { drawInspector(c); }));
	addWindow("Content Browser",
	          withContext([this](EditorContext& c) { m_panels->contentBrowser.draw(c); }));
	addWindow("Console", withContext([this](EditorContext& c) { m_panels->console.draw(c); }));
	addWindow("Environment", withContext([this](EditorContext& c) { m_panels->environment.draw(c); }));
	// Water and Fog are generated from their reflection data — no UI written per field. This is what
	// P3 was for: adding a setting to WaterParams makes it appear here with its range and tooltip.
	addWindow("Water", withContext([this](EditorContext& c) { drawWater(c); }));
	addWindow("Fog", withContext([this](EditorContext& c) { drawFog(c); }));
	addWindow("Tools", withContext([this](EditorContext& c) { m_panels->tools.draw(c); }));
	addWindow("Animation", withContext([this](EditorContext& c) { m_panels->animation.draw(c); }));
	addWindow("Stats", withContext([this](EditorContext& c) { m_panels->stats.draw(c); }));
}

void SceneTool::drawInspector(EditorContext& context) {
	m_panels->inspector.setUndoStack(&undoStack());
	m_panels->inspector.draw(context);
	if (m_panels->inspector.changed()) markDirty();
}

void SceneTool::drawWater(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->waterGrid.setUndoStack(&undoStack());
	m_panels->waterGrid.drawFilterBox();
	if (m_panels->waterGrid.draw(context.renderer->water())) markDirty();
}

void SceneTool::drawFog(EditorContext& context) {
	if (context.renderer == nullptr) {
		ImGui::TextDisabled("No renderer bound.");
		return;
	}
	m_panels->fogGrid.setUndoStack(&undoStack());
	m_panels->fogGrid.drawFilterBox();
	if (m_panels->fogGrid.draw(context.renderer->fog())) markDirty();
}

void SceneTool::setupDefaultLayout(uint32_t dockspaceId, float width, float height) {
	(void)width;
	(void)height;

	// The arrangement the shell used to impose globally, now owned by the tool: hierarchy over
	// properties on the right, browser and log along the bottom, viewport in the middle.
	ImGuiID centre = static_cast<ImGuiID>(dockspaceId);
	ImGuiID right = 0;
	ImGuiID rightLower = 0;
	ImGuiID bottom = 0;
	ImGuiID bottomRight = 0;

	ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, &right, &centre);
	ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, &rightLower, &right);
	ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, &bottom, &centre);
	ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f, &bottomRight, &bottom);

	dockWindow("Outliner", right);
	dockWindow("Inspector", rightLower);
	dockWindow("Stats", rightLower);
	dockWindow("Content Browser", bottom);
	dockWindow("Console", bottom);
	dockWindow("Environment", bottomRight);
	dockWindow("Water", bottomRight);
	dockWindow("Fog", bottomRight);
	dockWindow("Tools", bottomRight);
	dockWindow("Animation", bottomRight);

	// Draw order would open the right-hand node on Stats and the bottom-right one on Animation,
	// which is nobody's idea of where a fresh editor should start. Only one window can hold focus,
	// so the Inspector wins: it is the one that answers "what am I looking at".
	focusWindow("Inspector");
}

} // namespace tucano::editor
