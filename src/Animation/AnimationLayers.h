#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace tucano::anim {

struct BoneMask {
	std::unordered_set<std::string> includedBones;
	bool includeAll = true;

	void includeBone(const std::string& name) {
		includeAll = false;
		includedBones.insert(name);
	}

	void excludeBone(const std::string& name) {
		includeAll = true;
		includedBones.erase(name);
	}

	bool affectsBone(int boneIndex, const Skeleton& skeleton) const {
		if (includeAll) return true;
		if (boneIndex < 0 || size_t(boneIndex) >= skeleton.bones().size()) return false;
		return includedBones.count(skeleton.bones()[size_t(boneIndex)].name) > 0;
	}

	static BoneMask fullBody() { return {}; }
	static BoneMask upperBody() {
		BoneMask m; m.includeAll = false;
		m.includedBones = {"spine", "spine1", "spine2", "neck", "head",
		                   "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
		                   "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r"};
		return m;
	}
	static BoneMask lowerBody() {
		BoneMask m; m.includeAll = false;
		m.includedBones = {"pelvis", "thigh_l", "calf_l", "foot_l",
		                   "thigh_r", "calf_r", "foot_r"};
		return m;
	}
};

struct AnimationLayer {
	std::string name;
	BoneMask mask;
	const AnimationClip* clip = nullptr;
	float weight = 1.0f;
	float targetWeight = 1.0f;
	float blendSpeed = 5.0f;
	float time = 0;
	float speed = 1.0f;
	bool playing = false;
	bool loop = true;
	bool additive = false;

	void play(const AnimationClip* c, bool lp = true, float spd = 1.0f) {
		clip = c;
		loop = lp;
		speed = spd;
		time = 0;
		playing = c != nullptr;
		targetWeight = 1.0f;
	}

	void stop(float fadeOut = 0.2f) {
		targetWeight = 0;
		blendSpeed = 1.0f / std::max(fadeOut, 0.001f);
	}

	void pause() { playing = false; }
	void resume() { playing = clip != nullptr; }
};

class AnimationLayers {
public:
	AnimationLayer& addLayer(const std::string& name) {
		m_layers.push_back({name});
		return m_layers.back();
	}

	AnimationLayer* layer(const std::string& name) {
		for (auto& l : m_layers) if (l.name == name) return &l;
		return nullptr;
	}

	void removeLayer(const std::string& name) {
		m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
			[&](const AnimationLayer& l) { return l.name == name; }), m_layers.end());
	}

	void update(float dt) {
		for (auto& layer : m_layers) {
			if (!layer.playing || !layer.clip) continue;
			layer.time += dt * layer.speed;

			if (!layer.loop && layer.time >= layer.clip->duration()) {
				layer.time = layer.clip->duration();
				layer.playing = false;
			}

			if (layer.weight < layer.targetWeight) {
				layer.weight = std::min(layer.weight + layer.blendSpeed * dt, layer.targetWeight);
			} else if (layer.weight > layer.targetWeight) {
				layer.weight = std::max(layer.weight - layer.blendSpeed * dt, layer.targetWeight);
			}
		}

		m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
			[](const AnimationLayer& l) { return l.weight <= 0.001f && !l.playing; }), m_layers.end());
	}

	void evaluate(const Skeleton& skeleton, Pose& out) const {
		out = skeleton.bindPose();

		for (const auto& layer : m_layers) {
			if (!layer.clip || layer.weight <= 0.001f) continue;

			Pose layerPose = skeleton.bindPose();
			WrapMode wrap = layer.loop ? WrapMode::Loop : WrapMode::Clamp;
			layer.clip->sample(layer.time, wrap, layerPose);

			if (layer.additive) {
				applyAdditive(layerPose, skeleton.bindPose(), skeleton, layer.mask, layer.weight, out);
			} else {
				applyLayer(layerPose, skeleton, layer.mask, layer.weight, out);
			}
		}
	}

	void clear() { m_layers.clear(); }
	size_t layerCount() const { return m_layers.size(); }
	std::vector<AnimationLayer>& layers() { return m_layers; }
	const std::vector<AnimationLayer>& layers() const { return m_layers; }

private:
	void applyLayer(const Pose& src, const Skeleton& skeleton, const BoneMask& mask,
	                float weight, Pose& dst) const {
		for (size_t i = 0; i < dst.size() && i < src.size(); ++i) {
			if (!mask.affectsBone(int(i), skeleton)) continue;

			dst.positions[i] = glm::mix(dst.positions[i], src.positions[i], weight);
			if (glm::dot(dst.rotations[i], src.rotations[i]) < 0) {
				glm::quat negSrc = glm::quat(-src.rotations[i].w, -src.rotations[i].x,
				                             -src.rotations[i].y, -src.rotations[i].z);
				dst.rotations[i] = glm::slerp(dst.rotations[i], negSrc, weight);
			} else {
				dst.rotations[i] = glm::slerp(dst.rotations[i], src.rotations[i], weight);
			}
			dst.scales[i] = glm::mix(dst.scales[i], src.scales[i], weight);
		}
	}

	void applyAdditive(const Pose& src, const Pose& ref, const Skeleton& skeleton,
	                   const BoneMask& mask, float weight, Pose& dst) const {
		for (size_t i = 0; i < dst.size() && i < src.size(); ++i) {
			if (!mask.affectsBone(int(i), skeleton)) continue;

			dst.positions[i] += (src.positions[i] - ref.positions[i]) * weight;

			glm::quat diff = src.rotations[i] * glm::inverse(ref.rotations[i]);
			dst.rotations[i] = glm::slerp(glm::quat(1,0,0,0), diff, weight) * dst.rotations[i];

			dst.scales[i] += (src.scales[i] - ref.scales[i]) * weight;
		}
	}

	std::vector<AnimationLayer> m_layers;
};

} // namespace tucano::anim
