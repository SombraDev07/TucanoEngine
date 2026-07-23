#include "AssetPipeline/GLTFAnimation.h"

// cgltf implementation lives in GLTFLoader.cpp; this file only needs the declarations.
#include <cgltf.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <unordered_map>

namespace tucano {
namespace {

// glTF nodes form an arbitrary tree; the animation code needs parents before children so a single
// forward pass can accumulate transforms. This produces that ordering for the joints of one skin.
void collectJointOrder(const cgltf_skin& skin,
                       std::vector<const cgltf_node*>& ordered,
                       std::unordered_map<const cgltf_node*, int>& indexOf) {
  std::unordered_map<const cgltf_node*, bool> isJoint;
  for (cgltf_size i = 0; i < skin.joints_count; ++i) {
    isJoint[skin.joints[i]] = true;
  }

  // Repeatedly emit joints whose parent is already emitted (or is not a joint at all).
  std::vector<bool> done(skin.joints_count, false);
  bool progress = true;
  while (progress) {
    progress = false;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
      if (done[i]) continue;
      const cgltf_node* node = skin.joints[i];
      const cgltf_node* parent = node->parent;
      const bool parentIsJoint = parent && isJoint.count(parent) > 0;
      if (parentIsJoint && indexOf.find(parent) == indexOf.end()) continue; // parent not out yet

      indexOf[node] = int(ordered.size());
      ordered.push_back(node);
      done[i] = true;
      progress = true;
    }
  }

  // A cycle would be a malformed file; emit the leftovers so we degrade instead of hanging.
  for (cgltf_size i = 0; i < skin.joints_count; ++i) {
    if (done[i]) continue;
    indexOf[skin.joints[i]] = int(ordered.size());
    ordered.push_back(skin.joints[i]);
  }
}

void readNodeTransform(const cgltf_node* node, glm::vec3& t, glm::quat& r, glm::vec3& s) {
  t = glm::vec3(0.0f);
  r = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  s = glm::vec3(1.0f);

  if (node->has_translation) t = glm::make_vec3(node->translation);
  // glTF stores quaternions xyzw; glm::quat is wxyz.
  if (node->has_rotation) {
    r = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
  }
  if (node->has_scale) s = glm::make_vec3(node->scale);

  if (node->has_matrix) {
    const glm::mat4 m = glm::make_mat4(node->matrix);
    t = glm::vec3(m[3]);
    s = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                  glm::length(glm::vec3(m[2])));
    glm::mat3 rot(m);
    if (s.x > 1e-8f) rot[0] /= s.x;
    if (s.y > 1e-8f) rot[1] /= s.y;
    if (s.z > 1e-8f) rot[2] /= s.z;
    r = glm::quat_cast(rot);
  }
}

// glTF STEP samplers hold a value until the next key. Hermite can't express that, so a step
// channel is expanded into pairs of keys straddling each boundary.
template <typename T>
void appendStepKeys(std::vector<anim::Keyframe<T>>& keys, float time, const T& value) {
  if (!keys.empty()) {
    anim::Keyframe<T> hold = keys.back();
    hold.time = std::nextafter(time, time - 1.0f);
    keys.push_back(hold);
  }
  keys.push_back(anim::Keyframe<T>{value, T{}, T{}, time});
}

} // namespace

