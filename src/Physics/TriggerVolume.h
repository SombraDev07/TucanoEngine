#pragma once

#include "ECS/ComponentTypes.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Core/EventBus.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace tucano::physics {

struct TriggerVolume {
	enum Shape { Box, Sphere };

	Shape shape = Box;
	glm::vec3 center{0};
	glm::vec3 halfExtent{0.5f};
	float radius = 0.5f;
	std::string tag;
	std::unordered_set<ecs::Entity> inside;

	std::function<void(ecs::Entity)> onEnter;
	std::function<void(ecs::Entity)> onExit;

	float cooldown = 0.0f; // seconds between re-trigger
	std::unordered_map<ecs::Entity, float> lastEvents;
};

class TriggerSystem {
public:
	static TriggerSystem& instance() { static TriggerSystem ts; return ts; }

	void setWorld(ecs::World* world) { m_world = world; }

	uint32_t createBox(const glm::vec3& center, const glm::vec3& halfExtent, const std::string& tag = "") {
		m_volumes.push_back({TriggerVolume::Box, center, halfExtent, 0, tag});
		return uint32_t(m_volumes.size() - 1);
	}

	uint32_t createSphere(const glm::vec3& center, float radius, const std::string& tag = "") {
		m_volumes.push_back({TriggerVolume::Sphere, center, {}, radius, tag});
		return uint32_t(m_volumes.size() - 1);
	}

	void remove(uint32_t id) {
		if (id < m_volumes.size()) {
			if (m_volumes[id].onExit) {
				for (auto e : m_volumes[id].inside)
					m_volumes[id].onExit(e);
			}
			m_volumes[id].inside.clear();
			m_volumes.erase(m_volumes.begin() + id);
		}
	}

	TriggerVolume* get(uint32_t id) {
		return id < m_volumes.size() ? &m_volumes[id] : nullptr;
	}

	void setOnEnter(uint32_t id, std::function<void(ecs::Entity)> fn) {
		if (id < m_volumes.size()) m_volumes[id].onEnter = std::move(fn);
	}

	void setOnExit(uint32_t id, std::function<void(ecs::Entity)> fn) {
		if (id < m_volumes.size()) m_volumes[id].onExit = std::move(fn);
	}

	void update(float dt) {
		if (!m_world) return;

		for (auto& vol : m_volumes) {
			vol.cooldown -= dt;

			for (auto& arch : m_world->entities().archetypes()) {
				int slot = arch.slot(ecs::kCompTransform);
				if (slot < 0) continue;

				for (size_t ci = 0; ci < arch.chunks.size(); ++ci) {
					for (uint32_t i = 0; i < arch.chunks[ci].count; ++i) {
						auto e = arch.chunks[ci].entities[i];
						auto* t = static_cast<ecs::TransformComponent*>(
							arch.column(uint32_t(ci), slot));
						t += i;

						bool inside = vol.shape == TriggerVolume::Box
							? isInsideBox(t->position, vol.center, vol.halfExtent)
							: isInsideSphere(t->position, vol.center, vol.radius);

						bool wasInside = vol.inside.count(e) > 0;

						if (inside && !wasInside) {
							vol.inside.insert(e);
							if (vol.onEnter) { vol.onEnter(e); }
							EventBus::instance().emit("trigger_enter");
						} else if (!inside && wasInside) {
							vol.inside.erase(e);
							if (vol.onExit) { vol.onExit(e); }
							EventBus::instance().emit("trigger_exit");
						}
					}
				}
			}
		}
	}

	size_t count() const { return m_volumes.size(); }

private:
	static bool isInsideBox(const glm::vec3& p, const glm::vec3& c, const glm::vec3& he) {
		return p.x >= c.x - he.x && p.x <= c.x + he.x &&
		       p.y >= c.y - he.y && p.y <= c.y + he.y &&
		       p.z >= c.z - he.z && p.z <= c.z + he.z;
	}

	static bool isInsideSphere(const glm::vec3& p, const glm::vec3& c, float r) {
		return glm::distance(p, c) <= r;
	}

	ecs::World* m_world = nullptr;
	std::vector<TriggerVolume> m_volumes;
};

} // namespace tucano::physics
