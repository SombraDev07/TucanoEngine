#pragma once

// What a generated grid cannot express.
//
// This panel used to draw a widget per field by hand — around 100 of them — and that is exactly why
// 21 settings ended up unreachable from the editor: adding a field to RendererSettings did nothing
// until somebody remembered to add a slider here, and eventually nobody did. Sky and Rendering are
// now generated from reflection (D-01 / E-01) and cannot drift.
//
// What is left here is the residue that a property grid genuinely cannot do:
//   - **Presets** write several fields across three different structs at once.
//   - **The HDRI** has to re-cook the IBL and roll back when the file is unusable.
// Anything that grows past that belongs in the reflected struct instead — GI tier used to be listed
// here for want of enum reflection, and moved to the Rendering panel the moment that existed.

#include "Editor/EditorContext.h"
#include "Editor/UI/Pickers.h"
#include "Renderer/Renderer.h"
#include "Renderer/Weather/RainParams.h"

#include <imgui.h>

namespace tucano::editor {

class EnvironmentPanel {
public:
	void draw(EditorContext& ctx) {
		if (!ctx.renderer) {
			ImGui::TextDisabled("No renderer.");
			return;
		}
		auto& s = ctx.renderer->settings();
		auto& sky = ctx.renderer->sky();
		auto& rain = ctx.renderer->rain();

		ImGui::TextDisabled("Sky and Rendering have their own panels.");
		ImGui::Separator();

		// ── Time-of-day presets ──
		// Each one writes across sky, clouds and rain together, which is the point: "Storm" is not a
		// value of any single field. Hardcoded for now; E-03 turns them into assets.
		if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Button("Noon")) {
				sky.timeOfDay = 0.50f;
				sky.turbidity = 2.0f;
				sky.fogDensity = 0.005f;
				s.cloudCoverage = 0.15f;
				s.cloudStorminess = 0.05f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Sunset")) {
				sky.timeOfDay = 0.75f;
				sky.turbidity = 4.0f;
				sky.fogDensity = 0.008f;
				s.cloudCoverage = 0.30f;
				s.cloudStorminess = 0.05f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Midnight")) {
				sky.timeOfDay = 0.0f;
				sky.turbidity = 1.5f;
				sky.fogDensity = 0.002f;
				s.cloudCoverage = 0.10f;
				s.cloudStorminess = 0.05f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Storm")) {
				sky.timeOfDay = 0.40f;
				sky.turbidity = 6.0f;
				sky.fogDensity = 0.03f;
				s.cloudCoverage = 0.92f;
				s.cloudStorminess = 0.90f;
				rain.amount = 0.8f;
			}
		}

		// ── Environment lighting ──
		if (ImGui::CollapsingHeader("Environment lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
			// Picking an HDRI re-cooks the IBL immediately; reloadIBL keeps the previous environment
			// when the file cannot be used, so a bad pick costs nothing.
			m_hdriPicker.setRoot(TUCANO_ENGINE_ASSETS_DIR);
			m_hdriPicker.setKind(ui::AssetPicker::Kind::Hdri);
			if (m_hdriPicker.path() != s.hdriPath) m_hdriPicker.setPath(s.hdriPath);
			ImGui::TextUnformatted("HDRI");
			ImGui::SameLine();
			if (m_hdriPicker.draw("##hdri")) {
				const std::string previous = s.hdriPath;
				if (ctx.renderer->reloadIBL(m_hdriPicker.path())) {
					s.hdriPath = m_hdriPicker.path();
					ctx.logInfo("IBL reloaded from " + s.hdriPath);
				} else {
					m_hdriPicker.setPath(previous);
					ctx.logWarn("Could not load HDRI: " + m_hdriPicker.path());
				}
			}
		}
	}

private:
	ui::AssetPicker m_hdriPicker;
};

} // namespace tucano::editor
