#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace tucano::veg {

class DensityMap {
public:
	DensityMap() = default;

	void create(uint32_t width, uint32_t height, float defaultValue = 1.0f) {
		m_width = width;
		m_height = height;
		m_data.resize(size_t(width) * height);
		std::fill(m_data.begin(), m_data.end(), defaultValue);
		m_worldOrigin = {0, 0};
		m_worldSize = {100.0f, 100.0f};
	}

	void setWorldBounds(const glm::vec2& origin, const glm::vec2& size) {
		m_worldOrigin = origin;
		m_worldSize = size;
	}

	float sample(float worldX, float worldZ) const {
		if (m_data.empty()) return 1.0f;

		float u = (worldX - m_worldOrigin.x) / m_worldSize.x;
		float v = (worldZ - m_worldOrigin.y) / m_worldSize.y;

		float px = u * float(m_width - 1);
		float pz = v * float(m_height - 1);

		int x0 = std::clamp(int(std::floor(px)), 0, int(m_width) - 1);
		int x1 = std::clamp(x0 + 1, 0, int(m_width) - 1);
		int z0 = std::clamp(int(std::floor(pz)), 0, int(m_height) - 1);
		int z1 = std::clamp(z0 + 1, 0, int(m_height) - 1);

		float fx = px - float(x0);
		float fz = pz - float(z0);

		float v00 = m_data[size_t(z0) * m_width + size_t(x0)];
		float v10 = m_data[size_t(z0) * m_width + size_t(x1)];
		float v01 = m_data[size_t(z1) * m_width + size_t(x0)];
		float v11 = m_data[size_t(z1) * m_width + size_t(x1)];

		return glm::mix(glm::mix(v00, v10, fx), glm::mix(v01, v11, fx), fz);
	}

	void paintBrush(float worldX, float worldZ, float radius, float strength) {
		float u = (worldX - m_worldOrigin.x) / m_worldSize.x;
		float v = (worldZ - m_worldOrigin.y) / m_worldSize.y;

		int cx = int(u * float(m_width));
		int cz = int(v * float(m_height));
		int pixelRadius = int(radius / m_worldSize.x * float(m_width)) + 1;

		for (int dz = -pixelRadius; dz <= pixelRadius; ++dz) {
			for (int dx = -pixelRadius; dx <= pixelRadius; ++dx) {
				int px = cx + dx;
				int pz = cz + dz;
				if (px < 0 || px >= int(m_width) || pz < 0 || pz >= int(m_height)) continue;

				float dist = std::sqrt(float(dx * dx + dz * dz));
				float factor = 1.0f - std::clamp(dist / float(pixelRadius), 0.0f, 1.0f);
				factor = factor * factor * factor;

				size_t idx = size_t(pz) * m_width + size_t(px);
				m_data[idx] = std::clamp(m_data[idx] + strength * factor, 0.0f, 1.0f);
			}
		}
	}

	void eraseBrush(float worldX, float worldZ, float radius, float strength) {
		paintBrush(worldX, worldZ, radius, -strength);
	}

	bool save(const std::string& path) const {
		std::ofstream f(path, std::ios::binary);
		if (!f) return false;
		f.write(reinterpret_cast<const char*>(&m_width), sizeof(m_width));
		f.write(reinterpret_cast<const char*>(&m_height), sizeof(m_height));
		f.write(reinterpret_cast<const char*>(m_data.data()), m_data.size() * sizeof(float));
		return true;
	}

	bool load(const std::string& path) {
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		f.read(reinterpret_cast<char*>(&m_width), sizeof(m_width));
		f.read(reinterpret_cast<char*>(&m_height), sizeof(m_height));
		m_data.resize(size_t(m_width) * m_height);
		f.read(reinterpret_cast<char*>(m_data.data()), m_data.size() * sizeof(float));
		return true;
	}

	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }
	const float* data() const { return m_data.data(); }

private:
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	std::vector<float> m_data;
	glm::vec2 m_worldOrigin{0, 0};
	glm::vec2 m_worldSize{100, 100};
};

class ExclusionZone {
public:
	enum Type { Rectangle, Circle, Spline };

	struct Zone {
		Type type = Rectangle;
		glm::vec2 center{0};
		glm::vec2 halfExtent{0};
		float radius = 0;
		float falloff = 2.0f;
		bool enabled = true;
		std::string name;
	};

	void addRectangle(const glm::vec2& center, const glm::vec2& halfExtent, const std::string& name = "") {
		m_zones.push_back({Rectangle, center, halfExtent, 0, 2.0f, true, name});
	}

	void addCircle(const glm::vec2& center, float radius, const std::string& name = "") {
		m_zones.push_back({Circle, center, {}, radius, 2.0f, true, name});
	}

	bool isExcluded(float worldX, float worldZ) const {
		for (auto& z : m_zones) {
			if (!z.enabled) continue;
			if (z.type == Rectangle) {
				if (worldX >= z.center.x - z.halfExtent.x && worldX <= z.center.x + z.halfExtent.x &&
				    worldZ >= z.center.y - z.halfExtent.y && worldZ <= z.center.y + z.halfExtent.y)
					return true;
			} else if (z.type == Circle) {
				glm::vec2 diff(worldX - z.center.x, worldZ - z.center.y);
				if (glm::dot(diff, diff) <= z.radius * z.radius) return true;
			}
		}
		return false;
	}

	void removeZone(const std::string& name) {
		m_zones.erase(std::remove_if(m_zones.begin(), m_zones.end(),
			[&](const Zone& z) { return z.name == name; }), m_zones.end());
	}

	void clear() { m_zones.clear(); }
	size_t zoneCount() const { return m_zones.size(); }
	const std::vector<Zone>& zones() const { return m_zones; }

private:
	std::vector<Zone> m_zones;
};

} // namespace tucano::veg
