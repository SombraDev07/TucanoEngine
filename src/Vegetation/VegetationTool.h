#pragma once

#include "Vegetation/VegetationSystem.h"
#include "Vegetation/WindSystem.h"
#include "Vegetation/VegetationEditor.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <string>
#include <vector>

namespace tucano::veg {

class VegetationTool {
public:
	static VegetationTool& instance() { static VegetationTool vt; return vt; }

	void drawUI() {
		if (!m_open) return;

		if (m_fullEditor) {
			VegetationEditor::instance().drawUI();
			return;
		}

		ImGui::Begin("Vegetation Tool", &m_open);
		if (ImGui::Button("Open Full Editor")) {
			VegetationEditor::instance().open();
			m_fullEditor = true;
		}
		drawTypeEditor();
		ImGui::Separator();
		drawPlacementTool();
		ImGui::Separator();
		drawWindEditor();
		ImGui::Separator();
		drawStats();
		ImGui::End();
	}

	void toggle() { m_open = !m_open; if (m_open && m_fullEditor) VegetationEditor::instance().open(); }
	void open() { m_open = true; }
	void close() { m_open = false; if (m_fullEditor) VegetationEditor::instance().close(); }
	bool isOpen() const { return m_open; }

private:
	void drawTypeEditor() {
		if (ImGui::CollapsingHeader("Vegetation Types", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& sys = VegetationSystem::instance();

			for (uint32_t i = 0; i < sys.typeCount(); ++i) {
				auto* t = sys.typeMutable(i);
				if (!t) continue;

				ImGui::PushID(int(i));
				if (ImGui::TreeNode(t->name.c_str())) {
					ImGui::InputText("Mesh Path", m_meshPath, sizeof(m_meshPath));
					if (ImGui::Button("Apply Mesh Path")) {
						t->meshPath = m_meshPath;
						sys.paint().meshDirty = true;
						sys.paint().typeId = int(i);
					}
					ImGui::InputFloat("Min Scale", &t->minScale, 0.1f);
					ImGui::InputFloat("Max Scale", &t->maxScale, 0.1f);
					ImGui::SliderFloat("Wind Flex", &t->windFlexibility, 0.0f, 2.0f);
					ImGui::InputFloat("Cull Dist", &t->cullDistance, 10.0f);
					ImGui::InputFloat("Shadow Dist", &t->shadowDistance, 10.0f);
					ImGui::InputFloat("LOD0 Dist", &t->lodDistance0, 5.0f);
					ImGui::InputFloat("LOD1 Dist", &t->lodDistance1, 10.0f);
					ImGui::InputFloat("LOD2 Dist", &t->lodDistance2, 10.0f);
					ImGui::Checkbox("Cast Shadows", &t->castShadows);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (ImGui::Button("Add Grass")) {
				VegetationType t;
				t.name = "Grass_" + std::to_string(sys.typeCount());
				t.proceduralKind = 1;
				sys.registerType(t);
			}
			ImGui::SameLine();
			if (ImGui::Button("Add Plant")) {
				VegetationType t;
				t.name = "Plant_" + std::to_string(sys.typeCount());
				t.proceduralKind = 2;
				sys.registerType(t);
			}
		}
	}

	void drawPlacementTool() {
		if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& sys = VegetationSystem::instance();

			ImGui::SliderInt("Cell X", &m_cellX, -10, 10);
			ImGui::SliderInt("Cell Z", &m_cellZ, -10, 10);

			static int selectedType = 0;
			if (sys.typeCount() > 0) {
				std::vector<const char*> names;
				for (uint32_t i = 0; i < sys.typeCount(); ++i)
					names.push_back(sys.type(i)->name.c_str());

				if (ImGui::Combo("Type", &selectedType, names.data(), int(names.size()))) {}
			}

			ImGui::InputInt("Count", &m_scatterCount);
			if (m_scatterCount < 1) m_scatterCount = 1;
			if (m_scatterCount > 100000) m_scatterCount = 100000;

			ImGui::InputInt("Seed", &m_scatterSeed);

			if (ImGui::Button("Scatter")) {
				sys.scatter(m_cellX, m_cellZ, selectedType, m_scatterCount, m_scatterSeed);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear Cell")) {
				sys.removeInstances(m_cellX, m_cellZ);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear All")) {
				sys.clear();
			}
		}
	}

	void drawWindEditor() {
		if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& wind = WindSystem::instance();
			auto& params = wind.params();

			ImGui::SliderFloat("Strength", &params.strength, 0.0f, 3.0f);
			ImGui::SliderFloat("Speed", &params.speed, 0.0f, 2.0f);
			ImGui::SliderFloat("Gust Freq", &params.gustFrequency, 0.0f, 1.0f);
			ImGui::SliderFloat("Gust Strength", &params.gustStrength, 0.0f, 5.0f);
			ImGui::SliderFloat("Turbulence", &params.turbulence, 0.0f, 1.0f);
			ImGui::SliderFloat3("Direction", &params.direction.x, -1.0f, 1.0f);

			auto& cfg = VegetationSystem::instance().config();
			ImGui::Checkbox("Enable Wind", &cfg.enableWind);
			ImGui::Checkbox("GPU Wind", &cfg.enableGPUWind);
			ImGui::SliderFloat("Density Scale", &cfg.densityScale, 0.0f, 3.0f);
		}
	}

	void drawStats() {
		if (ImGui::CollapsingHeader("Stats")) {
			auto& sys = VegetationSystem::instance();
			ImGui::Text("Types: %u", sys.typeCount());
			ImGui::Text("Cells: %zu", sys.cellCount());
			ImGui::Text("Instances: %u", sys.instanceCount());
			ImGui::Text("Wind: %.2f", WindSystem::instance().effectiveStrength());
		}
	}

	bool m_open = false;
	int m_cellX = 0, m_cellZ = 0;
	int m_scatterCount = 100;
	int m_scatterSeed = 42;
	bool m_fullEditor = false;
	char m_meshPath[256] = "";
};

} // namespace tucano::veg
