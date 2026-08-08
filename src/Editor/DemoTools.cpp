#include "Editor/DemoTools.h"
#include "Editor/EditorTool.h"
#include "Editor/UndoStack.h"
#include "Editor/UI/CurveEditor.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"
#include "Editor/UI/TreeListView.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace tucano::editor {
namespace {

using namespace tucano::editor::ui;

class SceneDemoTool final : public EditorTool {
public:
	const char* toolTypeName() const override { return "Scene"; }
	const char* icon() const override { return TUCANO_ICON_FILE_TREE; }

	void onInitialize() override {
		addWindow("Outliner", [this] { drawOutliner(); });
		addWindow("Inspector", [this] { drawInspector(); });
		addWindow("Console", [this] { drawConsole(); });

		TreeListView::Item scene;
		scene.id = 1;
		scene.label = "Sponza";
		scene.icon = TUCANO_ICON_FILE_TREE;
		for (int i = 0; i < 5; ++i) {
			TreeListView::Item child;
			child.id = static_cast<uint64_t>(10 + i);
			child.label = std::string("Object_") + static_cast<char>('A' + i);
			child.icon = TUCANO_ICON_CUBE_OUTLINE;
			child.iconColor = Style::kAccent1;
			scene.children.push_back(child);
		}
		m_tree.setRoot({scene});
		m_tree.select(11);
		// Editing anything in the inspector marks the document dirty, which is what the tab's
		// unsaved marker and the close prompt (P2-02) hang off.
		m_tree.onSelectionChanged = [] {};
	}

	void setupDefaultLayout(uint32_t dockspaceId, float width, float height) override {
		(void)width;
		(void)height;
		ImGuiID centre = static_cast<ImGuiID>(dockspaceId);
		ImGuiID right = 0;
		ImGuiID bottom = 0;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.42f, &right, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, &bottom, &centre);
		dockWindow("Outliner", centre);
		dockWindow("Inspector", right);
		dockWindow("Console", bottom);
	}

private:
	void drawOutliner() {
		m_filter.setHint("Filter objects...");
		m_filter.draw("##sceneFilter");
		m_tree.draw(&m_filter);
	}

	// Every edit goes through the undo stack. The pattern is always the same: remember the value
	// before the widget, push the change after it, and end the merge run when the widget is released
	// — otherwise a drag becomes one undo step per frame.
	void undoableFloat(const char* label, float* value, float min, float max) {
		const float before = *value;
		if (ImGui::SliderFloat(label, value, min, max)) {
			undoStack().pushValue(label, value, before, *value);
			markDirty();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) undoStack().breakMerge();
	}

	// A vector edit is one gesture, so its three components collapse into one undo step — otherwise
	// dragging X, Y and Z costs three Ctrl+Z to put back.
	void undoableVec3(const char* label, float* v, float speed) {
		const float before[3] = {v[0], v[1], v[2]};
		if (ImGui::DragFloat3(label, v, speed)) {
			UndoStack::Compound c(undoStack(), label);
			undoStack().pushValue(std::string(label) + ".x", &v[0], before[0], v[0]);
			undoStack().pushValue(std::string(label) + ".y", &v[1], before[1], v[1]);
			undoStack().pushValue(std::string(label) + ".z", &v[2], before[2], v[2]);
			markDirty();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) undoStack().breakMerge();
	}

	void drawInspector() {
		sectionHeader(TUCANO_ICON_TUNE "  Transform");
		undoableVec3("Position", m_position, 0.01f);
		undoableVec3("Rotation", m_rotation, 0.5f);
		sectionHeader(TUCANO_ICON_PALETTE "  Material");
		undoableFloat("Roughness", &m_roughness, 0.0f, 1.0f);
		{
			const bool before = m_castShadows;
			if (ImGui::Checkbox("Cast shadows", &m_castShadows)) {
				undoStack().pushValue("Cast shadows", &m_castShadows, before, m_castShadows);
				undoStack().breakMerge(); // a click is a complete gesture
				markDirty();
			}
		}

		ImGui::Spacing();
		ImGui::TextDisabled("undo: %zu   redo: %zu", undoStack().undoDepth(), undoStack().redoDepth());
		if (undoStack().canUndo()) {
			ImGui::TextDisabled("next undo: %s", undoStack().undoName().c_str());
		}
	}

