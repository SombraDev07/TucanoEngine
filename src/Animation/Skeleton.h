#pragma once

#include "Animation/AnimationCurve.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::anim {

/// One joint. `parent` is an index into the same array, -1 for a root.
/// Bones are stored in an order where a parent always precedes its children, so a single forward
/// pass computes world transforms with no recursion.
struct Bone {
  std::string name;
  int parent = -1;
  glm::mat4 inverseBindPose{1.0f}; // mesh space → bone space at bind time
  glm::vec3 localPosition{0.0f};
  glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 localScale{1.0f};
};

/// A local-space pose: one transform per bone.
struct Pose {
  std::vector<glm::vec3> positions;
  std::vector<glm::quat> rotations;
  std::vector<glm::vec3> scales;

  void resize(size_t n) {
    positions.assign(n, glm::vec3(0.0f));
    rotations.assign(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    scales.assign(n, glm::vec3(1.0f));
  }
  size_t size() const { return positions.size(); }
};

/// Bone hierarchy plus bind pose. Ported from B3DFramework's Skeleton.
class Skeleton {
public:
  void addBone(Bone bone) {
    m_nameToIndex[bone.name] = uint32_t(m_bones.size());
    m_bones.push_back(std::move(bone));
  }

  const std::vector<Bone>& bones() const { return m_bones; }
  std::vector<Bone>& bones() { return m_bones; }
  size_t boneCount() const { return m_bones.size(); }
  bool empty() const { return m_bones.empty(); }

  /// Index of a bone by name, or -1.
  int findBone(const std::string& name) const {
    const auto it = m_nameToIndex.find(name);
    return it == m_nameToIndex.end() ? -1 : int(it->second);
  }

  /// The skeleton's own rest transforms as a pose.
  Pose bindPose() const {
    Pose p;
    p.resize(m_bones.size());
    for (size_t i = 0; i < m_bones.size(); ++i) {
      p.positions[i] = m_bones[i].localPosition;
      p.rotations[i] = m_bones[i].localRotation;
      p.scales[i] = m_bones[i].localScale;
    }
    return p;
  }

  /// Local pose → world (model-space) matrices, one per bone.
  void computeWorldMatrices(const Pose& pose, std::vector<glm::mat4>& out) const {
    out.resize(m_bones.size());
    for (size_t i = 0; i < m_bones.size(); ++i) {
      const glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.positions[i]) *
                              glm::mat4_cast(pose.rotations[i]) *
                              glm::scale(glm::mat4(1.0f), pose.scales[i]);
      const int parent = m_bones[i].parent;
      // Parents always come first, so the parent's matrix is already final here.
      out[i] = parent >= 0 ? out[size_t(parent)] * local : local;
    }
  }

  /// Matrices to upload for skinning: world × inverseBindPose per bone.
  void computeSkinningMatrices(const Pose& pose, std::vector<glm::mat4>& out) const {
    std::vector<glm::mat4> world;
    computeWorldMatrices(pose, world);
    out.resize(m_bones.size());
    for (size_t i = 0; i < m_bones.size(); ++i) {
      out[i] = world[i] * m_bones[i].inverseBindPose;
    }
  }

  /// True when every bone's parent appears before it — required by the single-pass evaluation.
  bool isTopologicallySorted() const {
    for (size_t i = 0; i < m_bones.size(); ++i) {
      if (m_bones[i].parent >= int(i)) return false;
    }
    return true;
  }

private:
  std::vector<Bone> m_bones;
  std::unordered_map<std::string, uint32_t> m_nameToIndex;
};

} // namespace tucano::anim
