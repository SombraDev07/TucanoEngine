#include "Animation/Graph/GraphNode.h"
#include "Animation/Graph/GraphInstance.h"
#include "Animation/Skeleton.h"

namespace tucano::animation {

void GraphNode::setOutputFloat(GraphInstance& ctx, uint32_t pinIdx, float v) {
	ctx.setNodeFloat(id, pinIdx, v);
}
void GraphNode::setOutputPose(GraphInstance& ctx, uint32_t pinIdx, const tucano::anim::Pose& v) {
	auto copy = std::make_shared<tucano::anim::Pose>(v);
	ctx.setNodePose(id, pinIdx, std::move(copy));
}

float GraphNode::getInputFloat(GraphInstance& ctx, uint32_t pinIdx, float def) const {
	if (pinIdx >= inputs.size()) return def;
	const auto& pin = inputs[pinIdx];
	return ctx.getNodeFloat(pin.sourceNodeId, pin.sourcePinIndex, def);
}

const tucano::anim::Pose* GraphNode::getInputPose(GraphInstance& ctx, uint32_t pinIdx) const {
	if (pinIdx >= inputs.size()) return nullptr;
	const auto& pin = inputs[pinIdx];
	return ctx.getNodePose(pin.sourceNodeId, pin.sourcePinIndex);
}

} // namespace tucano::animation
