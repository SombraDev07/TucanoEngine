#pragma once

#include "Editor/EditorContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/Weather/RainParams.h"
#include "Renderer/Weather/WaterParams.h"
#include "Renderer/Weather/FogParams.h"

#include <imgui.h>

namespace tucano::editor {

class EnvironmentPanel {
public:
	void draw(EditorContext& ctx) {
		if (!ctx.renderer) { ImGui::TextDisabled("No renderer."); return; }
		auto& s = ctx.renderer->settings();
		auto& rain = ctx.renderer->rain();
		auto& water = ctx.renderer->water();
		auto& fog = ctx.renderer->fog();

		// ── Time-of-day presets ──
		if (ImGui::Button("Noon"))    { s.timeOfDay = 0.50f; s.turbidity = 2.0f; s.cloudCoverage = 0.15f; s.cloudStorminess = 0.05f; s.fogDensity = 0.005f; }
		ImGui::SameLine();
		if (ImGui::Button("Sunset"))  { s.timeOfDay = 0.75f; s.turbidity = 4.0f; s.cloudCoverage = 0.30f; s.cloudStorminess = 0.05f; s.fogDensity = 0.008f; }
		ImGui::SameLine();
		if (ImGui::Button("Midnight")){ s.timeOfDay = 0.0f; s.turbidity = 1.5f; s.cloudCoverage = 0.10f; s.cloudStorminess = 0.05f; s.fogDensity = 0.002f; }
		ImGui::SameLine();
		if (ImGui::Button("Storm"))   { s.timeOfDay = 0.40f; s.turbidity = 6.0f; s.cloudCoverage = 0.92f; s.cloudStorminess = 0.90f; s.fogDensity = 0.03f; rain.amount = 0.8f; }
		ImGui::Separator();

		// ── Atmosphere ──
		if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enabled", &s.enableAtmosphere);
			ImGui::Checkbox("Bruneton", &s.useBrunetonAtmosphere);
			ImGui::SliderFloat("Time of Day", &s.timeOfDay, 0.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Turbidity", &s.turbidity, 1.0f, 10.0f);
			ImGui::SliderFloat("Latitude", &s.latitudeDeg, -90.0f, 90.0f);
		}

		// ── Clouds ──
		if (ImGui::CollapsingHeader("Clouds", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enabled", &s.enableClouds);
			ImGui::SliderFloat("Coverage", &s.cloudCoverage, 0.0f, 1.0f);
			ImGui::SliderFloat("Density", &s.cloudDensity, 0.1f, 3.0f);
			ImGui::SliderFloat("Altitude", &s.cloudAltitude, 500.0f, 5000.0f);
			ImGui::SliderFloat("Thickness", &s.cloudThickness, 200.0f, 3000.0f);
			ImGui::SliderFloat("Storminess", &s.cloudStorminess, 0.0f, 1.0f);
			ImGui::Checkbox("Shadows", &s.enableCloudShadows);
			if (s.enableCloudShadows) ImGui::SliderFloat("Shadow Str", &s.cloudShadowStrength, 0.0f, 1.0f);
			ImGui::Checkbox("God Rays", &s.enableCloudGodRays);
			if (s.enableCloudGodRays) ImGui::SliderFloat("GodRay Str", &s.cloudGodRayStrength, 0.0f, 1.0f);
		}

		// ── Fog ──
		if (ImGui::CollapsingHeader("Fog")) {
			ImGui::Checkbox("Volumetric", &fog.volumetric);
			ImGui::SliderFloat("Height Density", &s.fogDensity, 0.0f, 0.1f, "%.4f");
			ImGui::SliderFloat("Height", &s.fogHeight, 0.0f, 200.0f);
			ImGui::SliderFloat("Froxel Density", &fog.density, 0.0f, 0.1f, "%.4f");
		}

		// ── Water ──
		if (ImGui::CollapsingHeader("Water")) {
			ImGui::Checkbox("Enabled", &water.enabled);
			ImGui::SliderFloat("Water Level", &water.waterLevel, -10.0f, 30.0f);
			ImGui::SliderFloat("Roughness", &water.roughness, 0.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Wave Ampl.", &water.waveAmplitude, 0.0f, 3.0f);
			ImGui::SliderFloat("Foam", &water.foamAmount, 0.0f, 1.0f);
			ImGui::Checkbox("SSR", &water.enableSSR);
		}

		// ── Rain ──
		if (ImGui::CollapsingHeader("Rain")) {
			ImGui::Checkbox("Enabled", &rain.enabled);
			ImGui::SliderFloat("Amount", &rain.amount, 0.0f, 1.0f);
			ImGui::SliderFloat("Streak Int.", &rain.streakIntensity, 0.0f, 2.0f);
			ImGui::SliderFloat("Puddles", &rain.puddlesAmount, 0.0f, 2.0f);
			ImGui::SliderFloat("Splashes", &rain.splashesAmount, 0.0f, 2.0f);
			ImGui::SliderFloat("Lens Drops", &rain.rainDropsAmount, 0.0f, 1.0f);
			ImGui::SliderFloat("Mist", &rain.mistAmount, 0.0f, 1.0f);
			ImGui::Checkbox("Scene Rain", &rain.enableSceneRain);
			ImGui::Checkbox("Splashes 3D", &rain.enableWorldSplashes);
		}

		// ── Post Effects ──
		if (ImGui::CollapsingHeader("Post Effects")) {
			ImGui::Checkbox("Bloom", &s.enableBloom);
			if (s.enableBloom) ImGui::SliderFloat("Strength", &s.bloomStrength, 0.0f, 2.0f);
			ImGui::Checkbox("AO (GTAO)", &s.enableAO);
			if (s.enableAO) {
				ImGui::SliderFloat("Radius", &s.aoRadius, 0.1f, 4.0f);
				ImGui::SliderFloat("Intensity", &s.aoIntensity, 0.0f, 3.0f);
			}
			ImGui::Checkbox("Auto Exposure", &s.enableAutoExposure);
			ImGui::SliderFloat("Exposure Target", &s.exposureTarget, 0.05f, 1.0f, "%.2f");
			ImGui::SliderFloat("Exposure Adapt", &s.exposureAdapt, 0.01f, 1.0f, "%.2f");
			ImGui::Checkbox("Tonemap", &s.enableTonemap);
		}

		// ── Shadows ──
		if (ImGui::CollapsingHeader("Shadows")) {
			ImGui::Checkbox("Shadows", &s.enableShadows);
			ImGui::Checkbox("PCSS Soft", &s.enablePCSS);
			ImGui::Checkbox("ESM", &s.enableESM);
			ImGui::Checkbox("VSM", &s.enableVSM);
			ImGui::Checkbox("Toroidal CSM", &s.enableToroidalShadows);
			ImGui::Checkbox("Octahedral Pts", &s.enableOctahedralPointShadows);
			ImGui::Checkbox("Contact", &s.enableContactShadows);
		}

		// ── GI ──
		if (ImGui::CollapsingHeader("Global Illumination")) {
			ImGui::Checkbox("IBL", &s.enableIBL);
			ImGui::Checkbox("SSR", &s.enableSSR);
			ImGui::Checkbox("Voxel GI", &s.enableVoxelGI);
			ImGui::Checkbox("RT Reflections", &s.enableRTReflections);
			ImGui::Checkbox("RT Shadows", &s.enableRTShadows);
			ImGui::Checkbox("Async Compute", &s.enableAsyncCompute);
		}

		// ── Night Sky ──
		if (ImGui::CollapsingHeader("Night Sky")) {
			ImGui::Checkbox("Moon", &s.enableMoon);
			ImGui::SliderFloat("Moon Int.", &s.moonIntensity, 0.0f, 0.5f, "%.3f");
			ImGui::Checkbox("Stars", &s.enableStars);
			ImGui::SliderFloat("Star Int.", &s.starIntensity, 0.0f, 3.0f);
			ImGui::SliderFloat("Twinkle", &s.starTwinkle, 0.0f, 1.0f);
			ImGui::SliderFloat("Purkinje", &s.purkinjeStrength, 0.0f, 1.0f);
		}
	}
};

} // namespace tucano::editor
