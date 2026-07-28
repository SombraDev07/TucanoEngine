#pragma once

#include "Network/NetworkManager.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::net {

class RPCSystem {
public:
	using RPCHandler = std::function<void(net::ClientId sender, const uint8_t* data, uint32_t size)>;

	static RPCSystem& instance() { static RPCSystem rs; return rs; }

	void registerRPC(uint32_t rpcId, RPCHandler handler) {
		m_handlers[rpcId] = std::move(handler);
	}

	void unregisterRPC(uint32_t rpcId) {
		m_handlers.erase(rpcId);
	}

	void callServer(uint32_t rpcId, const uint8_t* data, uint32_t size, bool reliable = true) {
		if (!NetworkManager::instance().isClient()) return;

		std::vector<uint8_t> buffer(5 + size);
		buffer[0] = (uint8_t)net::MessageType::RPC;
		memcpy(buffer.data() + 1, &rpcId, 4);
		if (size > 0) memcpy(buffer.data() + 5, data, size);

		NetworkManager::instance().sendToServer(buffer.data(), (uint32_t)buffer.size(), reliable);
	}

	void callClient(net::ClientId target, uint32_t rpcId, const uint8_t* data, uint32_t size, bool = true) {
		if (!NetworkManager::instance().isServer()) return;

		std::vector<uint8_t> buffer(5 + size);
		buffer[0] = (uint8_t)net::MessageType::RPC;
		memcpy(buffer.data() + 1, &rpcId, 4);
		if (size > 0) memcpy(buffer.data() + 5, data, size);

		NetworkManager::instance().sendReliable(target, buffer.data(), (uint32_t)buffer.size());
	}

	void broadcastRPC(uint32_t rpcId, const uint8_t* data, uint32_t size, net::ClientId exclude = kInvalidClient, bool = true) {
		if (!NetworkManager::instance().isServer()) return;

		std::vector<uint8_t> buffer(5 + size);
		buffer[0] = (uint8_t)net::MessageType::RPC;
		memcpy(buffer.data() + 1, &rpcId, 4);
		if (size > 0) memcpy(buffer.data() + 5, data, size);

		NetworkManager::instance().broadcastReliable(buffer.data(), (uint32_t)buffer.size(), exclude);
	}

	void handlePacket(net::ClientId sender, const uint8_t* data, uint32_t size) {
		if (size < 5) return;
		uint32_t rpcId = *(uint32_t*)(data + 1);
		auto it = m_handlers.find(rpcId);
		if (it != m_handlers.end()) {
			it->second(sender, data + 5, size - 5);
		}
	}

	void handleIncoming(net::ClientId sender, const uint8_t* data, uint32_t size) {
		if (size == 0) return;
		uint8_t msgType = data[0];
		if (msgType == (uint8_t)net::MessageType::RPC) {
			handlePacket(sender, data, size);
		} else if (msgType == (uint8_t)net::MessageType::Replication) {
			ReplicationSystem::instance().handlePacket(data + 1, size - 1);
		}
	}

private:
	std::unordered_map<uint32_t, RPCHandler> m_handlers;
};

} // namespace tucano::net