	void drawConsole() {
		ImGui::TextDisabled("[info]  Loaded Sponza.gltf (25 materials)");
		ImGui::TextDisabled("[info]  Built BVH in 42 ms");
		textColored(0xFF3CC8F0, "[warn]  Mesh 'Curtain_Red' has no tangents");
	}

	TreeListView m_tree;
	Filter m_filter;
	float m_position[3] = {0.0f, 1.5f, 0.0f};
	float m_rotation[3] = {0.0f, 0.0f, 0.0f};
	float m_roughness = 0.4f;
	bool m_castShadows = true;
};

class MaterialDemoTool final : public EditorTool {
public:
	const char* toolTypeName() const override { return "Material"; }
	const char* icon() const override { return TUCANO_ICON_PALETTE; }

	void onInitialize() override {
		addWindow("Preview", [this] { drawPreview(); });
		addWindow("Properties", [this] { drawProperties(); });
		addWindow("Graph", [this] { drawGraph(); }, /*noPadding*/ true);
	}

	// Deliberately a different shape from the scene tool: the whole point is that each tool keeps
	// its own arrangement rather than sharing one global layout.
	void setupDefaultLayout(uint32_t dockspaceId, float width, float height) override {
		(void)width;
		(void)height;
		ImGuiID centre = static_cast<ImGuiID>(dockspaceId);
		ImGuiID left = 0;
		ImGuiID leftBottom = 0;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.34f, &left, &centre);
		ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, &leftBottom, &left);
		dockWindow("Preview", left);
		dockWindow("Properties", leftBottom);
		dockWindow("Graph", centre);
	}

private:
	void drawPreview() {
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Style::kGray9);
		const float r = std::min(size.x, size.y) * 0.35f;
		draw->AddCircleFilled(ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f), r, Style::kAccent2);
		ImGui::Dummy(size);
	}

	void drawProperties() {
		if (ImGui::ColorEdit3("Albedo", m_albedo)) markDirty();
		if (ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f)) markDirty();
		if (curveEditor("falloff", m_falloff, -1.0f, 90.0f)) markDirty();
		ImGui::TextDisabled("Falloff f(0.5) = %.3f", m_falloff.evaluate(0.5f));
	}

	void drawGraph() {
		// Stand-in for the node graph that lands in P6 — enough to show a full-bleed window.
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Style::kGray8);
		for (float x = 0.0f; x < size.x; x += 24.0f) {
			draw->AddLine(ImVec2(p.x + x, p.y), ImVec2(p.x + x, p.y + size.y), Style::kGray7);
		}
		for (float y = 0.0f; y < size.y; y += 24.0f) {
			draw->AddLine(ImVec2(p.x, p.y + y), ImVec2(p.x + size.x, p.y + y), Style::kGray7);
		}
		const ImVec2 node(p.x + 40.0f, p.y + 40.0f);
		draw->AddRectFilled(node, ImVec2(node.x + 140.0f, node.y + 60.0f), Style::kGray5, 4.0f);
		draw->AddRect(node, ImVec2(node.x + 140.0f, node.y + 60.0f), Style::kAccent1, 4.0f);
		draw->AddText(ImVec2(node.x + 10.0f, node.y + 10.0f), Style::kText, "Texture Sample");
		ImGui::Dummy(size);
	}

	float m_albedo[3] = {0.82f, 0.66f, 0.31f};
	float m_metallic = 0.15f;
	Curve m_falloff;
};

} // namespace

std::unique_ptr<EditorTool> makeSceneDemoTool() { return std::make_unique<SceneDemoTool>(); }

std::unique_ptr<EditorTool> makeMaterialDemoTool(const char* documentPath) {
	auto tool = std::make_unique<MaterialDemoTool>();
	tool->setDocumentPath(documentPath);
	return tool;
}

} // namespace tucano::editor
