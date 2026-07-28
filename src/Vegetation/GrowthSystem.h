#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

namespace tucano::veg {

struct GrowthConfig {
	float growthRate = 0.1f;
	float maxGrowth = 2.0f;
	float minScale = 0.2f;
	float seasonalGrowth = true;
	float growthVariance = 0.3f;
	bool enabled = true;
};

struct GrowthState {
	float currentScale = 0.2f;
	float targetScale = 1.0f;
	float growthProgress = 0;
	bool mature = false;
	float age = 0;
};

class GrowthSystem {
public:
	static GrowthSystem& instance() { static GrowthSystem gs; return gs; }

	void configure(const GrowthConfig& cfg) { m_config = cfg; }
	GrowthConfig& config() { return m_config; }

	void registerInstance(uint32_t instanceIndex, uint32_t typeId) {
		GrowthState state;
		state.currentScale = m_config.minScale;
		state.targetScale = 0.8f + (float(typeId * 127 + instanceIndex * 311) / 100000.0f) * m_config.growthVariance;
		state.growthProgress = 0;
		m_states[instanceIndex] = state;
	}

	void update(float dt) {
		if (!m_config.enabled) return;

		for (auto& [idx, state] : m_states) {
			if (state.mature) continue;

			state.age += dt;
			state.growthProgress += dt * m_config.growthRate;
			state.currentScale = m_config.minScale +
				(state.targetScale - m_config.minScale) * std::min(state.growthProgress, 1.0f);

			if (state.growthProgress >= 1.0f) {
				state.currentScale = state.targetScale;
				state.mature = true;
			}
		}
	}

	float getScale(uint32_t instanceIndex) const {
		auto it = m_states.find(instanceIndex);
		return it != m_states.end() ? it->second.currentScale : 1.0f;
	}

	bool isMature(uint32_t instanceIndex) const {
		auto it = m_states.find(instanceIndex);
		return it != m_states.end() ? it->second.mature : true;
	}

	void removeInstance(uint32_t instanceIndex) {
		m_states.erase(instanceIndex);
	}

	void clear() { m_states.clear(); }
	size_t instanceCount() const { return m_states.size(); }

private:
	GrowthConfig m_config;
	std::unordered_map<uint32_t, GrowthState> m_states;
};

class DestructionSystem {
public:
	static DestructionSystem& instance() { static DestructionSystem ds; return ds; }

	void markDestroyed(uint32_t instanceIndex) {
		m_destroyed.insert(instanceIndex);
		m_destroyTimers[instanceIndex] = 1.0f;
	}

	bool isDestroyed(uint32_t instanceIndex) const {
		return m_destroyed.count(instanceIndex) > 0;
	}

	void update(float dt) {
		for (auto it = m_destroyTimers.begin(); it != m_destroyTimers.end(); ) {
			it->second -= dt;
			if (it->second <= 0) {
				m_destroyed.erase(it->first);
				it = m_destroyTimers.erase(it);
			} else {
				++it;
			}
		}
	}

	float getDestroyProgress(uint32_t instanceIndex) const {
		auto it = m_destroyTimers.find(instanceIndex);
		return it != m_destroyTimers.end() ? 1.0f - it->second : 0;
	}

	void clear() {
		m_destroyed.clear();
		m_destroyTimers.clear();
	}

	bool hasDestroyed() const { return !m_destroyed.empty(); }

private:
	std::unordered_set<uint32_t> m_destroyed;
	std::unordered_map<uint32_t, float> m_destroyTimers;
};

} // namespace tucano::veg
