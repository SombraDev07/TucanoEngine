#pragma once

#include "Animation/Graph/GraphNode.h"
#include "Animation/Graph/GraphInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationClip.h"

#include <cmath>
#include <algorithm>

// Blends N poses based on a 1D parameter value.

namespace tucano::animation {

struct Node_Blend1D : GraphNode {
	// Parameter range for blending. Clips are positioned at evenly spaced points.
	float paramMin = 0.0f;
	float paramMax = 1.0f;

	// Per-input config (set during graph build)
	struct BlendInput {
		float paramPos = 0.0f; // position in 0..1 range for this clip
	};
	std::vector<BlendInput> blendInputs;

	const tucano::anim::Skeleton* skeleton = nullptr;

	Node_Blend1D(uint32_t numInputs = 2) {
		inputs.push_back({"Param", PinType::Float, 0, 0}); // pin 0 = blend parameter
		for (uint32_t i = 0; i < numInputs; ++i) {
			inputs.push_back({"Pose" + std::to_string(i), PinType::Pose, 0, 0});
		}
		outputs.push_back({"Pose", PinType::Pose, 0, 0});
	}

	const char* typeName() const override { return "Blend1D"; }

	void evaluate(GraphInstance& ctx, float) override {
		float param = getInputFloat(ctx, 0, 0.5f);

		auto out = std::make_shared<tucano::anim::Pose>();
		if (skeleton) out->resize(skeleton->boneCount());

		// Find the two nearest clips and blend
		int leftIdx = -1, rightIdx = -1;
		float leftPos = 0.0f, rightPos = 1.0f;
		for (size_t i = 0; i < blendInputs.size() && (i + 1) < inputs.size(); ++i) {
			float pos = blendInputs[i].paramPos;
			if (pos <= param && (leftIdx < 0 || pos > leftPos)) {
				leftIdx = static_cast<int>(i);
				leftPos = pos;
			}
			if (pos >= param && (rightIdx < 0 || pos < rightPos)) {
				rightIdx = static_cast<int>(i);
				rightPos = pos;
			}
		}

		if (leftIdx >= 0 && rightIdx >= 0 && leftIdx != rightIdx) {
			const auto* poseA = getInputPose(ctx, static_cast<uint32_t>(leftIdx + 1));
			const auto* poseB = getInputPose(ctx, static_cast<uint32_t>(rightIdx + 1));
			float range = rightPos - leftPos;
			float weight = range > 0.001f ? (param - leftPos) / range : 0.0f;
			if (poseA && poseB) {
				tucano::anim::AnimationClip::blend(*poseA, *poseB, weight, *out);
			} else if (poseA) {
				*out = *poseA;
			} else if (poseB) {
				*out = *poseB;
			}
		} else if (leftIdx >= 0) {
			const auto* pose = getInputPose(ctx, static_cast<uint32_t>(leftIdx + 1));
			if (pose) *out = *pose;
		} else if (rightIdx >= 0) {
			const auto* pose = getInputPose(ctx, static_cast<uint32_t>(rightIdx + 1));
			if (pose) *out = *pose;
		}

		setOutputPose(ctx, 0, *out);
	}
};

} // namespace tucano::animation
