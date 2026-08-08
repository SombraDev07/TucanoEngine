#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"

#include <glm/glm.hpp>

namespace tucano {

struct TUCANO_TYPE() WaterParams {
  TUCANO_FIELD(.label = "Enabled", .tooltip = "Draw the water pass at all", .category = "Surface")
  bool enabled = true;
  TUCANO_FIELD(.label = "Water level", .tooltip = "World height of the surface, in metres", .category = "Surface", .minValue = -200.0f, .maxValue = 200.0f, .step = 0.5f)
  float waterLevel = 0.0f;
  TUCANO_FIELD(.label = "Wave amplitude", .tooltip = "Height of the largest wave octave", .category = "Waves", .minValue = 0.0f, .maxValue = 5.0f, .step = 0.05f)
  float waveAmplitude = 0.8f;
  TUCANO_FIELD(.label = "Wave frequency", .tooltip = "Wavenumber of the first octave; higher is choppier", .category = "Waves", .minValue = 0.01f, .maxValue = 1.0f, .step = 0.01f)
  float waveFrequency = 0.12f;
  TUCANO_FIELD(.label = "Wave speed", .tooltip = "Multiplier on the deep-water phase speed", .category = "Waves", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float waveSpeed = 0.8f;
  TUCANO_FIELD(.label = "Choppiness", .tooltip = "Crest sharpening; also drives whitecap detection", .category = "Waves", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float waveChoppy = 0.5f;
  TUCANO_FIELD(.label = "Wave octaves", .tooltip = "How many directional waves are summed (max 12)", .category = "Waves", .minValue = 1.0f, .maxValue = 12.0f, .step = 1.0f)
  float waveCount = 8.0f;
  TUCANO_FIELD(.label = "Foam", .tooltip = "Whitecap and shoreline foam coverage", .category = "Surface", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float foamAmount = 0.6f;
  TUCANO_FIELD(.label = "Normal strength", .tooltip = "Scales the wave slope feeding the surface normal", .category = "Surface", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float normalStrength = 0.6f;
  float waterColorR = 0.03f;
  float waterColorG = 0.18f;
  float waterColorB = 0.38f;
  TUCANO_FIELD(.label = "Underwater fog", .tooltip = "Extinction seen from below the surface", .category = "Surface", .minValue = 0.0f, .maxValue = 0.2f, .step = 0.005f)
  float fogDensity = 0.015f;
  TUCANO_FIELD(.label = "Subsurface", .tooltip = "Glow through a thin, backlit crest", .category = "Surface", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float sssIntensity = 0.4f;
  TUCANO_FIELD(.label = "Shore hardness", .tooltip = "How quickly foam builds in shallow water", .category = "Surface", .minValue = 0.0f, .maxValue = 10.0f, .step = 0.1f)
  float shoreHardness = 2.0f;
  TUCANO_FIELD(.label = "Reflection", .tooltip = "Multiplier on the reflected environment", .category = "Reflection", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float reflectionStrength = 1.0f;
  TUCANO_FIELD(.label = "Wind direction", .tooltip = "XZ direction the wave field travels", .category = "Waves", .minValue = -1.0f, .maxValue = 1.0f)
  glm::vec2 windDirection{1.0f, 1.0f};

  /// Base microfacet roughness of the surface. Small values give a mirror; rain and wind raise it.
  TUCANO_FIELD(.label = "Roughness", .tooltip = "Microfacet roughness; small values give a mirror", .category = "Surface", .minValue = 0.0f, .maxValue = 0.5f, .step = 0.005f)
  float roughness = 0.045f;

  // ── Screen-space reflections ──
  /// Reflect scene geometry, not just the analytic sky. Off falls back to sky-only.
  TUCANO_FIELD(.label = "Screen-space reflections", .tooltip = "Off falls back to the environment probe alone", .category = "Reflection")
  bool enableSSR = true;
  /// Ray-march budget. 32 is enough at 1080p; raise for wide open reflections.
  TUCANO_FIELD(.label = "SSR steps", .tooltip = "Ray-march budget; 32 is enough at 1080p", .category = "Reflection", .minValue = 4.0f, .maxValue = 128.0f, .step = 1.0f)
  float ssrSteps = 32.0f;
  /// World-space length of the reflection ray before it gives up and returns sky.
  TUCANO_FIELD(.label = "SSR distance", .tooltip = "World length of the reflection ray", .category = "Reflection", .minValue = 10.0f, .maxValue = 1000.0f, .step = 10.0f)
  float ssrMaxDistance = 240.0f;
  /// How strongly a hit fades out near the screen edge, where there is no data to reflect.
  TUCANO_FIELD(.label = "SSR edge fade", .tooltip = "Fade near the screen border, where there is no data", .category = "Reflection", .minValue = 0.01f, .maxValue = 0.5f, .step = 0.01f)
  float ssrEdgeFade = 0.15f;

  // ── Refraction / absorption ──
  /// Screen-space UV displacement applied to the refracted view, in pixels at 1 m depth.
  TUCANO_FIELD(.label = "Refraction", .tooltip = "Screen-space distortion of the view through the surface", .category = "Reflection", .minValue = 0.0f, .maxValue = 0.2f, .step = 0.005f)
  float refractionStrength = 0.035f;
  /// Beer-Lambert extinction per metre of underwater path, per channel. Higher = murkier.
  TUCANO_FIELD(.label = "Absorption", .tooltip = "Beer-Lambert extinction per metre, per channel", .category = "Surface", .minValue = 0.0f, .maxValue = 2.0f)
  glm::vec3 absorption{0.28f, 0.075f, 0.045f};

  // ── Rain coupling (driven from RainParams by the renderer) ──
  /// 0 = dry. Fed from RainParams::amount when rain is enabled; see rainDrivesWater.
  TUCANO_FIELD(.label = "Rain intensity", .tooltip = "0 = dry. Overwritten each frame when rain drives water", .category = "Rain", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float rainIntensity = 0.0f;
  /// Height of the concentric impact ripples.
  TUCANO_FIELD(.label = "Ripple strength", .tooltip = "Height of the concentric impact ripples", .category = "Rain", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float rainRippleStrength = 0.55f;
  /// Impact cells per metre. One drop per cell per period, so this sets both ring size and
  /// density — 4 gives roughly 25 cm rings, which reads correctly for rain on open water.
  TUCANO_FIELD(.label = "Ripple density", .tooltip = "Impact cells per metre; 4 gives ~25 cm rings", .category = "Rain", .minValue = 0.5f, .maxValue = 12.0f, .step = 0.25f)
  float rainRippleScale = 4.0f;
  /// Impacts per second per cell.
  TUCANO_FIELD(.label = "Ripple speed", .tooltip = "Impacts per second per cell", .category = "Rain", .minValue = 0.0f, .maxValue = 8.0f, .step = 0.1f)
  float rainRippleSpeed = 2.2f;
  /// Extra surface roughness at full rain — this is what kills the mirror in a downpour.
  TUCANO_FIELD(.label = "Rain roughness", .tooltip = "Extra roughness at full rain; this is what breaks the mirror", .category = "Rain", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float rainRoughness = 0.22f;
  /// Extra foam/whitecap coverage at full rain.
  TUCANO_FIELD(.label = "Rain foam", .tooltip = "Extra foam coverage at full rain", .category = "Rain", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float rainFoam = 0.35f;

  /// Whether the renderer feeds RainParams::amount into rainIntensity each frame. Turn off to
  /// script the water's wetness independently of the weather system.
  TUCANO_FIELD(.label = "Rain drives water", .tooltip = "Feed RainParams amount into the surface automatically", .category = "Rain")
  bool rainDrivesWater = true;

  /// Detail wave octaves stop contributing past this distance, which is what stops the far
  /// field from shimmering into aliasing noise.
  TUCANO_FIELD(.label = "Detail fade", .tooltip = "Distance at which detail octaves stop, to stop aliasing", .category = "Surface", .minValue = 20.0f, .maxValue = 1000.0f, .step = 10.0f)
  float detailFadeDistance = 220.0f;
};

} // namespace tucano
