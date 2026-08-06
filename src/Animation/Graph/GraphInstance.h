#pragma once

#include "Animation/Graph/GraphNode.h"

#include <any>
#include <memory>
#include <unordered_map>

// Runtime animation graph instance.

namespace tucano::anim { struct Pose; }

namespace tucano::animation {

class GraphController {
public:
	void setFloat(const std::string& name, float v) { m_floats[name] = v; }
	float getFloat(const std::string& name, float d = 0.0f) const { auto it = m_floats.find(name); return it != m_floats.end() ? it->second : d; }
	void clear() { m_floats.clear(); }

private:
	std::unordered_map<std::string, float> m_floats;
};

class GraphInstance {
public:
	GraphInstance() = default;
	explicit GraphInstance(const GraphDefinition* def) : m_definition(def) {}

	void setDefinition(const GraphDefinition* def) { m_definition = def; reset(); }
	const GraphDefinition* definition() const { return m_definition; }
	GraphController& controller() { return m_controller; }

	// Evaluate full graph, returns the last Pose output (or nullptr).
	const tucano::anim::Pose* evaluate(float deltaTime);

	// Storage for node outputs during evaluation.
	void  setNodeFloat(uint32_t nodeId, uint32_t pinIdx, float v);
	float getNodeFloat(uint32_t nodeId, uint32_t pinIdx, float def = 0.0f) const;

	void  setNodePose(uint32_t nodeId, uint32_t pinIdx, std::shared_ptr<tucano::anim::Pose> v);
	const tucano::anim::Pose* getNodePose(uint32_t nodeId, uint32_t pinIdx) const;

	void reset();

private:
	const GraphDefinition* m_definition = nullptr;
	GraphController m_controller;

	// Float outputs: (nodeId, pinIdx) -> value
	std::unordered_map<uint64_t, float> m_floatOutputs;
	// Pose outputs: (nodeId, pinIdx) -> shared_ptr<Pose>
	std::unordered_map<uint64_t, std::shared_ptr<tucano::anim::Pose>> m_poseOutputs;

	const tucano::anim::Pose* m_lastPose = nullptr;
	uint32_t m_lastPoseOwner = 0;

	static uint64_t key(uint32_t nodeId, uint32_t pinIdx) {
		return (static_cast<uint64_t>(nodeId) << 32) | pinIdx;
	}
};

} // namespace tucano::animation
