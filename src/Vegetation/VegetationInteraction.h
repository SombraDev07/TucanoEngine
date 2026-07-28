#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>

namespace tucano::veg {

struct InteractionPoint {
	glm::vec3 position{0};
	float radius = 1.0f;
	float strength = 1.0f;
	float falloff = 2.0f;
	bool permanent = false;
	float lifetime = 0;
};

struct InteractionForce {
	glm::vec3 position{0};
	glm::vec3 direction{0, 0, 1};
	float radius = 5.0f;
	float strength = 1.0f;
	float duration = 1.0f;
	float elapsed = 0;
};

struct VegetationInteractionParams {
	float grassBendRadius = 2.0f;
	float grassBendStrength = 0.8f;
	float grassRecoverySpeed = 2.0f;
	float bushBendRadius = 3.0f;
	float bushBendStrength = 0.3f;
	float globalForceStrength = 1.0f;
	bool enablePlayerInteraction = true;
	bool enablePhysicsInteraction = true;
	bool enableWindForces = true;
};

struct InstanceInteractionState {
	float bendAmount = 0;
	float bendDirection = 0;
	float recoveryTimer = 0;
	uint32_t lastFrameTouched = 0;
};

class VegetationInteraction {
public:
	static VegetationInteraction& instance() { static VegetationInteraction vi; return vi; }

	void configure(const VegetationInteractionParams& p) { m_params = p; }
	VegetationInteractionParams& params() { return m_params; }

	void addInteractionPoint(const glm::vec3& pos, float radius, float strength) {
		InteractionPoint pt;
		pt.position = pos;
		pt.radius = radius;
		pt.strength = strength;
		pt.falloff = radius * 0.7f;
		m_points.push_back(pt);
	}

	void addForce(const glm::vec3& pos, const glm::vec3& dir, float radius, float strength, float duration) {
		m_forces.push_back({pos, dir, radius, strength, duration, 0});
	}

	void clearInteractionPoints() { m_points.clear(); }

	void update(float dt, uint32_t frameCount) {
		for (auto it = m_forces.begin(); it != m_forces.end(); ) {
			it->elapsed += dt;
			if (it->elapsed >= it->duration) {
				it = m_forces.erase(it);
			} else {
				++it;
			}
		}

		m_frameCount = frameCount;
	}

	float computeBendAmount(const glm::vec3& instancePos, float flexibility, float height) const {
		float totalBend = 0;

		for (auto& pt : m_points) {
			if (!m_params.enablePlayerInteraction) break;
			float dist = glm::distance(glm::vec2(instancePos.x, instancePos.z),
			                           glm::vec2(pt.position.x, pt.position.z));
			float radius = pt.radius * m_params.grassBendRadius;
			if (dist > radius) continue;

			float factor = 1.0f - (dist / radius);
			factor = std::pow(factor, 2.0f);
			totalBend += factor * pt.strength * flexibility * m_params.grassBendStrength;
		}

		for (auto& f : m_forces) {
			float dist = glm::distance(instancePos, f.position);
			if (dist > f.radius) continue;

			float factor = 1.0f - (dist / f.radius);
			float timeFactor = 1.0f - (f.elapsed / f.duration);
			totalBend += factor * f.strength * timeFactor;
		}

		return std::clamp(totalBend, 0.0f, 2.0f) * height * flexibility;
	}

	float computeBendDirection(const glm::vec3& instancePos) const {
		glm::vec2 totalDir(0, 0);
		float totalWeight = 0;

		for (auto& pt : m_points) {
			float dist = glm::distance(glm::vec2(instancePos.x, instancePos.z),
			                           glm::vec2(pt.position.x, pt.position.z));
			if (dist > pt.radius * m_params.grassBendRadius) continue;

			glm::vec2 dir = glm::normalize(glm::vec2(instancePos.x - pt.position.x,
			                                         instancePos.z - pt.position.z));
			float weight = 1.0f - (dist / (pt.radius * m_params.grassBendRadius));
			totalDir += dir * weight;
			totalWeight += weight;
		}

		for (auto& f : m_forces) {
			float dist = glm::distance(instancePos, f.position);
			if (dist > f.radius) continue;

			glm::vec2 dir = glm::normalize(glm::vec2(f.direction.x, f.direction.z));
			float weight = 1.0f - (dist / f.radius);
			totalDir += dir * weight;
			totalWeight += weight;
		}

		if (totalWeight < 0.001f) return 0;
		return std::atan2(totalDir.y, totalDir.x);
	}

	void setInteractionState(uint32_t instanceIndex, const InstanceInteractionState& state) {
		m_states[instanceIndex] = state;
	}

	InstanceInteractionState* getState(uint32_t instanceIndex) {
		auto it = m_states.find(instanceIndex);
		return it != m_states.end() ? &it->second : nullptr;
	}

private:
	VegetationInteractionParams m_params;
	std::vector<InteractionPoint> m_points;
	std::vector<InteractionForce> m_forces;
	std::unordered_map<uint32_t, InstanceInteractionState> m_states;
	uint32_t m_frameCount = 0;
};

} // namespace tucano::veg
