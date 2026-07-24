#pragma once

#include "Terrain/Heightmap.h"

#include <glm/glm.hpp>

namespace tucano::terrain {

struct TerrainRayHit {
	bool hit = false;
	glm::vec3 point{0};
	glm::vec3 normal{0, 1, 0};
	float distance = 0.0f;
};

class TerrainRayTracer {
public:
	TerrainRayHit trace(const Heightmap& hm, glm::vec3 origin, glm::vec3 direction, float maxDist = 5000.0f) const;

	float getHeightAboveGround(const Heightmap& hm, glm::vec3 point, float maxSearch = 200.0f) const;

private:
	static constexpr float kStepSize = 0.5f;
	static constexpr int kRefineSteps = 8;
};

} // namespace tucano::terrain
