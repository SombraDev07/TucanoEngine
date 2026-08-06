#pragma once

#include "Animation/Graph/GraphNode.h"
#include "Animation/Graph/GraphInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/BoneMask.h"

// Layered blending: applies multiple poses on top of a base pose using bone masks.
// Input 0: Base Pose
// Input 1,3,5...: Overlay Poses
// Input 2,4,6...: BoneMask for the preceding overlay

namespace tucano::animation {

struct Node_Layers : GraphNode {
	const tucano::anim::Skeleton* skeleton = nullptr;

	Node_Layers(uint32_t numLayers = 1) {
		inputs.push_back({"Base", PinType::Pose, 0, 0});
		for (uint32_t i = 0; i < numLayers; ++i) {
			inputs.push_back({"Overlay" + std::to_string(i), PinType::Pose, 0, 0});
			inputs.push_back({"Mask" + std::to_string(i), PinType::BoneMask, 0, 0});
		}
		outputs.push_back({"Pose", PinType::Pose, 0, 0});
	}

	const char* typeName() const override { return "Layers"; }

	void evaluate(GraphInstance& ctx, float) override {
		auto out = std::make_shared<tucano::anim::Pose>();

		// Start with base pose (pin 0)
		const auto* base = getInputPose(ctx, 0);
		if (base) {
			*out = *base;
		} else if (skeleton) {
			out->resize(skeleton->boneCount());
		}

		// Apply each overlay with its mask
		for (uint32_t layer = 0; layer * 2 + 2 < static_cast<uint32_t>(inputs.size()); ++layer) {
			uint32_t poseIdx = layer * 2 + 1;
			uint32_t maskIdx = layer * 2 + 2;
			const auto* overlay = getInputPose(ctx, poseIdx);
			if (!overlay) continue;

			// Full-weight blend for now (mask support for later).
			size_t n = std::min(out->size(), overlay->size());
			for (size_t i = 0; i < n; ++i) {
				tucano::anim::AnimationClip::blend(*out, *overlay, 0.5f, *out);
			}
		}

		setOutputPose(ctx, 0, *out);
	}
};

} // namespace tucano::animation
