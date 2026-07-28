#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

namespace tucano::net {

using ClientId = uint32_t;
inline constexpr ClientId kInvalidClient = 0xFFFFFFFF;
inline constexpr ClientId kServerId = 0;

struct NetworkConfig {
	uint16_t port = 27015;
	uint32_t maxClients = 32;
	uint32_t maxPacketSize = 1200;
	float tickRate = 30.0f;
	bool enableEncryption = true;
	std::string encryptionKey = "tucano-default-key";
};

enum class MessageType : uint8_t {
	Reliable = 0,
	Unreliable = 1,
	Connect = 2,
	Disconnect = 3,
	Ping = 4,
	Pong = 5,
	RPC = 6,
	Replication = 7,
};

struct PacketHeader {
	uint8_t messageType;
	uint8_t channel;
	uint16_t sequence;
	uint32_t ack;
	uint32_t ackBitfield;
	uint16_t payloadSize;
};

struct PendingMessage {
	std::vector<uint8_t> data;
	uint16_t sequence;
	float sendTime;
	uint8_t retries;
};

struct ClientState {
	sockaddr_in address;
	uint16_t nextSequence = 0;
	uint16_t lastAckedSequence = 0;
	uint32_t lastAckBitfield = 0;
	float lastReceived = 0;
	bool connected = false;

	std::queue<PendingMessage> reliableQueue;
	std::vector<uint16_t> receivedSequences;
};

class NetworkManager {
public:
	using ConnectionCallback = std::function<void(ClientId)>;
	using DataCallback = std::function<void(ClientId, const uint8_t*, uint32_t)>;

	static NetworkManager& instance() { static NetworkManager nm; return nm; }

	void configure(const NetworkConfig& cfg) { m_config = cfg; }

	bool startServer() {
		if (m_running) return false;
		m_mode = Mode::Server;
		return initSocket();
	}

	bool connectClient(const std::string& host) {
		if (m_running) return false;
		m_mode = Mode::Client;
		if (!initSocket()) return false;

		m_serverAddress.sin_family = AF_INET;
		m_serverAddress.sin_port = htons(m_config.port);
		inet_pton(AF_INET, host.c_str(), &m_serverAddress.sin_addr);

		m_clients[0] = {m_serverAddress};
		m_clients[0].connected = true;

		sendConnect();
		return true;
	}

	void stop() {
		m_running = false;
		if (m_sendThread.joinable()) m_sendThread.join();
		if (m_recvThread.joinable()) m_recvThread.join();
		if (m_socket != INVALID_SOCKET) { closesocket(m_socket); m_socket = INVALID_SOCKET; }
		WSACleanup();
		m_clients.clear();
	}

	void update(float dt) {
		m_timer += dt;
		float tickInterval = 1.0f / m_config.tickRate;
		if (m_timer < tickInterval) return;
		m_timer -= tickInterval;

		processReliableResends(tickInterval);
		checkTimeouts();
		if (m_mode == Mode::Server) sendPings();
	}

	void sendReliable(ClientId target, const uint8_t* data, uint32_t size, uint8_t channel = 0) {
		queueMessage(target, MessageType::Reliable, data, size, channel, true);
	}

	void sendUnreliable(ClientId target, const uint8_t* data, uint32_t size, uint8_t channel = 0) {
		queueMessage(target, MessageType::Unreliable, data, size, channel, false);
	}

	void broadcastReliable(const uint8_t* data, uint32_t size, ClientId exclude = kInvalidClient, uint8_t channel = 0) {
		for (auto& [id, client] : m_clients) {
			if (id != exclude && id != kServerId) sendReliable(id, data, size, channel);
		}
	}

	void broadcastUnreliable(const uint8_t* data, uint32_t size, ClientId exclude = kInvalidClient, uint8_t channel = 0) {
		for (auto& [id, client] : m_clients) {
			if (id != exclude && id != kServerId) sendUnreliable(id, data, size, channel);
		}
	}

	void sendToServer(const uint8_t* data, uint32_t size, bool reliable = true) {
		if (m_mode != Mode::Client || m_clients.empty()) return;
		queueMessage(0, reliable ? MessageType::Reliable : MessageType::Unreliable, data, size, 0, reliable);
	}

	bool isRunning() const { return m_running; }
	bool isServer() const { return m_mode == Mode::Server; }
	bool isClient() const { return m_mode == Mode::Client; }
	uint32_t clientCount() const { return uint32_t(m_clients.size()); }

	void onConnected(ConnectionCallback cb) { m_onConnected = std::move(cb); }
	void onDisconnected(ConnectionCallback cb) { m_onDisconnected = std::move(cb); }
	void onData(DataCallback cb) { m_onData = std::move(cb); }

	const NetworkConfig& config() const { return m_config; }
	float latency(ClientId id) const {
		auto it = m_latencies.find(id);
		return it != m_latencies.end() ? it->second : 0;
	}

private:
	enum class Mode { None, Server, Client };

	bool initSocket() {
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

		m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket == INVALID_SOCKET) return false;

		u_long mode = 1;
		ioctlsocket(m_socket, FIONBIO, &mode);

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(m_config.port);
		addr.sin_addr.s_addr = (m_mode == Mode::Server) ? INADDR_ANY : INADDR_LOOPBACK;

