#pragma once

#include "Editor/EditorContext.h"
#include "Renderer/Scene.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace tucano::editor {

class OutlinerPanel {
public:
	void draw(EditorContext& ctx) {
		if (!ctx.scene) { ImGui::TextDisabled("No scene."); return; }

		ImGui::SetNextItemWidth(-1);
		m_filter.Draw("##OutlinerFilter", "Search...");

		ImGui::Separator();
		ImGui::BeginChild("OutlinerTree", ImVec2(0, 0), false);

		auto& objects = ctx.scene->objects;
		for (size_t i = 0; i < objects.size(); ++i) {
			const auto& obj = objects[i];
			if (m_filter.IsActive() && !m_filter.PassFilter(obj.name.c_str())) continue;

			const bool selected = (static_cast<int>(i) == ctx.selectedObject);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (selected) flags |= ImGuiTreeNodeFlags_Selected;
			if (!obj.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));

			ImGui::PushID(static_cast<int>(i));
			const bool opened = ImGui::TreeNodeEx(obj.name.empty() ? "Unnamed" : obj.name.c_str(), flags);
			if (ImGui::IsItemClicked()) { ctx.selectedObject = static_cast<int>(i); }
			if (ImGui::BeginPopupContextItem()) {
				ctx.selectedObject = static_cast<int>(i);
				if (ImGui::MenuItem("Focus")) {}
				if (ImGui::MenuItem("Duplicate")) {}
				if (ImGui::Selectable("Rename")) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Del")) {
					objects.erase(objects.begin() + i);
					if (ctx.selectedObject >= static_cast<int>(objects.size())) ctx.selectedObject = -1;
					ImGui::CloseCurrentPopup();
					if (!obj.visible) ImGui::PopStyleColor();
					ImGui::TreePop(); ImGui::PopID(); ImGui::EndChild(); return;
				}
				ImGui::EndPopup();
			}
			if (opened) ImGui::TreePop();
			if (!obj.visible) ImGui::PopStyleColor();
			ImGui::PopID();
		}
		if (objects.empty()) ImGui::TextDisabled("Scene is empty.");
		ImGui::EndChild();
	}

private:
	ImGuiTextFilter m_filter;
};

} // namespace tucano::editor
