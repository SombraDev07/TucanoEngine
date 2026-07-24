#pragma once

#include "Terrain/Heightmap.h"

#include <glm/glm.hpp>

#include <memory>
#include <random>
#include <vector>

namespace tucano::terrain {

struct TerrainGenParams {
	uint32_t resolution = 512;
	float worldSize = 1024.0f;
	uint32_t octaves = 6;
	float persistence = 0.5f;
	float lacunarity = 2.0f;
	float baseFrequency = 4.0f;
	float baseAmplitude = 128.0f;
	float baseHeight = 0.0f;
	uint32_t seed = 42;
};

class TerrainGenerator {
public:
	static std::shared_ptr<Heightmap> generate(rhi::Device& device, const TerrainGenParams& params);

	static std::vector<float> generateFbm(const TerrainGenParams& params);
	static std::vector<float> generateRidged(const TerrainGenParams& params);

private:
	static void initPermutation(std::vector<int>& perm, uint32_t seed);
	static float fade(float t);
	static float lerp(float a, float b, float t);
	static float grad(int hash, float x, float y);
	static float noise2D(float x, float y, const std::vector<int>& perm);
	static float fbm2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm);
	static float ridged2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm);
};

} // namespace tucano::terrain
