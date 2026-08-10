#pragma once

#include "Editor/EditorContext.h"
#include "Editor/PlayMode.h"

#include <imgui.h>

namespace tucano::editor {

class ToolsPanel {
public:
	enum class Tool : int {
		Select,
		Translate,
		Rotate,
		Scale,
	};

	void draw(EditorContext& ctx) {
		// ── Transform Tools ──
		ImGui::TextUnformatted("Transform");
		drawToolButton("Select",   Tool::Select,    "S");
		ImGui::SameLine();
		drawToolButton("Translate", Tool::Translate, "W");
		ImGui::SameLine();
		drawToolButton("Rotate",   Tool::Rotate,    "E");
		ImGui::SameLine();
		drawToolButton("Scale",    Tool::Scale,     "R");

		ImGui::Separator();

		// ── Gizmo settings ──
		ImGui::Checkbox("World Space", &m_worldSpace);
		ImGui::Checkbox("Snap", &m_snap);
		if (m_snap) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80);
			ImGui::DragFloat("##snapVal", &m_snapValue, 0.01f, 0.01f, 10.0f);
		}

		ImGui::Separator();

		// ── Tool launchers ──
		ImGui::TextUnformatted("Content Tools");
		if (ImGui::Button("Terrain Sculpt", ImVec2(-1, 0))) {
			m_showTerrain = !m_showTerrain;
		}
		if (ImGui::Button("Vegetation Paint", ImVec2(-1, 0))) {
			m_showVegetation = !m_showVegetation;
		}
		if (ImGui::Button("Material Editor", ImVec2(-1, 0))) {
			m_showMaterials = !m_showMaterials;
		}
		if (ImGui::Button("Animation Graph", ImVec2(-1, 0))) {
			m_showAnimGraph = !m_showAnimGraph;
		}

		ImGui::Separator();

		// ── Playback (I-01) ──
		ImGui::TextUnformatted("Playback");
		if (ctx.play == nullptr) {
			ImGui::TextDisabled("No play mode bound.");
		} else {
			PlayMode& play = *ctx.play;
			const bool running = play.isPlaying();

			ImGui::PushStyleColor(ImGuiCol_Button, running ? ImVec4(0.15f, 0.55f, 0.18f, 1)
			                                               : ImVec4(0.3f, 0.3f, 0.3f, 1));
			if (ImGui::Button(running ? "Stop##PlayBtn" : "Play##PlayBtn", ImVec2(-1, 0))) {
				if (running) {
					play.stop();
					ctx.logInfo("Play stopped — scene restored.");
				} else if (play.play()) {
					ctx.logInfo("Play started.");
				} else {
					ctx.logWarn("Could not start play: " + play.error());
				}
			}
			ImGui::PopStyleColor();

			// Pause only means something while running, and a button that does nothing is worse
			// than one that says it cannot.
			ImGui::BeginDisabled(!running);
			if (ImGui::Button(play.isPaused() ? "Resume##PauseBtn" : "Pause##PauseBtn",
			                  ImVec2(-1, 0))) {
				play.togglePause();
				ctx.logInfo(play.isPaused() ? "Paused." : "Resumed.");
			}
			ImGui::EndDisabled();

			if (running) {
				ImGui::TextDisabled("%s — %.1fs", play.isPaused() ? "paused" : "running",
				                    play.playTime());
			}
		}

		// Terrain tool placeholder
		if (m_showTerrain) {
			ImGui::SetNextWindowSize(ImVec2(340, 300), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Terrain Sculpt", &m_showTerrain)) {
				ImGui::TextUnformatted("Terrain sculpting tools coming soon.");
				ImGui::SliderFloat("Brush Radius", &m_brushRadius, 0.5f, 20.0f);
				ImGui::SliderFloat("Brush Strength", &m_brushStrength, 0.01f, 1.0f);
				const char* brushModes[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise"};
				ImGui::Combo("Mode", &m_brushMode, brushModes, IM_ARRAYSIZE(brushModes));
			}
			ImGui::End();
		}

		// Vegetation tool placeholder
		if (m_showVegetation) {
			ImGui::SetNextWindowSize(ImVec2(340, 300), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Vegetation Paint", &m_showVegetation)) {
				ImGui::TextUnformatted("Vegetation painting tools coming soon.");
				ImGui::SliderFloat("Density", &m_vegDensity, 0.0f, 1.0f);
				ImGui::SliderFloat("Brush Radius", &m_vegRadius, 0.5f, 30.0f);
				const char* paintModes[] = {"Place", "Erase", "Density"};
				ImGui::Combo("Mode", &m_vegPaintMode, paintModes, IM_ARRAYSIZE(paintModes));
			}
			ImGui::End();
		}

		// Material editor placeholder
		if (m_showMaterials) {
			ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Material Editor", &m_showMaterials)) {
				ImGui::TextUnformatted("Node-based material editor coming soon.");
				ImGui::TextWrapped("Will use the Esoterica node graph editor port (Phase 5).");
			}
			ImGui::End();
		}
	}

private:
	void drawToolButton(const char* label, Tool tool, const char* shortcut) {
		const bool active = (m_activeTool == tool);
		if (active) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 1));
		}
		if (ImGui::Button(label)) {
			m_activeTool = tool;
		}
		if (active) {
			ImGui::PopStyleColor();
		}
		// Show shortcut as tooltip
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s (%s)", label, shortcut);
		}
	}

	Tool m_activeTool = Tool::Select;
	bool m_worldSpace = true;
	bool m_snap = false;
	float m_snapValue = 0.25f;

	bool m_playing = false;
	bool m_paused = false;

	bool m_showTerrain = false;
	float m_brushRadius = 2.0f;
	float m_brushStrength = 0.3f;
	int m_brushMode = 0;

	bool m_showVegetation = false;
	float m_vegDensity = 0.5f;
	float m_vegRadius = 5.0f;
	int m_vegPaintMode = 0;

	bool m_showMaterials = false;
	bool m_showAnimGraph = false;
};

} // namespace tucano::editor
