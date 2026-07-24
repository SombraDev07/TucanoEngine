#include "Terrain/TerrainGenerator.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace tucano::terrain {

void TerrainGenerator::initPermutation(std::vector<int>& perm, uint32_t seed) {
	perm.resize(512);
	for (int i = 0; i < 256; ++i) {
		perm[i] = i;
	}
	std::mt19937 rng(seed);
	std::shuffle(perm.begin(), perm.begin() + 256, rng);
	for (int i = 0; i < 256; ++i) {
		perm[i + 256] = perm[i];
	}
}

float TerrainGenerator::fade(float t) {
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float TerrainGenerator::lerp(float a, float b, float t) {
	return a + t * (b - a);
}

float TerrainGenerator::grad(int hash, float x, float y) {
	int h = hash & 3;
	float u = h < 2 ? x : y;
	float v = h < 2 ? y : x;
	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float TerrainGenerator::noise2D(float x, float y, const std::vector<int>& perm) {
	int xi = int(std::floor(x)) & 255;
	int yi = int(std::floor(y)) & 255;
	float xf = x - std::floor(x);
	float yf = y - std::floor(y);
	float u = fade(xf);
	float v = fade(yf);

	int aa = perm[perm[xi] + yi];
	int ab = perm[perm[xi] + yi + 1];
	int ba = perm[perm[xi + 1] + yi];
	int bb = perm[perm[xi + 1] + yi + 1];

	return lerp(lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
	            lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u), v);
}

float TerrainGenerator::fbm2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm) {
	float value = 0.0f;
	float freq = params.baseFrequency / params.worldSize;
	float amp = params.baseAmplitude;
	float maxValue = 0.0f;

	for (uint32_t i = 0; i < params.octaves; ++i) {
		value += noise2D(x * freq, y * freq, perm) * amp;
		maxValue += amp;
		freq *= params.lacunarity;
		amp *= params.persistence;
	}

	return value / maxValue;
}

float TerrainGenerator::ridged2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm) {
	float value = 0.0f;
	float freq = params.baseFrequency / params.worldSize;
	float amp = params.baseAmplitude;

	float prev = 1.0f;

	for (uint32_t i = 0; i < params.octaves; ++i) {
		float n = glm::abs(noise2D(x * freq, y * freq, perm));
		n = 1.0f - n;
		n *= n;
		n *= prev;
		prev = n;
		value += n * amp;
		freq *= params.lacunarity;
		amp *= params.persistence;
	}

	return value;
}

std::vector<float> TerrainGenerator::generateFbm(const TerrainGenParams& params) {
	std::vector<int> perm;
	initPermutation(perm, params.seed);

	std::vector<float> data(size_t(params.resolution) * size_t(params.resolution));
	for (uint32_t z = 0; z < params.resolution; ++z) {
		for (uint32_t x = 0; x < params.resolution; ++x) {
			float wx = float(x) / float(params.resolution - 1) * params.worldSize;
			float wz = float(z) / float(params.resolution - 1) * params.worldSize;
			data[size_t(z) * params.resolution + size_t(x)] = params.baseHeight + fbm2D(wx, wz, params, perm) * params.baseAmplitude;
		}
	}
	return data;
}

std::vector<float> TerrainGenerator::generateRidged(const TerrainGenParams& params) {
	std::vector<int> perm;
	initPermutation(perm, params.seed);

	std::vector<float> data(size_t(params.resolution) * size_t(params.resolution));
	for (uint32_t z = 0; z < params.resolution; ++z) {
		for (uint32_t x = 0; x < params.resolution; ++x) {
			float wx = float(x) / float(params.resolution - 1) * params.worldSize;
			float wz = float(z) / float(params.resolution - 1) * params.worldSize;
			data[size_t(z) * params.resolution + size_t(x)] = params.baseHeight + ridged2D(wx, wz, params, perm) * params.baseAmplitude;
		}
	}
	return data;
}

std::shared_ptr<Heightmap> TerrainGenerator::generate(rhi::Device& device, const TerrainGenParams& params) {
	auto data = generateFbm(params);
	return Heightmap::createFromData(device, params.resolution, params.worldSize, data);
}

} // namespace tucano::terrain
