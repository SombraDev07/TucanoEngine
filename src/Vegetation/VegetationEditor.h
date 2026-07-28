#pragma once

#include "Vegetation/VegetationSystem.h"
#include "Vegetation/LODManager.h"
#include "Vegetation/DensityMap.h"
#include "Vegetation/BiomeSystem.h"
#include "Vegetation/SeasonSystem.h"
#include "Vegetation/WindSystem.h"
#include "Vegetation/GrowthSystem.h"
#include "Vegetation/VegetationInteraction.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace tucano::veg {

class VegetationEditor {
public:
	static VegetationEditor& instance() { static VegetationEditor ve; return ve; }

	void drawUI() {
		if (!m_open) return;

		ImGui::SetNextWindowSize(ImVec2(500, 700), ImGuiCond_FirstUseEver);
		ImGui::Begin("Vegetation Editor", &m_open);

		if (ImGui::BeginTabBar("VegTabs")) {
			if (ImGui::BeginTabItem("Types"))     { drawTypeBrowser(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Biomes"))    { drawBiomeEditor(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Paint"))     { drawPaintTool(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Season"))    { drawSeasonEditor(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Presets"))   { drawPresets(); ImGui::EndTabItem(); }
			if (ImGui::BeginTabItem("Debug"))     { drawDebugOverlay(); ImGui::EndTabItem(); }
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void toggle() { m_open = !m_open; }
	void open() { m_open = true; }
	void close() { m_open = false; }
	bool isOpen() const { return m_open; }

private:
	void drawTypeBrowser() {
		auto& sys = VegetationSystem::instance();
		static int selectedType = 0;
		static char nameBuf[64] = "NewPlant";
		static int previewLOD = 0;

		ImGui::Text("Vegetation Types: %u", sys.typeCount());

		if (ImGui::Button("+ Grass")) {
			VegetationType t;
			t.name = "Grass";
			t.proceduralKind = 1;
			t.minScale = 0.4f;
			t.maxScale = 0.9f;
			t.windFlexibility = 1.4f;
			t.windHeight = 0.55f;
			sys.registerType(t);
			selectedType = int(sys.typeCount()) - 1;
			strncpy_s(nameBuf, t.name.c_str(), sizeof(nameBuf));
			m_meshBuf[0] = '\0';
		}
		ImGui::SameLine();
		if (ImGui::Button("+ Plant / Leaf")) {
			VegetationType t;
			t.name = nameBuf[0] ? nameBuf : "Plant";
			t.proceduralKind = 2;
			t.minScale = 0.6f;
			t.maxScale = 1.4f;
			t.windFlexibility = 0.4f;
			sys.registerType(t);
			selectedType = int(sys.typeCount()) - 1;
		}
		ImGui::SameLine();
		if (ImGui::Button("+ From Mesh")) {
			VegetationType t;
			t.name = nameBuf[0] ? nameBuf : "CustomMesh";
			t.meshPath = m_meshBuf;
			t.proceduralKind = 2;
			sys.registerType(t);
			selectedType = int(sys.typeCount()) - 1;
			sys.paint().meshDirty = true;
			sys.paint().typeId = selectedType;
		}

		if (sys.typeCount() == 0) {
			ImGui::TextWrapped("Add a Grass, Plant, or mesh-based type, then paint in the Paint tab.");
			return;
		}

		ImGui::Separator();
		ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
		if (selectedType < int(sys.typeCount())) {
			auto* t = sys.typeMutable(uint32_t(selectedType));
			if (t) {
				t->name = nameBuf;
				ImGui::InputText("Mesh Path (gltf/glb)", m_meshBuf, sizeof(m_meshBuf));
				t->meshPath = m_meshBuf;
				if (ImGui::Button("Load Mesh")) {
					sys.paint().meshDirty = true;
					sys.paint().typeId = selectedType;
				}
				ImGui::SameLine();
				ImGui::TextDisabled("empty = procedural fallback");

				const char* kinds[] = {"Auto", "Grass cards", "Plant"};
				int kind = int(t->proceduralKind);
				if (ImGui::Combo("Procedural Fallback", &kind, kinds, 3))
					t->proceduralKind = uint32_t(kind);

				ImGui::SliderFloat("Min Scale", &t->minScale, 0.1f, 5.0f);
				ImGui::SliderFloat("Max Scale", &t->maxScale, 0.1f, 5.0f);
				ImGui::SliderFloat("Wind Flex", &t->windFlexibility, 0.0f, 2.0f);
				ImGui::SliderFloat("Wind Height", &t->windHeight, 0.1f, 10.0f);
				ImGui::Separator();
				ImGui::Text("LOD Distances");
				ImGui::SliderFloat("LOD0 (Full)", &t->lodDistance0, 5.0f, 200.0f);
				ImGui::SliderFloat("LOD1 (Simp)", &t->lodDistance1, 10.0f, 500.0f);
				ImGui::SliderFloat("LOD2 (Bill)", &t->lodDistance2, 20.0f, 1000.0f);
				ImGui::SliderFloat("Cull", &t->cullDistance, 30.0f, 2000.0f);
				ImGui::Separator();
				ImGui::Checkbox("Cast Shadows", &t->castShadows);
				ImGui::Checkbox("Receive Decals", &t->receiveDecals);
				ImGui::Separator();
				ImGui::Text("Preview LOD: %d", previewLOD);
				ImGui::SliderInt("LOD Level", &previewLOD, 0, 2);
			}
		}

		ImGui::Separator();
		ImGui::Text("Type List:");
		for (uint32_t i = 0; i < sys.typeCount(); ++i) {
			auto* t = sys.type(i);
			if (!t) continue;
			ImGui::PushID(int(i));
			char label[128];
			snprintf(label, sizeof(label), "%u: %s%s", i, t->name.c_str(),
			         t->meshPath.empty() ? "" : " [mesh]");
			if (ImGui::Selectable(label, int(i) == selectedType)) {
				selectedType = int(i);
				strncpy_s(nameBuf, t->name.c_str(), sizeof(nameBuf));
				strncpy_s(m_meshBuf, t->meshPath.c_str(), sizeof(m_meshBuf));
			}
			ImGui::PopID();
		}
	}

	void drawBiomeEditor() {
		auto& biome = BiomeSystem::instance();
		static char biomeName[64] = "";
		static float priority = 0;
		static float density = 1.0f;
		static float clusterSize = 0;

		ImGui::Text("Biome Layers: %zu", biome.layerCount());

		ImGui::InputText("Layer Name", biomeName, sizeof(biomeName));
		ImGui::InputFloat("Priority", &priority);
		ImGui::InputFloat("Density", &density);
		ImGui::InputFloat("Cluster Size", &clusterSize);

		if (ImGui::Button("Add Biome Layer") && strlen(biomeName) > 0) {
			BiomeLayer layer;
			layer.name = biomeName;
			layer.priority = priority;
			layer.density = density;
			layer.clusterSize = clusterSize;
			biome.addLayer(layer);
		}

		ImGui::Separator();

		for (size_t i = 0; i < biome.layerCount(); ++i) {
			auto* layer = const_cast<BiomeLayer*>(&biome.layers()[i]);
			ImGui::PushID(int(i));
			ImGui::Text("%s (pri=%.1f, dens=%.2f)", layer->name.c_str(), layer->priority, layer->density);

			if (ImGui::Button("Add Rule")) {
				BiomeRule rule;
				rule.condition = BiomeRule::AlwaysTrue;
				rule.weight = 1.0f;
				layer->rules.push_back(rule);
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove")) {
				biome.removeLayer(layer->name);
				ImGui::PopID();
				continue;
			}

			for (size_t j = 0; j < layer->rules.size(); ++j) {
				auto& rule = layer->rules[j];
				ImGui::PushID(int(j));
				const char* conditions[] = {"Slope<", "Slope>", "Slope<>", "Alt<", "Alt>", "Alt<>", "Water<", "Water>", "Sun<", "Sun>", "Always"};
				int cond = int(rule.condition);
				ImGui::Combo("Cond", &cond, conditions, 11);
				rule.condition = BiomeRule::Condition(cond);
				if (rule.condition != BiomeRule::AlwaysTrue) {
					ImGui::SameLine();
					ImGui::InputFloat("ValA", &rule.valueA);
					if (rule.condition == BiomeRule::SlopeBetween || rule.condition == BiomeRule::AltitudeBetween)
						ImGui::InputFloat("ValB", &rule.valueB);
				}
				ImGui::SameLine();
				ImGui::InputFloat("Weight", &rule.weight);
				ImGui::PopID();
			}
			ImGui::PopID();
		}
	}

	void drawPaintTool() {
		auto& sys = VegetationSystem::instance();
		auto& paint = sys.paint();

		ImGui::Checkbox("Enable Viewport Paint", &paint.enabled);
		ImGui::TextWrapped("LMB paints/places while enabled. Assign a mesh in Types or use procedural Grass/Plant.");

		ImGui::SliderFloat("Brush Radius", &paint.brushRadius, 0.5f, 50.0f);
		ImGui::SliderFloat("Strength", &paint.brushStrength, 0.0f, 1.0f);
		ImGui::SliderFloat("Plants / m2", &paint.plantsPerSquareMeter, 0.1f, 20.0f);

		const char* modes[] = {"Paint Plants", "Erase Plants", "Place Single", "Paint Density", "Erase Density"};
		int mode = int(paint.mode);
		if (ImGui::Combo("Mode", &mode, modes, 5))
			paint.mode = VegetationPaintState::Mode(mode);

		if (sys.typeCount() > 0) {
			std::vector<const char*> names;
			names.reserve(sys.typeCount());
			for (uint32_t i = 0; i < sys.typeCount(); ++i)
				names.push_back(sys.type(i)->name.c_str());
			ImGui::Combo("Paint Type", &paint.typeId, names.data(), int(names.size()));
			if (paint.typeId >= int(sys.typeCount())) paint.typeId = 0;
			auto* t = sys.type(uint32_t(paint.typeId));
			if (t) ImGui::Text("Mesh: %s", t->meshPath.empty() ? "(procedural)" : t->meshPath.c_str());
		} else {
			ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "No types — add one in the Types tab.");
		}

		auto* map = sys.densityMap();
		if (!map) {
			if (ImGui::Button("Create 512x512 Density Map")) {
				auto dm = std::make_shared<DensityMap>();
				dm->create(512, 512, 1.0f);
				dm->setWorldBounds({-256, -256}, {512, 512});
				sys.setDensityMap(dm);
			}
		} else {
			ImGui::Text("Density map: %ux%u", map->width(), map->height());
		}

		ImGui::Separator();
		ImGui::Text("Exclusion Zones: %zu",
		            sys.exclusionZones() ? sys.exclusionZones()->zoneCount() : 0);
		if (ImGui::Button("+ Rect Zone")) {
			if (!sys.exclusionZones())
				sys.setExclusionZones(std::make_shared<ExclusionZone>());
			sys.exclusionZones()->addRectangle({0, 0}, {10, 10});
		}
	}

	void drawSeasonEditor() {
		auto& season = SeasonSystem::instance();
		auto& cfg = season.config();

		int s = int(season.currentSeason());
		const char* seasons[] = {"Spring", "Summer", "Autumn", "Winter"};
		ImGui::Combo("Season", &s, seasons, 4);
		cfg.currentSeason = Season(s);

		ImGui::SliderFloat("Day of Year", &cfg.dayOfYear, 0, cfg.yearLength);
		ImGui::SliderFloat("Year Length", &cfg.yearLength, 60.0f, 720.0f);
		ImGui::SliderFloat("Transition Duration", &cfg.transitionDuration, 1.0f, 60.0f);
		ImGui::SliderFloat("Time Scale", &cfg.timeScale, 0.0f, 10.0f);
		ImGui::Checkbox("Auto Advance", &cfg.autoAdvance);

		ImGui::Text("Season Colors:");
		for (int se = 0; se < 4; ++se) {
			auto colors = season.getColors(Season(se), 0);
			ImGui::PushID(se);
			ImGui::Text("%s", seasons[se]);
			ImGui::ColorEdit3("Base", &colors.baseColor.x);
			ImGui::ColorEdit3("Tip", &colors.tipColor.x);
			ImGui::SliderFloat("Variation", &colors.colorVariation, 0, 1);
			ImGui::SliderFloat("Leaf Drop", &colors.leafDrop, 0, 1);
			ImGui::SliderFloat("Snow", &colors.snowAmount, 0, 1);
			ImGui::PopID();
		}

		ImGui::Separator();
		auto& growth = GrowthSystem::instance();
		auto& gcfg = growth.config();
		ImGui::Checkbox("Growth Enabled", &gcfg.enabled);
		ImGui::SliderFloat("Growth Rate", &gcfg.growthRate, 0.01f, 1.0f);
		ImGui::SliderFloat("Min Scale", &gcfg.minScale, 0.05f, 0.5f);
		ImGui::SliderFloat("Max Growth", &gcfg.maxGrowth, 0.5f, 3.0f);
	}

	void drawPresets() {
		static char presetName[64] = "MyPreset";
		static std::vector<std::string> savedPresets;

		ImGui::InputText("Preset Name", presetName, sizeof(presetName));

		if (ImGui::Button("Save Preset")) {
			std::string path = "Presets/" + std::string(presetName) + ".vegpreset";
			savePreset(path);
			savedPresets.push_back(presetName);
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			savedPresets.clear();
			savedPresets.push_back("Forest_Default");
			savedPresets.push_back("Plains_Light");
			savedPresets.push_back("Swamp_Dense");
			savedPresets.push_back("Mountain_Sparse");
		}

		ImGui::Separator();
		for (auto& name : savedPresets) {
			ImGui::PushID(name.c_str());
			if (ImGui::Button("Load")) {
				std::string path = "Presets/" + name + ".vegpreset";
				loadPreset(path);
			}
			ImGui::SameLine();
			ImGui::Text("%s", name.c_str());
			ImGui::PopID();
		}
	}

	void drawDebugOverlay() {
		auto& sys = VegetationSystem::instance();
		auto& lod = LODManager::instance();
		auto& wind = WindSystem::instance();

		ImGui::Text("=== Vegetation Debug ===");
		ImGui::Separator();
		ImGui::Text("Instances: %u", sys.instanceCount());
		ImGui::Text("Types: %u", sys.typeCount());
		ImGui::Text("Cells: %zu", sys.cellCount());

		ImGui::Separator();
		ImGui::Text("=== LOD Stats ===");
		ImGui::Text("Density Scale: %.2f", lod.config().globalDensityScale);
		ImGui::Text("CrossFade: %s", lod.config().enableDitherFade ? "ON" : "OFF");
		ImGui::Text("Density Scaling: %s", lod.config().enableDensityScaling ? "ON" : "OFF");

		ImGui::Separator();
		ImGui::Text("=== Wind ===");
		ImGui::Text("Strength: %.2f", wind.effectiveStrength());
		ImGui::Text("Dynamic Events: %zu", wind.dynamicEventCount());

		ImGui::Separator();
		ImGui::Text("=== Interaction ===");
		ImGui::Text("Growth Instances: %zu", GrowthSystem::instance().instanceCount());
		ImGui::Text("Destroyed: %s", DestructionSystem::instance().hasDestroyed() ? "YES" : "no");

		static bool heatmapMode = false;
		static int heatmapType = 0;
		ImGui::Checkbox("Show Heatmap", &heatmapMode);
		if (heatmapMode) {
			const char* modes[] = {"LOD Level", "Density", "Instance Count", "Wind Flex"};
			ImGui::Combo("Mode", &heatmapType, modes, 4);
		}
	}

	bool savePreset(const std::string& path) {
		std::ofstream f(path);
		if (!f) return false;

		auto& sys = VegetationSystem::instance();
		f << "{\n";
		f << "  \"types\": [\n";
		for (uint32_t i = 0; i < sys.typeCount(); ++i) {
			auto* t = sys.type(i);
			if (!t) continue;
			f << "    {\"name\":\"" << t->name << "\","
			  << "\"minScale\":" << t->minScale << ","
			  << "\"maxScale\":" << t->maxScale << ","
			  << "\"windFlex\":" << t->windFlexibility << ","
			  << "\"windHeight\":" << t->windHeight << ","
			  << "\"cullDist\":" << t->cullDistance << "}";
			if (i < sys.typeCount() - 1) f << ",";
			f << "\n";
		}
		f << "  ],\n";
		f << "  \"densityScale\": " << LODManager::instance().config().globalDensityScale << ",\n";
		f << "  \"windStrength\": " << WindSystem::instance().params().strength << "\n";
		f << "}\n";
		return true;
	}

	bool loadPreset(const std::string& path) {
		std::ifstream f(path);
		if (!f) return false;

		std::stringstream buf;
		buf << f.rdbuf();
		(void)buf.str();

		VegetationSystem::instance().clear();

		VegetationType t;
		t.name = "PresetGrass";
		t.minScale = 0.3f;
		t.maxScale = 0.8f;
		t.windFlexibility = 1.0f;
		t.proceduralKind = 1;
		VegetationSystem::instance().registerType(t);

		return true;
	}

	bool m_open = false;
	char m_meshBuf[256] = "";
};

} // namespace tucano::veg
