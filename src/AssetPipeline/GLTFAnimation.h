#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"

#include <memory>
#include <string>
#include <vector>

namespace tucano {

/// Skeleton plus every animation clip found in a glTF file.
struct SkinnedAsset {
  anim::Skeleton skeleton;
  std::vector<std::shared_ptr<anim::AnimationClip>> clips;
  bool valid() const { return !skeleton.empty(); }
};

/// Reads the first skin and all animations from a .gltf/.glb.
/// Separate from loadGLTFScene because a rig is useful without geometry (retargeting, inspection)
/// and geometry is far more common than a rig.
/// Returns an empty asset when the file has no skin; `error` receives the reason when non-null.
SkinnedAsset loadGLTFSkinnedAsset(const std::string& path, std::string* error = nullptr);

} // namespace tucano