SkinnedAsset loadGLTFSkinnedAsset(const std::string& path, std::string* error) {
  SkinnedAsset out;

  cgltf_options options{};
  cgltf_data* data = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
    if (error) *error = "could not parse " + path;
    return out;
  }
  if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
    if (error) *error = "could not load buffers for " + path;
    cgltf_free(data);
    return out;
  }

  if (data->skins_count == 0) {
    if (error) *error = "no skin in " + path;
    cgltf_free(data);
    return out;
  }

  const cgltf_skin& skin = data->skins[0];
  std::vector<const cgltf_node*> ordered;
  std::unordered_map<const cgltf_node*, int> indexOf;
  collectJointOrder(skin, ordered, indexOf);

  for (size_t i = 0; i < ordered.size(); ++i) {
    const cgltf_node* node = ordered[i];
    anim::Bone bone;
    bone.name = node->name ? node->name : ("joint" + std::to_string(i));

    const auto pit = node->parent ? indexOf.find(node->parent) : indexOf.end();
    bone.parent = pit != indexOf.end() ? pit->second : -1;

    readNodeTransform(node, bone.localPosition, bone.localRotation, bone.localScale);

    // Inverse bind matrices are indexed by the skin's own joint order, not ours.
    if (skin.inverse_bind_matrices) {
      cgltf_size original = 0;
      for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        if (skin.joints[j] == node) { original = j; break; }
      }
      float m[16]{};
      cgltf_accessor_read_float(skin.inverse_bind_matrices, original, m, 16);
      bone.inverseBindPose = glm::make_mat4(m);
    }

    out.skeleton.addBone(std::move(bone));
  }

  // Animations. A channel targets a node; only nodes that are joints of this skin are kept.
  for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
    const cgltf_animation& a = data->animations[ai];
    auto clip = std::make_shared<anim::AnimationClip>();
    clip->setName(a.name ? a.name : ("clip" + std::to_string(ai)));

    // One track per targeted joint, built up across channels.
    std::unordered_map<int, anim::BoneTrack> tracks;

    for (cgltf_size ci = 0; ci < a.channels_count; ++ci) {
      const cgltf_animation_channel& ch = a.channels[ci];
      if (!ch.target_node || !ch.sampler) continue;
      const auto it = indexOf.find(ch.target_node);
      if (it == indexOf.end()) continue; // not part of this skin

      const cgltf_animation_sampler& sampler = *ch.sampler;
      const cgltf_accessor* input = sampler.input;
      const cgltf_accessor* output = sampler.output;
      if (!input || !output) continue;

      auto& track = tracks[it->second];
      if (track.boneName.empty()) {
        track.boneName = out.skeleton.bones()[size_t(it->second)].name;
        track.boneIndex = it->second;
      }

      const bool step = sampler.interpolation == cgltf_interpolation_type_step;
      // Cubic spline samplers store in-tangent/value/out-tangent triplets per key.
      const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
      const cgltf_size stride = cubic ? 3 : 1;

      std::vector<anim::Keyframe<glm::vec3>> vecKeys;
      std::vector<anim::Keyframe<glm::quat>> quatKeys;

      for (cgltf_size k = 0; k < input->count; ++k) {
        float t = 0.0f;
        cgltf_accessor_read_float(input, k, &t, 1);
        const cgltf_size base = k * stride + (cubic ? 1 : 0); // skip the in-tangent

        if (ch.target_path == cgltf_animation_path_type_rotation) {
          float q[4]{0, 0, 0, 1};
          cgltf_accessor_read_float(output, base, q, 4);
          const glm::quat value(q[3], q[0], q[1], q[2]);
          if (step) {
            if (!quatKeys.empty()) {
              auto hold = quatKeys.back();
              hold.time = std::nextafter(t, t - 1.0f);
              quatKeys.push_back(hold);
            }
            quatKeys.push_back({value, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), t});
          } else {
            quatKeys.push_back({value, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), t});
          }
        } else {
          float v[3]{};
          cgltf_accessor_read_float(output, base, v, 3);
          const glm::vec3 value(v[0], v[1], v[2]);
          if (step) {
            appendStepKeys(vecKeys, t, value);
          } else {
            vecKeys.push_back({value, glm::vec3(0.0f), glm::vec3(0.0f), t});
          }
        }
      }

      switch (ch.target_path) {
        case cgltf_animation_path_type_translation:
          track.position.setKeys(std::move(vecKeys));
          if (!step) track.position.buildSmoothTangents();
          break;
        case cgltf_animation_path_type_scale:
          track.scale.setKeys(std::move(vecKeys));
          if (!step) track.scale.buildSmoothTangents();
          break;
        case cgltf_animation_path_type_rotation:
          // Rotation curves slerp between keys, so tangents are unused.
          track.rotation.setKeys(std::move(quatKeys));
          break;
        default:
          break; // weights (morph targets) are not supported yet
      }
    }

    for (auto& [idx, track] : tracks) {
      clip->addTrack(std::move(track));
    }
    if (!clip->tracks().empty()) out.clips.push_back(std::move(clip));
  }

  cgltf_free(data);
  return out;
}

} // namespace tucano
