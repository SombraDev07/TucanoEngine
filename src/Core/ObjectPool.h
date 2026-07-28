#pragma once

#include "ECS/World.h"
#include "ECS/Components.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace tucano {

class ObjectPool {
public:
	static ObjectPool& instance() { static ObjectPool p; return p; }

	void setWorld(ecs::World* world) { m_world = world; }

	void prewarm(const std::string& templateName, uint32_t count) {
		if (!m_world) return;
		auto& pool = m_pools[templateName];
		pool.resize(pool.size() + count);
		for (uint32_t i = 0; i < count; ++i) {
			auto e = m_world->instantiate(templateName);
			if (e != ecs::kInvalidEntity) {
				pool.push_back(e);
				if (auto* t = m_world->get<ecs::TransformComponent>(e)) {
					t->position = {0, -9999, 0}; // hide offscreen
				}
			}
		}
	}

	ecs::Entity acquire(const std::string& templateName) {
		auto& pool = m_pools[templateName];
		if (pool.empty()) {
			prewarm(templateName, 1);
		}
		if (pool.empty()) return ecs::kInvalidEntity;
		auto e = pool.back();
		pool.pop_back();
		return e;
	}

	void release(const std::string& templateName, ecs::Entity e) {
		if (!m_world || !m_world->alive(e)) return;
		if (auto* t = m_world->get<ecs::TransformComponent>(e)) {
			t->position = {0, -9999, 0};
		}
		m_pools[templateName].push_back(e);
	}

	uint32_t available(const std::string& templateName) const {
		auto it = m_pools.find(templateName);
		return it != m_pools.end() ? uint32_t(it->second.size()) : 0;
	}

	void clear(const std::string& templateName = "") {
		if (templateName.empty()) {
			for (auto& [_, pool] : m_pools) {
				for (auto e : pool) { if (m_world) m_world->destroy(e); }
				pool.clear();
			}
			m_pools.clear();
		} else {
			auto it = m_pools.find(templateName);
			if (it != m_pools.end()) {
				for (auto e : it->second) { if (m_world) m_world->destroy(e); }
				m_pools.erase(it);
			}
		}
	}

private:
	ecs::World* m_world = nullptr;
	std::unordered_map<std::string, std::vector<ecs::Entity>> m_pools;
};

} // namespace tucano
