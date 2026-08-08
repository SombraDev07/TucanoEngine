#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"

#include <glm/glm.hpp>

namespace tucano {

/// Froxel volumetric fog.
///
/// The analytic height fog it replaces could only tint a pixel by its distance: fog was equally
/// bright in shadow and in sun, identical looking toward the sun and away from it, and no light
/// ever shafted through it. This is the participating-media model instead — density and lighting
/// are evaluated in a camera-fitted 3D grid, so the fog is shadowed, scatters anisotropically
/// toward the sun, and produces light shafts for free.
struct TUCANO_TYPE() FogParams {
  TUCANO_FIELD(.label = "Enabled", .tooltip = "Master switch for both fog paths", .category = "Medium")
  bool enabled = true;

  /// Falls back to the analytic height fog in the lighting pass. Cheaper, and the only option
  /// when the froxel volume cannot be created.
  TUCANO_FIELD(.label = "Volumetric", .tooltip = "Off falls back to the cheap analytic height fog", .category = "Medium")
  bool volumetric = true;

  // ── Medium ──
  /// Extinction at the base height, per metre. This is the master density knob.
  TUCANO_FIELD(.label = "Density", .tooltip = "Extinction at the base height, per metre", .category = "Medium", .minValue = 0.0f, .maxValue = 0.2f, .step = 0.002f)
  float density = 0.02f;
  /// Height at which density starts falling off.
  TUCANO_FIELD(.label = "Base height", .tooltip = "Height at which density starts falling off", .category = "Medium", .minValue = -200.0f, .maxValue = 500.0f, .step = 1.0f)
  float baseHeight = 0.0f;
  /// e-folding distance of the vertical falloff, in metres. Large values give a uniform medium.
  TUCANO_FIELD(.label = "Height falloff", .tooltip = "Large values give a uniform medium, not a ground layer", .category = "Medium", .minValue = 1.0f, .maxValue = 500.0f, .step = 1.0f)
  float heightFalloff = 60.0f;
  /// Fraction of extinction that scatters rather than absorbs. 1 = pure white fog, low = smoke.
  TUCANO_FIELD(.label = "Albedo", .tooltip = "Scattered vs absorbed. 1 = white fog, low = smoke", .category = "Medium", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float albedo = 0.9f;
  /// Henyey-Greenstein asymmetry, -1..1. Positive scatters forward, which is what makes fog glow
  /// around the sun; 0 is isotropic.
  TUCANO_FIELD(.label = "Anisotropy", .tooltip = "Positive scatters forward, which makes fog glow at the sun", .category = "Medium", .minValue = -0.9f, .maxValue = 0.9f, .step = 0.02f)
  float anisotropy = 0.6f;
  /// Tint applied to the scattered light.
  TUCANO_FIELD(Color, .label = "Scattering tint", .tooltip = "Tint applied to the scattered light", .category = "Lighting")
  glm::vec3 scatteringColor{1.0f, 1.0f, 1.0f};

  // ── Lighting ──
  /// Multiplier on the sun's contribution to the medium.
  TUCANO_FIELD(.label = "Sun scattering", .tooltip = "Multiplier on the sun's contribution to the medium", .category = "Lighting", .minValue = 0.0f, .maxValue = 6.0f, .step = 0.1f)
  float sunIntensity = 1.0f;
  /// Ambient/sky light reaching the medium, so fog in shadow is not black.
  TUCANO_FIELD(.label = "Ambient", .tooltip = "Keeps fog in shadow readable instead of black", .category = "Lighting", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float ambientIntensity = 0.35f;
  /// Sun shadowing of the medium. 0 = unshadowed fog (cheaper, flatter); 1 = full light shafts.
  TUCANO_FIELD(.label = "Light shafts", .tooltip = "How much the sun is shadowed inside the medium", .category = "Lighting", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float shadowStrength = 1.0f;

  // ── Detail ──
  /// Strength of the 3D noise that breaks up the medium, 0 = perfectly smooth fog.
  TUCANO_FIELD(.label = "Noise", .tooltip = "Breaks the medium up so it is not a uniform slab", .category = "Noise", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float noiseStrength = 0.35f;
  /// World size of one noise cell, in metres.
  TUCANO_FIELD(.label = "Noise scale", .tooltip = "World size of one noise cell, in metres", .category = "Noise", .minValue = 5.0f, .maxValue = 400.0f, .step = 5.0f)
  float noiseScale = 80.0f;
  /// Metres per second the noise drifts, along the wind direction.
  TUCANO_FIELD(.label = "Noise speed", .tooltip = "Metres per second the noise drifts along the wind", .category = "Noise", .minValue = 0.0f, .maxValue = 20.0f, .step = 0.1f)
  float noiseSpeed = 1.5f;

  // ── Volume ──
  /// Far edge of the froxel grid, in metres. Beyond it the last slice is extended, so distant
  /// geometry still receives fog but stops gaining detail.
  TUCANO_FIELD(.label = "Max distance", .tooltip = "Far edge of the froxel volume", .category = "Volume", .minValue = 50.0f, .maxValue = 2000.0f, .step = 25.0f)
  float maxDistance = 400.0f;
  /// Depth distribution exponent. >1 packs slices near the camera, where fog detail is visible.
  TUCANO_FIELD(.label = "Depth distribution", .tooltip = ">1 packs froxel slices near the camera", .category = "Volume", .minValue = 1.0f, .maxValue = 4.0f, .step = 0.1f)
  float depthPower = 2.0f;
  /// Blend weight of the previous frame's volume. High values hide the froxel grid's coarseness
  /// but smear fast-moving shadows through the fog.
  TUCANO_FIELD(.label = "Temporal blend", .tooltip = "High hides froxel coarseness but smears moving shadows", .category = "Volume", .minValue = 0.0f, .maxValue = 0.98f, .step = 0.02f)
  float temporalBlend = 0.92f;
};

} // namespace tucano
