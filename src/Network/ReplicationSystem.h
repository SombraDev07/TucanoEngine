#pragma once

#include "Network/NetworkManager.h"
#include "ECS/World.h"
#include "ECS/Components.h"

#include <functional>
#include <unordered_set>
#include <vector>

namespace tucano::net {

class ReplicationSystem {
public:
	static ReplicationSystem& instance() { static ReplicationSystem rs; return rs; }

	void setWorld(ecs::World* world) { m_world = world; }

	void markReplicated(uint32_t componentId) {
		m_replicatedComponents.insert(componentId);
	}

	void markReplicated(const char* name) {
		uint32_t id = ecs::ComponentRegistry::instance().find(name);
		if (id != ecs::kInvalidEntity) m_replicatedComponents.insert(id);
	}

	bool isReplicated(uint32_t componentId) const {
		return m_replicatedComponents.count(componentId) > 0;
	}

	void spawnOnClients(ecs::Entity e) {
		if (!NetworkManager::instance().isServer()) return;
		if (!m_world || !m_world->alive(e)) return;

		std::vector<uint8_t> buffer;
		buffer.push_back(0); // spawn opcode

		uint32_t eid = e;
		buffer.insert(buffer.end(), (uint8_t*)&eid, ((uint8_t*)&eid) + 4);

		serializeEntity(e, buffer);
		NetworkManager::instance().broadcastReliable(buffer.data(), (uint32_t)buffer.size());
	}

	void destroyOnClients(ecs::Entity e) {
		if (!NetworkManager::instance().isServer()) return;

		std::vector<uint8_t> buffer;
		buffer.push_back(1); // destroy opcode
		uint32_t eid = e;
		buffer.insert(buffer.end(), (uint8_t*)&eid, ((uint8_t*)&eid) + 4);

		NetworkManager::instance().broadcastReliable(buffer.data(), (uint32_t)buffer.size());
	}

	void tick() {
		if (!NetworkManager::instance().isServer() || !m_world) return;

		m_tickTimer += 1.0f / 30.0f;
		if (m_tickTimer < m_replicationInterval) return;
		m_tickTimer = 0;

		for (auto& arch : m_world->entities().archetypes()) {
			for (uint32_t compId : arch.comps) {
				if (!isReplicated(compId)) continue;

				for (size_t ci = 0; ci < arch.chunks.size(); ++ci) {
					for (uint32_t i = 0; i < arch.chunks[ci].count; ++i) {
						auto e = arch.chunks[ci].entities[i];
						std::vector<uint8_t> buffer;
						buffer.push_back(2); // update opcode
						uint32_t eid = e;
						buffer.insert(buffer.end(), (uint8_t*)&eid, ((uint8_t*)&eid) + 4);

						uint32_t cid = compId;
						buffer.insert(buffer.end(), (uint8_t*)&cid, ((uint8_t*)&cid) + 4);

						const ecs::ComponentDesc& desc = ecs::ComponentRegistry::instance().desc(compId);
						int slot = arch.slot(compId);
						if (slot < 0) continue;

						void* data = arch.column(uint32_t(ci), slot);
						data = (uint8_t*)data + i * desc.size;
						buffer.insert(buffer.end(), (uint8_t*)data, (uint8_t*)data + desc.size);

						NetworkManager::instance().broadcastUnreliable(buffer.data(), (uint32_t)buffer.size());
					}
				}
			}
		}
	}

	void handlePacket(const uint8_t* data, uint32_t size) {
		if (size < 5 || !m_world) return;
		uint8_t opcode = data[0];
		uint32_t eid = *(uint32_t*)(data + 1);

		if (opcode == 0) {
			ecs::Entity e = (ecs::Entity)eid;
			(void)e;
		} else if (opcode == 1) {
			ecs::Entity e = (ecs::Entity)eid;
			if (m_world->alive(e)) m_world->destroy(e);
		} else if (opcode == 2 && size >= 13) {
			ecs::Entity e = (ecs::Entity)eid;
			uint32_t compId = *(uint32_t*)(data + 5);
			uint32_t compSize = size - 9;

			void* comp = m_world->entities().get(e, compId);
			if (!comp) comp = m_world->entities().add(e, compId);
			if (comp) memcpy(comp, data + 9, compSize);
		}
	}

	void setInterval(float interval) { m_replicationInterval = interval; }

private:
	void serializeEntity(ecs::Entity e, std::vector<uint8_t>& out) {
		for (uint32_t compId : m_replicatedComponents) {
			if (!m_world->entities().has(e, compId)) continue;

			const auto& desc = ecs::ComponentRegistry::instance().desc(compId);
			void* data = m_world->entities().get(e, compId);
			if (!data) continue;

			out.insert(out.end(), (uint8_t*)&compId, ((uint8_t*)&compId) + 4);
			out.insert(out.end(), (uint8_t*)data, (uint8_t*)data + desc.size);
		}
	}

	ecs::World* m_world = nullptr;
	std::unordered_set<uint32_t> m_replicatedComponents;
	float m_replicationInterval = 1.0f / 30.0f;
	float m_tickTimer = 0;
};

} // namespace tucano::net
