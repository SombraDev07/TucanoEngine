#pragma once

#include "Terrain/Heightmap.h"

#include <cstdint>
#include <random>

namespace tucano::terrain {

struct ErosionParams {
	uint32_t iterations = 50000;
	uint32_t seed = 123;
	float erosionRate = 0.3f;
	float depositionRate = 0.3f;
	float evaporationRate = 0.01f;
	float minSlope = 0.01f;
	float gravity = 4.0f;
	float capacityFactor = 4.0f;
	float inertia = 0.05f;
	float radius = 3.0f;
};

class ErosionSimulation {
public:
	void erode(Heightmap& hm, const ErosionParams& params = {});

	void applyThermalErosion(Heightmap& hm, float talusAngle = 0.6f, uint32_t iterations = 100);

private:
	std::mt19937 m_rng;
};

} // namespace tucano::terrain
