#pragma once

#include <glm/glm.hpp>

namespace tucano {

// CryEngine-inspired rain volume parameters (SRainParams subset, reimplemented).
struct RainParams {
  bool enabled = false;
  float amount = 0.65f;           // r_RainAmount / fCurrentAmount
  float maxViewDist = 40.0f;      // r_RainMaxViewDist_Deferred
  float diffuseDarkening = 0.55f; // wet albedo darken
  float puddlesAmount = 1.2f;
  float puddlesMask = 0.7f;
  float puddlesRipple = 1.5f;
  float splashesAmount = 1.0f;
  float rainDropsAmount = 0.45f;  // lens drops
  float rainDropsSpeed = 1.0f;
  float rainDropsLighting = 1.0f;
  float streakIntensity = 0.85f;  // falling streaks
  float streakSpeed = 1.6f;
  float streakLayers = 3.0f;
  float mistAmount = 0.4f;
  float glossBoost = 0.9f; // smoothness increase when wet
  glm::vec3 color{0.75f, 0.82f, 0.9f};
  glm::vec3 wind{0.15f, 0.0f, 0.05f};
  glm::vec3 worldPos{0.0f, 0.0f, 0.0f};
  float radius = 2000.0f;
};

} // namespace tucano
