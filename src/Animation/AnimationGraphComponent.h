#pragma once

#include "Animation/Graph/GraphInstance.h"
#include "Animation/Graph/GraphNode.h"
#include "Animation/Graph/Nodes/Node_AnimationClip.h"
#include "Animation/Graph/Nodes/Node_Blend1D.h"
#include "Animation/Graph/Nodes/Node_Layers.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationClip.h"

#include <memory>
#include <vector>

// ECS component: holds an animation graph instance for an entity.

namespace tucano::animation {

struct AnimationGraphComponent {
	std::unique_ptr<GraphDefinition> definition;
	std::unique_ptr<GraphInstance> instance;
	const tucano::anim::Skeleton* skeleton = nullptr;

	// Convenience: build a simple locomotion graph (idle / walk / run)
	static AnimationGraphComponent createLocomotion(
		const tucano::anim::AnimationClip* idle,
		const tucano::anim::AnimationClip* walk,
		const tucano::anim::AnimationClip* run,
		const tucano::anim::Skeleton* skeleton);

	// Convenience: build a simple single-clip graph
	static AnimationGraphComponent createSingleClip(
		const tucano::anim::AnimationClip* clip,
		const tucano::anim::Skeleton* skeleton);

	// Evaluate the graph. Returns true if a pose was produced.
	bool evaluate(float deltaTime, float speed, tucano::anim::Pose& outPose);
};

} // namespace tucano::animation
