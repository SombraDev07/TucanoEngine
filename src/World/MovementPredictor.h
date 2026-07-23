#pragma once

// WM-3: movement prediction.
//
// The scheduler in WM-2 loads what an observer can see NOW. That is already too late when the
// observer moves fast: by the time a cell enters the load radius, the disk read has not started,
// so it pops in. Prediction fixes this by streaming a little way into the future — cells in the
// direction of travel begin loading before the observer arrives.
//
// Two design choices depart from the roadmap:
//
//   * The roadmap estimates velocity by finite-differencing a position history. But a Streaming
//     observer already carries a velocity (the game's physics knows it exactly). So this PREFERS
//     the supplied velocity and only estimates from history when none is given — a spectator
//     camera that reports position but not velocity still gets prediction, and a physics-driven
//     player gets exact values instead of a noisy derivative.
//
//   * Prediction only ever ADDS to the load set, never the unload set. Loading ahead is useful;
//     keeping cells resident behind you is not — that is what the unload hysteresis already does.
//     A predicted cell the observer never reaches simply ages out through the normal unload path.

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace tucano::world {

struct PredictionSettings {
  bool enabled = true;

  /// Below this speed there is no meaningful heading, so prediction switches off — standing still
  /// must not stream a random direction.
  float minSpeed = 1.0f;
  /// Teleports and frame hitches produce absurd velocities; clamp so one bad sample cannot request
  /// half the world.
  float maxSpeed = 200.0f;

  /// How far ahead, in seconds, to sample. Two horizons: a near one to cover the next moment and a
  /// far one to give the disk a head start. More horizons cost more prefetch cells.
  std::vector<float> horizons = {0.8f, 2.0f};

  /// Radius around each predicted point. Smaller than the real load radius — prefetch only needs
  /// to seed the pipeline ahead, not to fully populate the future.
  float prefetchRadius = 96.0f;

  /// Heading is uncertain, more so further out, so each horizon fans into a cone. This is the
  /// half-angle at the FARTHEST horizon; nearer horizons scale down proportionally.
  float coneHalfAngleDeg = 20.0f;
  /// Lateral samples per side, per horizon. 0 disables the cone (centre line only).
  uint32_t coneSamples = 2;

  /// EMA weight for smoothing the velocity/acceleration estimate. Low enough to reject jitter,
  /// high enough to follow a real turn.
  float smoothing = 0.35f;
};

/// Tracks one observer's motion and projects where it is heading.
///
/// One instance per observer, kept alive across frames by the scheduler (keyed on observer id).
class MovementPredictor {
public:
  /// Feeds a new sample. `suppliedVelocity` is used directly when it has meaningful magnitude;
  /// otherwise velocity is estimated from the position delta. `dtSeconds` is the frame time.
  void update(const glm::vec3& position, const glm::vec3& suppliedVelocity, float dtSeconds,
              const PredictionSettings& settings);

  const glm::vec3& position() const { return m_position; }
  const glm::vec3& velocity() const { return m_velocity; }
  const glm::vec3& acceleration() const { return m_acceleration; }

  float speed() const { return glm::length(m_velocity); }
  bool moving(const PredictionSettings& s) const { return speed() >= s.minSpeed; }

  /// Position `secondsAhead` in the future under constant acceleration.
  glm::vec3 predict(float secondsAhead) const {
    return m_position + m_velocity * secondsAhead +
           m_acceleration * (0.5f * secondsAhead * secondsAhead);
  }

  /// Points to prefetch around: the predicted trajectory plus a widening cone for heading
  /// uncertainty. Empty when the observer is not moving. Deterministic for a given state.
  void samplePoints(const PredictionSettings& settings, std::vector<glm::vec3>& out) const;

private:
  glm::vec3 m_position{0.0f};
  glm::vec3 m_velocity{0.0f};
  glm::vec3 m_acceleration{0.0f};
  glm::vec3 m_prevVelocity{0.0f};
  bool m_hasSample = false;
};

} // namespace tucano::world