		if (bind(m_socket, (sockaddr*)&addr, sizeof(addr)) != 0) {
			closesocket(m_socket);
			m_socket = INVALID_SOCKET;
			return false;
		}

		m_running = true;
		m_recvThread = std::thread(&NetworkManager::recvLoop, this);
		return true;
	}

	void sendConnect() {
		uint8_t buf[4] = {0};
		sendto(m_socket, (char*)buf, 4, 0, (sockaddr*)&m_serverAddress, sizeof(m_serverAddress));
	}

	void sendPings() {
		uint8_t buf[1] = {(uint8_t)MessageType::Ping};
		for (auto& [id, client] : m_clients) {
			if (id == kServerId) continue;
			sendto(m_socket, (char*)buf, 1, 0, (sockaddr*)&client.address, sizeof(client.address));
		}
	}

	void queueMessage(ClientId target, MessageType type, const uint8_t* data, uint32_t size, uint8_t channel, bool reliable) {
		std::lock_guard lock(m_sendMutex);
		auto it = m_clients.find(target);
		if (it == m_clients.end()) return;

		auto& client = it->second;
		uint16_t seq = client.nextSequence++;

		PacketHeader hdr{};
		hdr.messageType = (uint8_t)type;
		hdr.channel = channel;
		hdr.sequence = seq;
		hdr.ack = client.lastAckedSequence;
		hdr.ackBitfield = client.lastAckBitfield;
		hdr.payloadSize = (uint16_t)size;

		std::vector<uint8_t> packet(sizeof(PacketHeader) + size);
		memcpy(packet.data(), &hdr, sizeof(PacketHeader));
		memcpy(packet.data() + sizeof(PacketHeader), data, size);

		if (reliable) {
			client.reliableQueue.push({packet, seq, 0, 0});
		}

		sendPacket(client.address, packet);
	}

	void sendPacket(const sockaddr_in& addr, const std::vector<uint8_t>& packet) {
		sendto(m_socket, (char*)packet.data(), (int)packet.size(), 0, (sockaddr*)&addr, sizeof(addr));
	}

	void processReliableResends(float dt) {
		std::lock_guard lock(m_sendMutex);
		for (auto& [id, client] : m_clients) {
			auto& q = client.reliableQueue;
			while (!q.empty()) {
				auto& pending = q.front();
				pending.sendTime += dt;
				if (pending.sendTime > 0.5f) {
					pending.sendTime = 0;
					pending.retries++;
					if (pending.retries > 10) {
						q.pop();
						continue;
					}
					sendto(m_socket, (char*)pending.data.data(), (int)pending.data.size(), 0,
					       (sockaddr*)&client.address, sizeof(client.address));
				}
				break;
			}
		}
	}

	void checkTimeouts() {
		for (auto it = m_clients.begin(); it != m_clients.end(); ) {
			if (it->first != kServerId && m_timer - it->second.lastReceived > 10.0f) {
				if (m_onDisconnected) m_onDisconnected(it->first);
				it = m_clients.erase(it);
			} else {
				++it;
			}
		}
	}

	void recvLoop() {
		uint8_t buf[4096];
		while (m_running) {
			sockaddr_in from{};
			int fromLen = sizeof(from);
			int bytes = recvfrom(m_socket, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
			if (bytes <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }

			if (bytes < (int)sizeof(PacketHeader)) continue;
			auto* hdr = (PacketHeader*)buf;

			ClientId id = findOrCreateClient(from);
			if (id == kInvalidClient) continue;

			auto& client = m_clients[id];
			client.lastReceived = m_timer;

			if (hdr->messageType == (uint8_t)MessageType::Ping) {
				uint8_t pong = (uint8_t)MessageType::Pong;
				sendto(m_socket, (char*)&pong, 1, 0, (sockaddr*)&from, sizeof(from));
			} else if (hdr->messageType == (uint8_t)MessageType::Disconnect) {
				if (m_onDisconnected) m_onDisconnected(id);
			} else {
				if (m_onData && hdr->payloadSize <= bytes - (int)sizeof(PacketHeader)) {
					m_onData(id, buf + sizeof(PacketHeader), hdr->payloadSize);
				}
			}
		}
	}

	ClientId findOrCreateClient(const sockaddr_in& addr) {
		for (auto& [id, c] : m_clients) {
			if (c.address.sin_addr.s_addr == addr.sin_addr.s_addr &&
			    c.address.sin_port == addr.sin_port) return id;
		}
		if (m_mode == Mode::Server) {
			ClientId id = (ClientId)m_clients.size() + 1;
			m_clients[id] = {addr};
			m_clients[id].connected = true;
			m_clients[id].lastReceived = m_timer;
			if (m_onConnected) m_onConnected(id);
			return id;
		}
		return kInvalidClient;
	}

	NetworkConfig m_config;
	Mode m_mode = Mode::None;
	SOCKET m_socket = INVALID_SOCKET;
	sockaddr_in m_serverAddress{};
	std::atomic<bool> m_running{false};
	float m_timer = 0;

	std::unordered_map<ClientId, ClientState> m_clients;
	std::unordered_map<ClientId, float> m_latencies;
	std::mutex m_sendMutex;
	std::thread m_recvThread;
	std::thread m_sendThread;

	ConnectionCallback m_onConnected;
	ConnectionCallback m_onDisconnected;
	DataCallback m_onData;
};

} // namespace tucano::net
