#pragma once

#include "Editor/EditorContext.h"
#include "Renderer/Scene.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tucano::editor {

class InspectorPanel {
public:
	void draw(EditorContext& ctx) {
		if (!ctx.scene) { ImGui::TextDisabled("No scene."); return; }
		auto& objects = ctx.scene->objects;
		if (ctx.selectedObject < 0 || static_cast<size_t>(ctx.selectedObject) >= objects.size()) {
			ImGui::TextDisabled("Select an object in the Outliner.");
			return;
		}

		auto& obj = objects[ctx.selectedObject];
		const char* name = obj.name.empty() ? "Unnamed" : obj.name.c_str();

		ImGui::TextUnformatted(name);
		ImGui::Separator();

		// ── Transform ──
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
			glm::vec3 pos = obj.transform.translation;
			if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.1f)) {
				obj.transform.translation = pos;
			}

			const float RAD2DEG = 180.0f / glm::pi<float>();
			glm::vec3 euler = glm::eulerAngles(obj.transform.rotation) * RAD2DEG;
			if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 0.5f)) {
				obj.transform.rotation = glm::quat(euler / RAD2DEG);
			}

			glm::vec3 scale = obj.transform.scale;
			if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.01f, 0.01f, 100.0f)) {
				obj.transform.scale = scale;
			}

			if (ImGui::Button("Reset")) {
				obj.transform.translation = glm::vec3(0);
				obj.transform.rotation = glm::quat(1, 0, 0, 0);
				obj.transform.scale = glm::vec3(1);
			}
		}

		// ── Material ──
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (!obj.materials.empty() && obj.materials[0]) {
				auto& mat = *obj.materials[0];
				if (ImGui::ColorEdit3("Base Color", glm::value_ptr(mat.baseColorFactor))) {}
				ImGui::SliderFloat("Metallic", &mat.metallicFactor, 0.0f, 1.0f);
				ImGui::SliderFloat("Roughness", &mat.roughnessFactor, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Clearcoat", &mat.clearcoat, 0.0f, 1.0f);
				ImGui::SliderFloat("Fuzz", &mat.fuzz, 0.0f, 1.0f);
				if (mat.emissiveFactor.r > 0 || mat.emissiveFactor.g > 0 || mat.emissiveFactor.b > 0) {
					ImGui::Text("Emissive: %.2f %.2f %.2f", mat.emissiveFactor.r, mat.emissiveFactor.g, mat.emissiveFactor.b);
				}
			} else {
				ImGui::TextDisabled("No material.");
			}
		}

		// ── Info ──
		ImGui::Separator();
		ImGui::Checkbox("Visible", &obj.visible);
		if (obj.mesh) {
			ImGui::TextDisabled("Mesh: %zu verts", obj.mesh->vertexCount());
		}
	}

private:
	glm::vec3 m_lastEuler{0};
};

} // namespace tucano::editor
