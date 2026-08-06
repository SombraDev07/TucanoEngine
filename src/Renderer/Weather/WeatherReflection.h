#pragma once

// Field declarations for the weather parameter blocks.
//
// Kept in one place next to the structs rather than inside them: the params headers are included by
// shaders-adjacent code and samples that have no interest in reflection, and the ranges here are
// editor presentation concerns, not engine contracts.
//
// Adding a field to WaterParams or FogParams and forgetting to declare it here means it will not
// appear in the generated panel — the compiler cannot catch that. It is the one piece of
// duplication this design keeps, in exchange for the params structs staying plain aggregates.

#include "Core/Reflection.h"
#include "Renderer/Weather/FogParams.h"
#include "Renderer/Weather/WaterParams.h"

TUCANO_REFLECT_BEGIN(WaterParams)
  TUCANO_FIELD_BOOL (enabled,             "Enabled",            "Draw the water pass at all")
  TUCANO_FIELD_FLOAT(waterLevel,          "Water level",        "World height of the surface, in metres", -200.0f, 200.0f, 0.5f)
  TUCANO_FIELD_FLOAT(waveAmplitude,       "Wave amplitude",     "Height of the largest wave octave", 0.0f, 5.0f, 0.05f)
  TUCANO_FIELD_FLOAT(waveFrequency,       "Wave frequency",     "Wavenumber of the first octave; higher is choppier", 0.01f, 1.0f, 0.01f)
  TUCANO_FIELD_FLOAT(waveSpeed,           "Wave speed",         "Multiplier on the deep-water phase speed", 0.0f, 3.0f, 0.05f)
  TUCANO_FIELD_FLOAT(waveChoppy,          "Choppiness",         "Crest sharpening; also drives whitecap detection", 0.0f, 2.0f, 0.05f)
  TUCANO_FIELD_FLOAT(waveCount,           "Wave octaves",       "How many directional waves are summed (max 12)", 1.0f, 12.0f, 1.0f)
  TUCANO_FIELD_FLOAT(foamAmount,          "Foam",               "Whitecap and shoreline foam coverage", 0.0f, 2.0f, 0.05f)
  TUCANO_FIELD_FLOAT(normalStrength,      "Normal strength",    "Scales the wave slope feeding the surface normal", 0.0f, 3.0f, 0.05f)
  TUCANO_FIELD_FLOAT(roughness,           "Roughness",          "Microfacet roughness; small values give a mirror", 0.0f, 0.5f, 0.005f)
  TUCANO_FIELD_FLOAT(reflectionStrength,  "Reflection",         "Multiplier on the reflected environment", 0.0f, 2.0f, 0.05f)
  TUCANO_FIELD_FLOAT(sssIntensity,        "Subsurface",         "Glow through a thin, backlit crest", 0.0f, 2.0f, 0.05f)
  TUCANO_FIELD_FLOAT(shoreHardness,       "Shore hardness",     "How quickly foam builds in shallow water", 0.0f, 10.0f, 0.1f)
  TUCANO_FIELD_FLOAT(fogDensity,          "Underwater fog",     "Extinction seen from below the surface", 0.0f, 0.2f, 0.005f)
  TUCANO_FIELD_VEC2 (windDirection,       "Wind direction",     "XZ direction the wave field travels", -1.0f, 1.0f)
  TUCANO_FIELD_VEC3 (absorption,          "Absorption",         "Beer-Lambert extinction per metre, per channel", 0.0f, 2.0f)

  TUCANO_FIELD_BOOL (enableSSR,           "Screen-space reflections", "Off falls back to the environment probe alone")
  TUCANO_FIELD_FLOAT(ssrSteps,            "SSR steps",          "Ray-march budget; 32 is enough at 1080p", 4.0f, 128.0f, 1.0f)
  TUCANO_FIELD_FLOAT(ssrMaxDistance,      "SSR distance",       "World length of the reflection ray", 10.0f, 1000.0f, 10.0f)
  TUCANO_FIELD_FLOAT(ssrEdgeFade,         "SSR edge fade",      "Fade near the screen border, where there is no data", 0.01f, 0.5f, 0.01f)
  TUCANO_FIELD_FLOAT(refractionStrength,  "Refraction",         "Screen-space distortion of the view through the surface", 0.0f, 0.2f, 0.005f)

  TUCANO_FIELD_BOOL (rainDrivesWater,     "Rain drives water",  "Feed RainParams amount into the surface automatically")
  TUCANO_FIELD_FLOAT(rainIntensity,       "Rain intensity",     "0 = dry. Overwritten each frame when rain drives water", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(rainRippleStrength,  "Ripple strength",    "Height of the concentric impact ripples", 0.0f, 2.0f, 0.05f)
  TUCANO_FIELD_FLOAT(rainRippleScale,     "Ripple density",     "Impact cells per metre; 4 gives ~25 cm rings", 0.5f, 12.0f, 0.25f)
  TUCANO_FIELD_FLOAT(rainRippleSpeed,     "Ripple speed",       "Impacts per second per cell", 0.0f, 8.0f, 0.1f)
  TUCANO_FIELD_FLOAT(rainRoughness,       "Rain roughness",     "Extra roughness at full rain; this is what breaks the mirror", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(rainFoam,            "Rain foam",          "Extra foam coverage at full rain", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(detailFadeDistance,  "Detail fade",        "Distance at which detail octaves stop, to stop aliasing", 20.0f, 1000.0f, 10.0f)
TUCANO_REFLECT_END()

TUCANO_REFLECT_BEGIN(FogParams)
  TUCANO_FIELD_BOOL (enabled,             "Enabled",            "Master switch for both fog paths")
  TUCANO_FIELD_BOOL (volumetric,          "Volumetric",         "Off falls back to the cheap analytic height fog")
  TUCANO_FIELD_FLOAT(density,             "Density",            "Extinction at the base height, per metre", 0.0f, 0.2f, 0.002f)
  TUCANO_FIELD_FLOAT(baseHeight,          "Base height",        "Height at which density starts falling off", -200.0f, 500.0f, 1.0f)
  TUCANO_FIELD_FLOAT(heightFalloff,       "Height falloff",     "Large values give a uniform medium, not a ground layer", 1.0f, 500.0f, 1.0f)
  TUCANO_FIELD_FLOAT(albedo,              "Albedo",             "Scattered vs absorbed. 1 = white fog, low = smoke", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(anisotropy,          "Anisotropy",         "Positive scatters forward, which makes fog glow at the sun", -0.9f, 0.9f, 0.02f)
  TUCANO_FIELD_COLOR(scatteringColor,     "Scattering tint",    "Tint applied to the scattered light")
  TUCANO_FIELD_FLOAT(sunIntensity,        "Sun scattering",     "Multiplier on the sun's contribution to the medium", 0.0f, 6.0f, 0.1f)
  TUCANO_FIELD_FLOAT(ambientIntensity,    "Ambient",            "Keeps fog in shadow readable instead of black", 0.0f, 3.0f, 0.05f)
  TUCANO_FIELD_FLOAT(shadowStrength,      "Light shafts",       "How much the sun is shadowed inside the medium", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(noiseStrength,       "Noise",              "Breaks the medium up so it is not a uniform slab", 0.0f, 1.0f, 0.02f)
  TUCANO_FIELD_FLOAT(noiseScale,          "Noise scale",        "World size of one noise cell, in metres", 5.0f, 400.0f, 5.0f)
  TUCANO_FIELD_FLOAT(noiseSpeed,          "Noise speed",        "Metres per second the noise drifts along the wind", 0.0f, 20.0f, 0.1f)
  TUCANO_FIELD_FLOAT(maxDistance,         "Max distance",       "Far edge of the froxel volume", 50.0f, 2000.0f, 25.0f)
  TUCANO_FIELD_FLOAT(depthPower,          "Depth distribution", ">1 packs froxel slices near the camera", 1.0f, 4.0f, 0.1f)
  TUCANO_FIELD_FLOAT(temporalBlend,       "Temporal blend",     "High hides froxel coarseness but smears moving shadows", 0.0f, 0.98f, 0.02f)
TUCANO_REFLECT_END()
