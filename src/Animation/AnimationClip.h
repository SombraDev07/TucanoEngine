#pragma once

#include "Animation/AnimationCurve.h"
#include "Animation/Skeleton.h"

#include <string>
#include <vector>

namespace tucano::anim {

/// Animation curves for a single bone. A track may drive only some channels; empty curves are
/// left at the bind-pose value rather than snapping to zero.
struct BoneTrack {
  std::string boneName;
  int boneIndex = -1; // resolved against a skeleton by resolveBones()
  CurveVec3 position;
  CurveQuat rotation;
  CurveVec3 scale;
};

/// A named animation. Ported from B3DFramework's AnimationClip, minus the resource/RTTI layer.
class AnimationClip {
public:
  const std::string& name() const { return m_name; }
  void setName(std::string name) { m_name = std::move(name); }

  float duration() const { return m_duration; }
  void setDuration(float d) { m_duration = d; }

  const std::vector<BoneTrack>& tracks() const { return m_tracks; }
  std::vector<BoneTrack>& tracks() { return m_tracks; }

  void addTrack(BoneTrack track) {
    m_duration = std::max({m_duration, track.position.endTime(), track.rotation.endTime(),
                           track.scale.endTime()});
    m_tracks.push_back(std::move(track));
  }

  /// Binds tracks to a skeleton by name. Tracks with no matching bone keep index -1 and are
  /// skipped during sampling — a clip authored for another rig should not corrupt this one.
  /// Returns how many tracks resolved.
  size_t resolveBones(const Skeleton& skeleton) {
    size_t resolved = 0;
    for (auto& t : m_tracks) {
      t.boneIndex = skeleton.findBone(t.boneName);
      if (t.boneIndex >= 0) ++resolved;
    }
    return resolved;
  }

  /// Samples the clip into `out`, which must already hold the bind pose: channels the clip does
  /// not animate keep their bind values.
  void sample(float time, WrapMode wrap, Pose& out) const {
    for (const auto& t : m_tracks) {
      if (t.boneIndex < 0 || size_t(t.boneIndex) >= out.size()) continue;
      const auto i = size_t(t.boneIndex);
      if (!t.position.empty()) out.positions[i] = t.position.evaluate(time, wrap);
      if (!t.rotation.empty()) out.rotations[i] = t.rotation.evaluate(time, wrap);
      if (!t.scale.empty()) out.scales[i] = t.scale.evaluate(time, wrap);
    }
  }

  /// Blends two clips into `out` (both must be sampled against the same skeleton).
  /// `weight` 0 = a, 1 = b. Used for cross-fades between states.
  static void blend(const Pose& a, const Pose& b, float weight, Pose& out) {
    const size_t n = std::min(a.size(), b.size());
    out.resize(n);
    const float w = std::clamp(weight, 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) {
      out.positions[i] = glm::mix(a.positions[i], b.positions[i], w);
      // slerp, not mix: see the quaternion note in AnimationCurve.
      glm::quat qb = b.rotations[i];
      if (glm::dot(a.rotations[i], qb) < 0.0f) qb = -qb;
      out.rotations[i] = glm::normalize(glm::slerp(a.rotations[i], qb, w));
      out.scales[i] = glm::mix(a.scales[i], b.scales[i], w);
    }
  }

private:
  std::string m_name;
  float m_duration = 0.0f;
  std::vector<BoneTrack> m_tracks;
};

/// Playback state for one clip on one skeleton.
class AnimationPlayer {
public:
  void play(const AnimationClip* clip, bool loop = true) {
    m_clip = clip;
    m_wrap = loop ? WrapMode::Loop : WrapMode::Clamp;
    m_time = 0.0f;
    m_playing = clip != nullptr;
  }

  void stop() {
    m_playing = false;
    m_time = 0.0f;
  }
  void pause() { m_playing = false; }
  void resume() { m_playing = m_clip != nullptr; }

  bool isPlaying() const { return m_playing; }
  const AnimationClip* clip() const { return m_clip; }

  float time() const { return m_time; }
  void setTime(float t) { m_time = t; }
  float speed() const { return m_speed; }
  void setSpeed(float s) { m_speed = s; }

  void update(float dt) {
    if (!m_playing || !m_clip) return;
    m_time += dt * m_speed;
    // A non-looping clip parks on its last frame instead of running off the end forever.
    if (m_wrap == WrapMode::Clamp && m_time >= m_clip->duration()) {
      m_time = m_clip->duration();
      m_playing = false;
    }
  }

  /// Writes the current pose; `out` should start from the skeleton's bind pose.
  void evaluate(Pose& out) const {
    if (m_clip) m_clip->sample(m_time, m_wrap, out);
  }

private:
  const AnimationClip* m_clip = nullptr;
  WrapMode m_wrap = WrapMode::Loop;
  float m_time = 0.0f;
  float m_speed = 1.0f;
  bool m_playing = false;
};

} // namespace tucano::anim
