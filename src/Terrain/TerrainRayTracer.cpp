#include "Terrain/TerrainRayTracer.h"

#include <algorithm>
#include <cmath>

namespace tucano::terrain {

TerrainRayHit TerrainRayTracer::trace(const Heightmap& hm, glm::vec3 origin, glm::vec3 direction, float maxDist) const {
	TerrainRayHit result;
	direction = glm::normalize(direction);

	float ws = hm.worldSize();

	float t = 0.0f;
	bool below = false;

	while (t < maxDist) {
		glm::vec3 p = origin + direction * t;
		if (p.x < 0.0f || p.x >= ws || p.z < 0.0f || p.z >= ws) break;

		float h = hm.sampleHeight(p.x, p.z);
		if (p.y < h) { below = true; break; }
		t += kStepSize;
	}

	if (!below) return result;

	float lo = t - kStepSize;
	float hi = t;
	for (int i = 0; i < kRefineSteps; ++i) {
		float mid = (lo + hi) * 0.5f;
		glm::vec3 p = origin + direction * mid;
		float h = hm.sampleHeight(p.x, p.z);
		if (p.y < h) hi = mid;
		else lo = mid;
	}

	float hitT = (lo + hi) * 0.5f;
	glm::vec3 hitP = origin + direction * hitT;

	if (hitP.x < 0.0f || hitP.x >= ws || hitP.z < 0.0f || hitP.z >= ws) return result;

	result.hit = true;
	result.point = hitP;
	result.distance = hitT;
	result.normal = hm.sampleNormal(hitP.x, hitP.z);
	return result;
}

float TerrainRayTracer::getHeightAboveGround(const Heightmap& hm, glm::vec3 point, float maxSearch) const {
	TerrainRayHit hit = trace(hm, point, glm::vec3(0, -1, 0), maxSearch);
	if (hit.hit) {
		return point.y - hit.point.y;
	}
	return maxSearch;
}

} // namespace tucano::terrain
