#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace tucano {

class SplinePath {
public:
	SplinePath() = default;

	void addPoint(const glm::vec3& p) { m_points.push_back(p); }
	void setPoints(const std::vector<glm::vec3>& pts) { m_points = pts; }
	void clear() { m_points.clear(); }
	size_t size() const { return m_points.size(); }

	glm::vec3 evaluate(float t) const {
		if (m_points.empty()) return {0,0,0};
		if (m_points.size() == 1) return m_points[0];
		if (m_points.size() == 2) return glm::mix(m_points[0], m_points[1], t);

		t = std::clamp(t, 0.0f, 1.0f);
		float segmentCount = float(m_points.size()) - 1.0f;
		float seg = t * segmentCount;
		int i = std::min(int(std::floor(seg)), int(m_points.size()) - 2);
		float localT = seg - float(i);

		return catmullRom(
			i > 0 ? m_points[i - 1] : m_points[i] - (m_points[i + 1] - m_points[i]),
			m_points[i],
			m_points[i + 1],
			i + 2 < int(m_points.size()) ? m_points[i + 2] : m_points[i + 1] + (m_points[i + 1] - m_points[i]),
			localT
		);
	}

	glm::vec3 tangent(float t) const {
		if (m_points.size() < 2) return {0,0,1};

		t = std::clamp(t, 0.0f, 1.0f);
		float segmentCount = float(m_points.size()) - 1.0f;
		float seg = t * segmentCount;
		int i = std::min(int(std::floor(seg)), int(m_points.size()) - 2);
		float localT = seg - float(i);

		glm::vec3 p0 = i > 0 ? m_points[i - 1] : m_points[i] - (m_points[i + 1] - m_points[i]);
		glm::vec3 p1 = m_points[i];
		glm::vec3 p2 = m_points[i + 1];
		glm::vec3 p3 = i + 2 < int(m_points.size()) ? m_points[i + 2] : m_points[i + 1] + (m_points[i + 1] - m_points[i]);

		float t2 = localT * localT;
		return glm::normalize(
			(-0.5f * 3.0f * t2 + localT - 0.5f) * p0 +
			(1.5f * t2 - 2.5f * t2 + 1.0f) * p1 +
			(-1.5f * t2 + 2.0f * localT + 0.5f) * p2 +
			(0.5f * t2 - 0.5f * localT) * p3
		);
	}

	float length(uint32_t samples = 100) const {
		float total = 0;
		glm::vec3 prev = evaluate(0);
		for (uint32_t i = 1; i <= samples; ++i) {
			float t = float(i) / float(samples);
			glm::vec3 curr = evaluate(t);
			total += glm::distance(prev, curr);
			prev = curr;
		}
		return total;
	}

	float closestParam(const glm::vec3& point, uint32_t samples = 100) const {
		float bestT = 0;
		float bestDist = std::numeric_limits<float>::max();
		for (uint32_t i = 0; i <= samples; ++i) {
			float t = float(i) / float(samples);
			float d = glm::distance(evaluate(t), point);
			if (d < bestDist) { bestDist = d; bestT = t; }
		}
		return bestT;
	}

	const std::vector<glm::vec3>& points() const { return m_points; }

private:
	static glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
	                            const glm::vec3& p2, const glm::vec3& p3, float t) {
		float t2 = t * t;
		float t3 = t2 * t;
		return 0.5f * (
			(2.0f * p1) +
			(-p0 + p2) * t +
			(2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
			(-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
		);
	}

	std::vector<glm::vec3> m_points;
};

} // namespace tucano
