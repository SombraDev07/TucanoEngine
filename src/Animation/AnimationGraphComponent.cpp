#include "Animation/AnimationGraphComponent.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationClip.h"

namespace tucano::animation {

AnimationGraphComponent AnimationGraphComponent::createSingleClip(
	const tucano::anim::AnimationClip* clip,
	const tucano::anim::Skeleton* skeleton)
{
	AnimationGraphComponent comp;
	comp.skeleton = skeleton;

	auto def = std::make_unique<GraphDefinition>();
	def->name = clip ? clip->name() : "Empty";

	auto* node = new Node_AnimationClip();
	node->id = 0;
	node->name = "Clip";
	node->clip = clip;
	node->skeleton = skeleton;
	def->nodes.emplace_back(node);

	comp.definition = std::move(def);
	comp.instance = std::make_unique<GraphInstance>(comp.definition.get());
	return comp;
}

AnimationGraphComponent AnimationGraphComponent::createLocomotion(
	const tucano::anim::AnimationClip* idle,
	const tucano::anim::AnimationClip* walk,
	const tucano::anim::AnimationClip* run,
	const tucano::anim::Skeleton* skeleton)
{
	AnimationGraphComponent comp;
	comp.skeleton = skeleton;

	auto def = std::make_unique<GraphDefinition>();
	def->name = "Locomotion";

	// Idle clip node
	auto* nodeIdle = new Node_AnimationClip();
	nodeIdle->id = 0;
	nodeIdle->name = "Idle";
	nodeIdle->clip = idle;
	nodeIdle->skeleton = skeleton;
	def->nodes.emplace_back(nodeIdle);

	// Walk clip node
	auto* nodeWalk = new Node_AnimationClip();
	nodeWalk->id = 1;
	nodeWalk->name = "Walk";
	nodeWalk->clip = walk;
	nodeWalk->skeleton = skeleton;
	def->nodes.emplace_back(nodeWalk);

	// Run clip node
	auto* nodeRun = new Node_AnimationClip();
	nodeRun->id = 2;
	nodeRun->name = "Run";
	nodeRun->clip = run;
	nodeRun->skeleton = skeleton;
	def->nodes.emplace_back(nodeRun);

	// Blend1D node (blends idle/walk/run by speed)
	auto* nodeBlend = new Node_Blend1D(3);
	nodeBlend->id = 3;
	nodeBlend->name = "SpeedBlend";
	nodeBlend->skeleton = skeleton;
	nodeBlend->paramMin = 0.0f;
	nodeBlend->paramMax = 1.0f;
	nodeBlend->blendInputs = {{0.0f}, {0.5f}, {1.0f}};

	// Connect idle -> blend input 0 (pin 1 on blend)
	nodeBlend->inputs[1].sourceNodeId = 0;
	nodeBlend->inputs[1].sourcePinIndex = 0;
	// Connect walk -> blend input 1 (pin 2 on blend)
	nodeBlend->inputs[2].sourceNodeId = 1;
	nodeBlend->inputs[2].sourcePinIndex = 0;
	// Connect run -> blend input 2 (pin 3 on blend)
	nodeBlend->inputs[3].sourceNodeId = 2;
	nodeBlend->inputs[3].sourcePinIndex = 0;

	def->nodes.emplace_back(nodeBlend);
	def->parameters.push_back({"Speed", PinType::Float, 0.5f});

	comp.definition = std::move(def);
	comp.instance = std::make_unique<GraphInstance>(comp.definition.get());
	return comp;
}

bool AnimationGraphComponent::evaluate(float deltaTime, float speed, tucano::anim::Pose& outPose) {
	if (!instance || !definition) return false;

	// Update blend parameter from speed (mapped to 0..1)
	instance->controller().setFloat("Speed", speed);

	const auto* result = instance->evaluate(deltaTime);
	if (result) {
		outPose = *result;
		return true;
	}
	return false;
}

} // namespace tucano::animation
