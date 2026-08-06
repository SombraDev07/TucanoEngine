#pragma once

#include "Animation/Graph/GraphNode.h"
#include "Animation/Graph/GraphInstance.h"
#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"

#include <cmath>

// Samples an AnimationClip at a given time and outputs a Pose.

namespace tucano::animation {

struct Node_AnimationClip : GraphNode {
	const tucano::anim::AnimationClip* clip = nullptr;
	const tucano::anim::Skeleton* skeleton = nullptr;

	Node_AnimationClip() {
		inputs.push_back({"Time", PinType::Float, 0, 0});
		outputs.push_back({"Pose", PinType::Pose, 0, 0});
	}

	const char* typeName() const override { return "AnimationClip"; }

	void evaluate(GraphInstance& ctx, float) override {
		float time = getInputFloat(ctx, 0, 0.0f);
		auto out = std::make_shared<tucano::anim::Pose>();

		if (skeleton) {
			out->resize(skeleton->boneCount());
			for (size_t i = 0; i < skeleton->boneCount(); ++i) {
				const auto& bones = skeleton->bones();
				out->positions[i] = bones[i].localPosition;
				out->rotations[i] = bones[i].localRotation;
				out->scales[i] = bones[i].localScale;
			}
		}

		if (clip && skeleton) {
			float t = std::fmod(std::max(0.0f, time), clip->duration());
			clip->sample(t, tucano::anim::WrapMode::Loop, *out);
		}

		setOutputPose(ctx, 0, *out);
	}
};

} // namespace tucano::animation
