#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <any>
#include <glm/glm.hpp>

// Animation graph node base.
// Inspired by Esoterica's Animation_RuntimeGraph_Node.

namespace tucano::anim { struct Pose; }
namespace tucano::animation { class BoneMask; }

namespace tucano::animation {

class GraphInstance;

// ── Value types ──

enum class PinType : uint8_t {
	Float,
	Bool,
	Int,
	Vector,
	Pose,
	BoneMask,
};

// ── Base node ──

struct GraphNode {
	std::string name;
	uint32_t id = 0;

	struct Pin {
		std::string name;
		PinType type;
		uint32_t sourceNodeId = 0;
		uint32_t sourcePinIndex = 0;
	};
	std::vector<Pin> inputs;
	std::vector<Pin> outputs;

	virtual ~GraphNode() = default;
	virtual void evaluate(GraphInstance& ctx, float deltaTime) = 0;
	virtual const char* typeName() const = 0;

	// Helpers for accessing inputs/outputs via GraphInstance
	void setOutputFloat(GraphInstance& ctx, uint32_t pinIdx, float v);
	void setOutputPose(GraphInstance& ctx, uint32_t pinIdx, const tucano::anim::Pose& v);
	float getInputFloat(GraphInstance& ctx, uint32_t pinIdx, float def = 0.0f) const;
	const tucano::anim::Pose* getInputPose(GraphInstance& ctx, uint32_t pinIdx) const;
};

// ── Graph definition (serializable) ──

struct GraphDefinition {
	std::string name;
	std::vector<std::unique_ptr<GraphNode>> nodes;

	struct ParameterSlot {
		std::string name;
		PinType type = PinType::Float;
		float defaultFloat = 0.0f;
	};
	std::vector<ParameterSlot> parameters;

	GraphNode* findNode(const std::string& nodeName) {
		for (auto& n : nodes) {
			if (n->name == nodeName) return n.get();
		}
		return nullptr;
	}
};

} // namespace tucano::animation
