#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"

#include <glm/glm.hpp>

namespace tucano {

// CryEngine-inspired rain volume parameters (SRainParams subset, reimplemented).
struct TUCANO_TYPE() RainParams {
  TUCANO_FIELD(.label = "Enabled", .tooltip = "Master switch for every rain pass", .category = "Rain")
  bool enabled = false;
  TUCANO_FIELD(.label = "Amount", .tooltip = "0 = dry, 1 = downpour. Drives wetness, puddles and streaks", .category = "Rain", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float amount = 0.65f;           // r_RainAmount / fCurrentAmount
  TUCANO_FIELD(.label = "View distance", .tooltip = "Metres beyond which deferred rain stops being applied", .category = "Rain", .minValue = 5.0f, .maxValue = 200.0f, .step = 1.0f)
  float maxViewDist = 40.0f;      // r_RainMaxViewDist_Deferred
  TUCANO_FIELD(.label = "Wet darkening", .tooltip = "How much wet albedo darkens; water fills surface pores", .category = "Wetness", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float diffuseDarkening = 0.55f; // wet albedo darken
  TUCANO_FIELD(.label = "Puddles", .tooltip = "Coverage of standing water", .category = "Puddles", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float puddlesAmount = 1.2f;
  TUCANO_FIELD(.label = "Puddle mask", .tooltip = "Threshold on the puddle noise; higher pools in fewer places", .category = "Puddles", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float puddlesMask = 0.7f;
  TUCANO_FIELD(.label = "Puddle ripples", .tooltip = "Impact rings on standing water", .category = "Puddles", .minValue = 0.0f, .maxValue = 4.0f, .step = 0.05f)
  float puddlesRipple = 1.5f;
  TUCANO_FIELD(.label = "Puddle mirror", .tooltip = "Screen-space reflection strength in puddles", .category = "Puddles", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float puddlesSSR = 1.0f; // mirror strength (SSR + local march)
  TUCANO_FIELD(.label = "Splashes", .tooltip = "Impact splashes on wet surfaces", .category = "Impacts", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float splashesAmount = 1.0f;
  TUCANO_FIELD(.label = "Lens drops", .tooltip = "Droplets on the camera lens; 0 removes them", .category = "Lens", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float rainDropsAmount = 0.45f;  // lens drops
  TUCANO_FIELD(.label = "Lens drop speed", .tooltip = "How fast lens droplets run down", .category = "Lens", .minValue = 0.0f, .maxValue = 4.0f, .step = 0.05f)
  float rainDropsSpeed = 1.0f;
  TUCANO_FIELD(.label = "Lens drop light", .tooltip = "How much droplets refract and catch light", .category = "Lens", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float rainDropsLighting = 1.0f;
  TUCANO_FIELD(.label = "Streaks", .tooltip = "Visibility of falling rain streaks", .category = "Falling rain", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float streakIntensity = 0.85f;  // falling streaks
  TUCANO_FIELD(.label = "Streak speed", .tooltip = "Fall speed of the streak layers", .category = "Falling rain", .minValue = 0.0f, .maxValue = 5.0f, .step = 0.05f)
  float streakSpeed = 1.6f;
  TUCANO_FIELD(.label = "Streak layers", .tooltip = "Parallax layers of streaks; more costs more", .category = "Falling rain", .minValue = 1.0f, .maxValue = 8.0f, .step = 1.0f)
  float streakLayers = 3.0f;
  TUCANO_FIELD(.label = "Mist", .tooltip = "Ground haze kicked up by the rain", .category = "Falling rain", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float mistAmount = 0.4f;
  TUCANO_FIELD(.label = "Gloss boost", .tooltip = "Smoothness added when wet; this is what makes it look rained on", .category = "Wetness", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float glossBoost = 0.9f; // smoothness increase when wet
  TUCANO_FIELD(.label = "Volumetric cones", .tooltip = "Light-shaft cones through falling rain", .category = "Falling rain")
  bool enableSceneRain = true;       // volumetric cones
  TUCANO_FIELD(.label = "World splashes", .tooltip = "Ground splash billboards, separate from the surface effect", .category = "Impacts")
  bool enableWorldSplashes = true;   // ground splash billboards
  TUCANO_FIELD(.label = "Cone intensity", .tooltip = "Strength of the volumetric rain cones", .category = "Falling rain", .minValue = 0.0f, .maxValue = 3.0f, .step = 0.05f)
  float sceneRainIntensity = 0.9f;
  TUCANO_FIELD(Color, .label = "Tint", .tooltip = "Colour of streaks, mist and splashes", .category = "Falling rain")
  glm::vec3 color{0.75f, 0.82f, 0.9f};
  TUCANO_FIELD(.label = "Wind", .tooltip = "World-space drift applied to falling rain", .category = "Volume", .minValue = -2.0f, .maxValue = 2.0f, .step = 0.01f)
  glm::vec3 wind{0.15f, 0.0f, 0.05f};
  TUCANO_FIELD(.label = "Volume centre", .tooltip = "Centre of the rain volume in world space", .category = "Volume", .step = 1.0f)
  glm::vec3 worldPos{0.0f, 0.0f, 0.0f};
  TUCANO_FIELD(.label = "Volume radius", .tooltip = "Radius of the rain volume, in metres", .category = "Volume", .minValue = 1.0f, .maxValue = 5000.0f, .step = 10.0f)
  float radius = 2000.0f;
};

} // namespace tucano
