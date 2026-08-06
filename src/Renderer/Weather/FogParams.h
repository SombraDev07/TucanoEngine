#pragma once

#include <glm/glm.hpp>

namespace tucano {

/// Froxel volumetric fog.
///
/// The analytic height fog it replaces could only tint a pixel by its distance: fog was equally
/// bright in shadow and in sun, identical looking toward the sun and away from it, and no light
/// ever shafted through it. This is the participating-media model instead — density and lighting
/// are evaluated in a camera-fitted 3D grid, so the fog is shadowed, scatters anisotropically
/// toward the sun, and produces light shafts for free.
struct FogParams {
  bool enabled = true;

  /// Falls back to the analytic height fog in the lighting pass. Cheaper, and the only option
  /// when the froxel volume cannot be created.
  bool volumetric = true;

  // ── Medium ──
  /// Extinction at the base height, per metre. This is the master density knob.
  float density = 0.02f;
  /// Height at which density starts falling off.
  float baseHeight = 0.0f;
  /// e-folding distance of the vertical falloff, in metres. Large values give a uniform medium.
  float heightFalloff = 60.0f;
  /// Fraction of extinction that scatters rather than absorbs. 1 = pure white fog, low = smoke.
  float albedo = 0.9f;
  /// Henyey-Greenstein asymmetry, -1..1. Positive scatters forward, which is what makes fog glow
  /// around the sun; 0 is isotropic.
  float anisotropy = 0.6f;
  /// Tint applied to the scattered light.
  glm::vec3 scatteringColor{1.0f, 1.0f, 1.0f};

  // ── Lighting ──
  /// Multiplier on the sun's contribution to the medium.
  float sunIntensity = 1.0f;
  /// Ambient/sky light reaching the medium, so fog in shadow is not black.
  float ambientIntensity = 0.35f;
  /// Sun shadowing of the medium. 0 = unshadowed fog (cheaper, flatter); 1 = full light shafts.
  float shadowStrength = 1.0f;

  // ── Detail ──
  /// Strength of the 3D noise that breaks up the medium, 0 = perfectly smooth fog.
  float noiseStrength = 0.35f;
  /// World size of one noise cell, in metres.
  float noiseScale = 80.0f;
  /// Metres per second the noise drifts, along the wind direction.
  float noiseSpeed = 1.5f;

  // ── Volume ──
  /// Far edge of the froxel grid, in metres. Beyond it the last slice is extended, so distant
  /// geometry still receives fog but stops gaining detail.
  float maxDistance = 400.0f;
  /// Depth distribution exponent. >1 packs slices near the camera, where fog detail is visible.
  float depthPower = 2.0f;
  /// Blend weight of the previous frame's volume. High values hide the froxel grid's coarseness
  /// but smear fast-moving shadows through the fog.
  float temporalBlend = 0.92f;
};

} // namespace tucano
