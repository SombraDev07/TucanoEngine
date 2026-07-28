#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <vector>

namespace tucano::veg {

struct ScatterConfig {
	float minRadius = 1.0f;
	float maxRadius = 4.0f;
	float clusterChance = 0.3f;
	float clusterRadius = 5.0f;
	uint32_t clusterSize = 8;
	uint32_t maxAttempts = 30;
	float groundSnap = true;
	float slopeMax = 45.0f;
	float scaleRandomness = 0.3f;
	uint32_t seed = 42;
};

struct PoissonDiskResult {
	glm::vec2 position{0};
	float radius = 1.0f;
	bool placed = false;
};

class ProceduralPlacement {
public:
	ProceduralPlacement() = default;

	static std::vector<PoissonDiskResult> poissonDisk(const glm::vec2& regionMin, const glm::vec2& regionMax,
	                                                    float minRadius, float maxRadius,
	                                                    uint32_t maxPoints, uint32_t seed) {
		std::vector<PoissonDiskResult> results;
		if (minRadius <= 0) return results;

		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> dist01(0, 1);
		std::uniform_real_distribution<float> distRadius(minRadius, maxRadius);

		float cellSize = minRadius / std::sqrt(2.0f);
		int cols = int((regionMax.x - regionMin.x) / cellSize) + 1;
		int rows = int((regionMax.y - regionMin.y) / cellSize) + 1;

		std::vector<std::vector<int>> grid(rows, std::vector<int>(cols, -1));
		std::vector<glm::vec2> active;
		std::vector<glm::vec2> points;

		float startX = (regionMin.x + regionMax.x) * 0.5f;
		float startY = (regionMin.y + regionMax.y) * 0.5f;
		active.push_back({startX, startY});
		points.push_back({startX, startY});

		while (!active.empty() && points.size() < maxPoints) {
			int idx = int(dist01(rng) * (active.size() - 1));
			glm::vec2 center = active[size_t(idx)];

			bool found = false;
			float radius = distRadius(rng);

			for (uint32_t attempt = 0; attempt < 30; ++attempt) {
				float angle = dist01(rng) * 2.0f * 3.14159265f;
				float dist = radius + dist01(rng) * radius;
				glm::vec2 candidate(center.x + std::cos(angle) * dist,
				                    center.y + std::sin(angle) * dist);

				if (candidate.x < regionMin.x || candidate.x > regionMax.x ||
				    candidate.y < regionMin.y || candidate.y > regionMax.y) continue;

				int gx = int((candidate.x - regionMin.x) / cellSize);
				int gy = int((candidate.y - regionMin.y) / cellSize);
				if (gx < 0 || gx >= cols || gy < 0 || gy >= rows) continue;

				bool valid = true;
				for (int dy = -2; dy <= 2 && valid; ++dy) {
					for (int dx = -2; dx <= 2 && valid; ++dx) {
						int nx = gx + dx, ny = gy + dy;
						if (nx < 0 || nx >= cols || ny < 0 || ny >= rows) continue;
						int pIdx = grid[size_t(ny)][size_t(nx)];
						if (pIdx >= 0 && size_t(pIdx) < points.size()) {
							glm::vec2 diff = candidate - points[size_t(pIdx)];
							if (glm::dot(diff, diff) < radius * radius) valid = false;
						}
					}
				}

				if (valid) {
					active.push_back(candidate);
					points.push_back(candidate);
					grid[size_t(gy)][size_t(gx)] = int(points.size()) - 1;
					found = true;
					break;
				}
			}

			if (!found) {
				active[size_t(idx)] = active.back();
				active.pop_back();
			}
		}

		for (size_t i = 0; i < points.size(); ++i) {
			PoissonDiskResult r;
			r.position = points[i];
			r.radius = i == 0 ? minRadius : minRadius + dist01(rng) * (maxRadius - minRadius);
			r.placed = true;
			results.push_back(r);
		}

		return results;
	}

	static std::vector<glm::vec2> clusterScatter(const glm::vec2& center, float radius,
	                                              uint32_t count, uint32_t seed) {
		std::vector<glm::vec2> points;
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> dist(0, 1);

		for (uint32_t i = 0; i < count; ++i) {
			float angle = dist(rng) * 2.0f * 3.14159265f;
			float r = radius * std::sqrt(dist(rng));
			points.push_back({center.x + std::cos(angle) * r, center.y + std::sin(angle) * r});
		}

		return points;
	}

	struct PlacementPoint {
		glm::vec3 position{0};
		float scale = 1.0f;
		float rotation = 0;
		uint32_t typeId = 0;
	};

	std::vector<PlacementPoint> scatter(const ScatterConfig& config,
	                                     const glm::vec2& regionMin, const glm::vec2& regionMax,
	                                     uint32_t typeId,
	                                     const std::function<float(float, float)>& heightFunc) {
		std::vector<PlacementPoint> results;
		std::mt19937 rng(config.seed);
		std::uniform_real_distribution<float> dist01(0, 1);

		auto poisson = poissonDisk(regionMin, regionMax, config.minRadius,
		                           config.maxRadius, config.maxAttempts * 4, config.seed);

		for (auto& p : poisson) {
			if (!p.placed) continue;

			float slope = 0;
			if (heightFunc) {
				float hL = heightFunc(p.position.x - 0.5f, p.position.y);
				float hR = heightFunc(p.position.x + 0.5f, p.position.y);
				float hD = heightFunc(p.position.x, p.position.y - 0.5f);
				float hU = heightFunc(p.position.x, p.position.y + 0.5f);
				slope = std::atan2(std::max(std::abs(hR - hL), std::abs(hU - hD)), 1.0f) * 57.29578f;
			}

			if (slope > config.slopeMax) continue;

			float height = heightFunc ? heightFunc(p.position.x, p.position.y) : 0;

			PlacementPoint pt;
			pt.position = {p.position.x, height, p.position.y};
			pt.scale = 1.0f + (dist01(rng) - 0.5f) * config.scaleRandomness * 2.0f;
			pt.rotation = dist01(rng) * 2.0f * 3.14159265f;
			pt.typeId = typeId;
			results.push_back(pt);

			if (dist01(rng) < config.clusterChance) {
				auto cluster = clusterScatter(p.position, config.clusterRadius,
				                              config.clusterSize, config.seed + uint32_t(results.size()));
				for (auto& cp : cluster) {
					float ch = heightFunc ? heightFunc(cp.x, cp.y) : 0;
					PlacementPoint cpt;
					cpt.position = {cp.x, ch, cp.y};
					cpt.scale = pt.scale * (0.5f + dist01(rng) * 0.5f);
					cpt.rotation = dist01(rng) * 2.0f * 3.14159265f;
					cpt.typeId = typeId;
					results.push_back(cpt);
				}
			}
		}

		return results;
	}

	float slopeAt(float x, float z, const std::function<float(float, float)>& heightFn) const {
		if (!heightFn) return 0;
		float hL = heightFn(x - 0.5f, z);
		float hR = heightFn(x + 0.5f, z);
		float hD = heightFn(x, z - 0.5f);
		float hU = heightFn(x, z + 0.5f);
		return std::atan2(std::max(std::abs(hR - hL), std::abs(hU - hD)), 1.0f) * 57.29578f;
	}
};

} // namespace tucano::veg
