#include "Animation/Graph/GraphInstance.h"
#include "Animation/Skeleton.h"

namespace tucano::animation {

void GraphInstance::setNodeFloat(uint32_t nodeId, uint32_t pinIdx, float v) {
	m_floatOutputs[key(nodeId, pinIdx)] = v;
}
float GraphInstance::getNodeFloat(uint32_t nodeId, uint32_t pinIdx, float def) const {
	auto it = m_floatOutputs.find(key(nodeId, pinIdx));
	return it != m_floatOutputs.end() ? it->second : def;
}

void GraphInstance::setNodePose(uint32_t nodeId, uint32_t pinIdx, std::shared_ptr<tucano::anim::Pose> v) {
	m_poseOutputs[key(nodeId, pinIdx)] = std::move(v);
}
const tucano::anim::Pose* GraphInstance::getNodePose(uint32_t nodeId, uint32_t pinIdx) const {
	auto it = m_poseOutputs.find(key(nodeId, pinIdx));
	return it != m_poseOutputs.end() ? it->second.get() : nullptr;
}

void GraphInstance::reset() {
	m_floatOutputs.clear();
	m_poseOutputs.clear();
	m_lastPose = nullptr;
}

const tucano::anim::Pose* GraphInstance::evaluate(float deltaTime) {
	if (!m_definition) return nullptr;

	for (auto& node : m_definition->nodes) {
		node->evaluate(*this, deltaTime);
	}

	// Return the last Pose output
	const tucano::anim::Pose* last = nullptr;
	for (auto& node : m_definition->nodes) {
		for (size_t i = 0; i < node->outputs.size(); ++i) {
			if (node->outputs[i].type == PinType::Pose) {
				last = getNodePose(node->id, static_cast<uint32_t>(i));
			}
		}
	}
	return last;
}

} // namespace tucano::animation
