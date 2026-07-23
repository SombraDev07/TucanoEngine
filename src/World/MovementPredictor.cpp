#include "World/MovementPredictor.h"

#include <algorithm>
#include <cmath>

namespace tucano::world {
namespace {

constexpr float kDeg2Rad = 0.01745329251f;

glm::vec3 clampSpeed(const glm::vec3& v, float maxSpeed) {
  const float s = glm::length(v);
  if (s > maxSpeed && s > 1e-6f) return v * (maxSpeed / s);
  return v;
}

} // namespace

void MovementPredictor::update(const glm::vec3& position, const glm::vec3& suppliedVelocity,
                               float dtSeconds, const PredictionSettings& settings) {
  const float dt = std::max(dtSeconds, 1e-4f);

  glm::vec3 newVelocity;
  if (glm::length(suppliedVelocity) > 1e-3f) {
    // Trust the game's own velocity. It is exact where a finite difference would be noisy — and it
    // stays correct even when the observer is teleported by gameplay rather than moving.
    newVelocity = clampSpeed(suppliedVelocity, settings.maxSpeed);
  } else if (m_hasSample) {
    // No velocity given: estimate it from how far the observer moved. Clamped so a teleport (a huge
    // one-frame jump) does not translate into a request for a distant region.
    newVelocity = clampSpeed((position - m_position) / dt, settings.maxSpeed);
  } else {
    newVelocity = glm::vec3(0.0f);
  }

  if (!m_hasSample) {
    // First sample: seed everything so the smoothing has a starting point instead of lurching from
    // zero on frame two.
    m_velocity = newVelocity;
    m_prevVelocity = newVelocity;
    m_acceleration = glm::vec3(0.0f);
    m_position = position;
    m_hasSample = true;
    return;
  }

  // Exponential moving average on both derivatives. Streaming that chases raw frame-to-frame
  // velocity would fan prefetch in a jittering direction and thrash the disk.
  m_velocity = glm::mix(m_velocity, newVelocity, settings.smoothing);
  const glm::vec3 accelSample = (m_velocity - m_prevVelocity) / dt;
  m_acceleration = clampSpeed(glm::mix(m_acceleration, accelSample, settings.smoothing),
                              settings.maxSpeed);
  m_prevVelocity = m_velocity;
  m_position = position;
}

void MovementPredictor::samplePoints(const PredictionSettings& settings,
                                     std::vector<glm::vec3>& out) const {
  out.clear();
  if (!moving(settings) || settings.horizons.empty()) return;

  const glm::vec3 dir = m_velocity / std::max(glm::length(m_velocity), 1e-6f);

  // A horizontal "right" axis to fan the cone across. Movement is overwhelmingly horizontal, so
  // spreading the uncertainty sideways (not vertically) is what matches how observers actually
  // change heading. Fall back to a world axis if travelling near-vertically.
  glm::vec3 up(0.0f, 1.0f, 0.0f);
  glm::vec3 right = glm::cross(dir, up);
  if (glm::length(right) < 1e-3f) right = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
  right = glm::normalize(right);

  const float maxHorizon = *std::max_element(settings.horizons.begin(), settings.horizons.end());

  for (float h : settings.horizons) {
    const glm::vec3 centre = predict(h);
    out.push_back(centre);

    if (settings.coneSamples == 0) continue;

    const float alongDist = glm::length(centre - m_position);
    // Uncertainty grows with how far out this horizon is.
    const float horizonFrac = maxHorizon > 1e-6f ? h / maxHorizon : 1.0f;

    for (uint32_t k = 1; k <= settings.coneSamples; ++k) {
      const float frac = float(k) / float(settings.coneSamples);
      const float angle = settings.coneHalfAngleDeg * kDeg2Rad * horizonFrac * frac;
      const float lateral = std::tan(angle) * alongDist;
      out.push_back(centre + right * lateral);
      out.push_back(centre - right * lateral);
    }
  }
}

} // namespace tucano::world
