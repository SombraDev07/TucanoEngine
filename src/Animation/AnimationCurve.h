#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <vector>

namespace tucano::anim {

/// One key on an animation spline. Ported from B3DFramework's TKeyframe.
/// Tangents are what let a curve keep its authored shape: linear interpolation between keys makes
/// motion visibly robotic at the key boundaries.
template <typename T>
struct Keyframe {
  T value{};
  T inTangent{};  // slope arriving at this key (from the previous one)
  T outTangent{}; // slope leaving this key (towards the next one)
  float time = 0.0f;
};

/// Cubic-Hermite interpolation between two keys.
/// h00/h10/h01/h11 are the standard Hermite basis functions; `length` scales the tangents back into
/// the key's own time span so curve shape is independent of key spacing.
template <typename T>
T hermite(float t, const T& p0, const T& p1, const T& m0, const T& m1, float length) {
  const float t2 = t * t;
  const float t3 = t2 * t;

  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;

  return p0 * h00 + m0 * (h10 * length) + p1 * h01 + m1 * (h11 * length);
}

enum class WrapMode {
  Clamp,  // hold the first/last value outside the range
  Loop,   // wrap around
  PingPong // bounce back and forth
};

/// A keyframed curve of T. Keys are kept sorted by time.
template <typename T>
class AnimationCurve {
public:
  AnimationCurve() = default;
  explicit AnimationCurve(std::vector<Keyframe<T>> keys) : m_keys(std::move(keys)) { sort(); }

  void addKey(const Keyframe<T>& key) {
    m_keys.push_back(key);
    sort();
  }

  void setKeys(std::vector<Keyframe<T>> keys) {
    m_keys = std::move(keys);
    sort();
  }

  const std::vector<Keyframe<T>>& keys() const { return m_keys; }
  bool empty() const { return m_keys.empty(); }
  size_t size() const { return m_keys.size(); }

  float startTime() const { return m_keys.empty() ? 0.0f : m_keys.front().time; }
  float endTime() const { return m_keys.empty() ? 0.0f : m_keys.back().time; }
  float length() const { return endTime() - startTime(); }

  /// Samples the curve. Out-of-range times follow `wrap`.
  T evaluate(float time, WrapMode wrap = WrapMode::Clamp) const {
    if (m_keys.empty()) return T{};
    if (m_keys.size() == 1) return m_keys.front().value;

    time = wrapTime(time, wrap);

    // Before the first / after the last key: hold the edge value.
    if (time <= m_keys.front().time) return m_keys.front().value;
    if (time >= m_keys.back().time) return m_keys.back().value;

    const size_t i = findSegment(time);
    const Keyframe<T>& a = m_keys[i];
    const Keyframe<T>& b = m_keys[i + 1];

    const float span = b.time - a.time;
    if (span <= 1e-8f) return b.value; // coincident keys: nothing to interpolate

    const float t = (time - a.time) / span;
    return hermite(t, a.value, b.value, a.outTangent, b.inTangent, span);
  }

  /// Replaces all tangents with a Catmull-Rom estimate, which is what an importer wants when the
  /// source format only stores values (glTF LINEAR/STEP samplers, for instance).
  void buildSmoothTangents() {
    const size_t n = m_keys.size();
    if (n < 2) return;

    for (size_t i = 0; i < n; ++i) {
      if (i == 0) {
        const float span = m_keys[1].time - m_keys[0].time;
        const T slope = span > 1e-8f ? (m_keys[1].value - m_keys[0].value) * (1.0f / span) : T{};
        m_keys[0].inTangent = slope;
        m_keys[0].outTangent = slope;
      } else if (i == n - 1) {
        const float span = m_keys[n - 1].time - m_keys[n - 2].time;
        const T slope =
            span > 1e-8f ? (m_keys[n - 1].value - m_keys[n - 2].value) * (1.0f / span) : T{};
        m_keys[i].inTangent = slope;
        m_keys[i].outTangent = slope;
      } else {
        const float span = m_keys[i + 1].time - m_keys[i - 1].time;
        const T slope =
            span > 1e-8f ? (m_keys[i + 1].value - m_keys[i - 1].value) * (1.0f / span) : T{};
        m_keys[i].inTangent = slope;
        m_keys[i].outTangent = slope;
      }
    }
  }

  /// Zeroes every tangent, giving piecewise-linear motion (matches a glTF LINEAR sampler exactly).
  void makeLinear() {
    for (auto& k : m_keys) {
      k.inTangent = T{};
      k.outTangent = T{};
    }
  }

private:
  void sort() {
    std::stable_sort(m_keys.begin(), m_keys.end(),
                     [](const Keyframe<T>& a, const Keyframe<T>& b) { return a.time < b.time; });
  }

  size_t findSegment(float time) const {
    // Binary search: clips can carry thousands of keys and this runs per bone per frame.
    size_t lo = 0, hi = m_keys.size() - 1;
    while (lo + 1 < hi) {
      const size_t mid = (lo + hi) / 2;
      if (m_keys[mid].time <= time) lo = mid; else hi = mid;
    }
    return lo;
  }

  float wrapTime(float time, WrapMode wrap) const {
    const float start = startTime();
    const float len = length();
    if (len <= 1e-8f || wrap == WrapMode::Clamp) return time;

    float rel = time - start;
    if (wrap == WrapMode::Loop) {
      rel = std::fmod(rel, len);
      if (rel < 0.0f) rel += len;
    } else { // PingPong
      const float twice = len * 2.0f;
      rel = std::fmod(rel, twice);
      if (rel < 0.0f) rel += twice;
      if (rel > len) rel = twice - rel;
    }
    return start + rel;
  }

  std::vector<Keyframe<T>> m_keys;
};

/// Quaternions need slerp, not component-wise Hermite: interpolating the four numbers separately
/// takes a shortcut through the inside of the hypersphere and the rotation speeds up mid-way.
template <>
inline glm::quat AnimationCurve<glm::quat>::evaluate(float time, WrapMode wrap) const {
  if (m_keys.empty()) return glm::quat(1, 0, 0, 0);
  if (m_keys.size() == 1) return m_keys.front().value;

  time = wrapTime(time, wrap);
  if (time <= m_keys.front().time) return m_keys.front().value;
  if (time >= m_keys.back().time) return m_keys.back().value;

  const size_t i = findSegment(time);
  const glm::quat& a = m_keys[i].value;
  glm::quat b = m_keys[i + 1].value;

  const float span = m_keys[i + 1].time - m_keys[i].time;
  if (span <= 1e-8f) return b;

  // Take the short way around: q and -q are the same rotation.
  if (glm::dot(a, b) < 0.0f) b = -b;
  return glm::normalize(glm::slerp(a, b, (time - m_keys[i].time) / span));
}

using CurveFloat = AnimationCurve<float>;
using CurveVec3 = AnimationCurve<glm::vec3>;
using CurveQuat = AnimationCurve<glm::quat>;

} // namespace tucano::anim
