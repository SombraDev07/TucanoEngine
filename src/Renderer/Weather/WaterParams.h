#pragma once

#include <glm/glm.hpp>

namespace tucano {

struct WaterParams {
  bool enabled = true;
  float waterLevel = 0.0f;
  float waveAmplitude = 0.8f;
  float waveFrequency = 0.12f;
  float waveSpeed = 0.8f;
  float waveChoppy = 0.5f;
  float waveCount = 8.0f;
  float foamAmount = 0.6f;
  float normalStrength = 0.6f;
  float waterColorR = 0.03f;
  float waterColorG = 0.18f;
  float waterColorB = 0.38f;
  float fogDensity = 0.015f;
  float sssIntensity = 0.4f;
  float shoreHardness = 2.0f;
  float reflectionStrength = 1.0f;
  glm::vec2 windDirection{1.0f, 1.0f};

  /// Base microfacet roughness of the surface. Small values give a mirror; rain and wind raise it.
  float roughness = 0.045f;

  // ── Screen-space reflections ──
  /// Reflect scene geometry, not just the analytic sky. Off falls back to sky-only.
  bool enableSSR = true;
  /// Ray-march budget. 32 is enough at 1080p; raise for wide open reflections.
  float ssrSteps = 32.0f;
  /// World-space length of the reflection ray before it gives up and returns sky.
  float ssrMaxDistance = 240.0f;
  /// How strongly a hit fades out near the screen edge, where there is no data to reflect.
  float ssrEdgeFade = 0.15f;

  // ── Refraction / absorption ──
  /// Screen-space UV displacement applied to the refracted view, in pixels at 1 m depth.
  float refractionStrength = 0.035f;
  /// Beer-Lambert extinction per metre of underwater path, per channel. Higher = murkier.
  glm::vec3 absorption{0.28f, 0.075f, 0.045f};

  // ── Rain coupling (driven from RainParams by the renderer) ──
  /// 0 = dry. Fed from RainParams::amount when rain is enabled; see rainDrivesWater.
  float rainIntensity = 0.0f;
  /// Height of the concentric impact ripples.
  float rainRippleStrength = 0.55f;
  /// Impact cells per metre. One drop per cell per period, so this sets both ring size and
  /// density — 4 gives roughly 25 cm rings, which reads correctly for rain on open water.
  float rainRippleScale = 4.0f;
  /// Impacts per second per cell.
  float rainRippleSpeed = 2.2f;
  /// Extra surface roughness at full rain — this is what kills the mirror in a downpour.
  float rainRoughness = 0.22f;
  /// Extra foam/whitecap coverage at full rain.
  float rainFoam = 0.35f;

  /// Whether the renderer feeds RainParams::amount into rainIntensity each frame. Turn off to
  /// script the water's wetness independently of the weather system.
  bool rainDrivesWater = true;

  /// Detail wave octaves stop contributing past this distance, which is what stops the far
  /// field from shimmering into aliasing noise.
  float detailFadeDistance = 220.0f;
};

} // namespace tucano
