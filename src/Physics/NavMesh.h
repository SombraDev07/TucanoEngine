#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

namespace tucano::ai {

struct NavNode {
	glm::vec3 position;
	std::vector<uint32_t> neighbors;
	float cost = 1.0f;
};

struct NavPathPoint {
	glm::vec3 position;
	float distanceFromStart = 0;
};

class NavMesh {
public:
	NavMesh() = default;

	void addNode(const glm::vec3& pos) {
		m_nodes.push_back({pos});
	}

	void connectNodes(uint32_t a, uint32_t b, bool bidirectional = true) {
		if (a >= m_nodes.size() || b >= m_nodes.size()) return;
		m_nodes[a].neighbors.push_back(b);
		if (bidirectional) m_nodes[b].neighbors.push_back(a);
	}

	void clear() { m_nodes.clear(); }
	size_t nodeCount() const { return m_nodes.size(); }

	uint32_t closestNode(const glm::vec3& point) const {
		uint32_t best = 0;
		float bestDist = std::numeric_limits<float>::max();
		for (size_t i = 0; i < m_nodes.size(); ++i) {
			float d = glm::distance(m_nodes[i].position, point);
			if (d < bestDist) { bestDist = d; best = uint32_t(i); }
		}
		return best;
	}

	std::vector<NavPathPoint> findPath(uint32_t start, uint32_t end) const {
		std::vector<NavPathPoint> path;
		if (start >= m_nodes.size() || end >= m_nodes.size()) return path;
		if (start == end) { path.push_back({m_nodes[start].position, 0}); return path; }

		using PQEntry = std::pair<float, uint32_t>;
		std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> open;

		std::unordered_map<uint32_t, float> gScore;
		std::unordered_map<uint32_t, uint32_t> cameFrom;

		for (size_t i = 0; i < m_nodes.size(); ++i) gScore[uint32_t(i)] = std::numeric_limits<float>::max();

		gScore[start] = 0;
		float h = glm::distance(m_nodes[start].position, m_nodes[end].position);
		open.push({h, start});

		while (!open.empty()) {
			auto [_, current] = open.top();
			open.pop();

			if (current == end) {
				uint32_t node = end;
				while (node != start) {
					path.push_back({m_nodes[node].position, gScore[node]});
					node = cameFrom[node];
				}
				path.push_back({m_nodes[start].position, 0});
				std::reverse(path.begin(), path.end());
				return path;
			}

			for (uint32_t neighbor : m_nodes[current].neighbors) {
				float tentativeG = gScore[current] + glm::distance(
					m_nodes[current].position, m_nodes[neighbor].position);

				if (tentativeG < gScore[neighbor]) {
					cameFrom[neighbor] = current;
					gScore[neighbor] = tentativeG;
					float f = tentativeG + glm::distance(m_nodes[neighbor].position, m_nodes[end].position);
					open.push({f, neighbor});
				}
			}
		}

		return path;
	}

	std::vector<NavPathPoint> findPathToPoint(uint32_t start, const glm::vec3& target) const {
		return findPath(start, closestNode(target));
	}

	std::vector<NavPathPoint> findPathFromTo(const glm::vec3& from, const glm::vec3& to) const {
		return findPath(closestNode(from), closestNode(to));
	}

	glm::vec3 nodePosition(uint32_t id) const {
		return id < m_nodes.size() ? m_nodes[id].position : glm::vec3(0);
	}

	const NavNode& node(uint32_t id) const { return m_nodes[id]; }
	NavNode& node(uint32_t id) { return m_nodes[id]; }

private:
	std::vector<NavNode> m_nodes;
};

} // namespace tucano::ai
