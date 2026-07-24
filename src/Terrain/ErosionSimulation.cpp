#include "Terrain/ErosionSimulation.h"

#include <algorithm>
#include <cmath>

namespace tucano::terrain {

static float sampleBilinear(const std::vector<float>& data, uint32_t res, float fx, float fz) {
	int x0 = int(fx), z0 = int(fz);
	int x1 = std::min(x0 + 1, int(res - 1));
	int z1 = std::min(z0 + 1, int(res - 1));
	x0 = std::max(0, x0); z0 = std::max(0, z0);
	float tx = fx - float(x0), tz = fz - float(z0);
	float h00 = data[size_t(z0) * res + size_t(x0)];
	float h10 = data[size_t(z0) * res + size_t(x1)];
	float h01 = data[size_t(z1) * res + size_t(x0)];
	float h11 = data[size_t(z1) * res + size_t(x1)];
	return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
}

static glm::vec3 sampleNormal(const std::vector<float>& data, uint32_t res, float fx, float fz, float ws) {
	float ts = ws / float(res);
	float hL = sampleBilinear(data, res, fx - 1, fz);
	float hR = sampleBilinear(data, res, fx + 1, fz);
	float hD = sampleBilinear(data, res, fx, fz - 1);
	float hU = sampleBilinear(data, res, fx, fz + 1);
	return glm::normalize(glm::vec3(0.5f * (hL - hR) / ts, 1.0f, 0.5f * (hD - hU) / ts));
}

static void depositData(std::vector<float>& data, uint32_t res, float fx, float fz, float amount) {
	int x0 = int(fx), z0 = int(fz);
	if (x0 < 0 || x0 >= int(res) || z0 < 0 || z0 >= int(res)) return;
	float tx = fx - float(x0), tz = fz - float(z0);
	int x1 = std::min(x0 + 1, int(res - 1));
	int z1 = std::min(z0 + 1, int(res - 1));
	float w00 = (1 - tx) * (1 - tz);
	float w10 = tx * (1 - tz);
	float w01 = (1 - tx) * tz;
	float w11 = tx * tz;
	data[size_t(z0) * res + size_t(x0)] += amount * w00;
	data[size_t(z0) * res + size_t(x1)] += amount * w10;
	data[size_t(z1) * res + size_t(x0)] += amount * w01;
	data[size_t(z1) * res + size_t(x1)] += amount * w11;
}

void ErosionSimulation::erode(Heightmap& hm, const ErosionParams& params) {
	m_rng.seed(params.seed);

	uint32_t res = hm.resolution();
	float ws = hm.worldSize();
	std::vector<float> work(hm.data());

	std::uniform_real_distribution<float> dist(0.0f, float(res - 1));

	for (uint32_t iter = 0; iter < params.iterations; ++iter) {
		float fx = dist(m_rng);
		float fz = dist(m_rng);
		float x = fx, z = fz;
		float dirX = 0, dirZ = 0;
		float speed = 1.0f;
		float water = 1.0f;
		float sediment = 0.0f;

		for (int life = 0; life < 40; ++life) {
			int ix = int(x), iz = int(z);
			if (ix < 0 || ix >= int(res) - 1 || iz < 0 || iz >= int(res) - 1) break;

			glm::vec3 n = sampleNormal(work, res, x, z, ws);
			float nLen = std::sqrt(n.x * n.x + n.z * n.z);

			dirX = dirX * params.inertia - n.x * (1 - params.inertia);
			dirZ = dirZ * params.inertia - n.z * (1 - params.inertia);
			float len = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (len > 0) { dirX /= len; dirZ /= len; }

			x += dirX;
			z += dirZ;
			if (x < 0 || x >= float(res - 1) || z < 0 || z >= float(res - 1)) break;

			float hOld = sampleBilinear(hm.data(), res, x, z);
			float hNew = sampleBilinear(work, res, x, z);
			float deltaH = hOld - hNew;

			float capacity = std::max(deltaH, params.minSlope) * speed * water * params.capacityFactor;

			if (sediment > capacity) {
				float depositAmt = (sediment - capacity) * params.depositionRate;
				depositData(work, res, x, z, depositAmt);
				sediment -= depositAmt;
			} else {
				float erodeAmt = std::min((capacity - sediment) * params.erosionRate, deltaH);
				depositData(work, res, x, z, -erodeAmt);
				sediment += erodeAmt;
			}

			speed = std::sqrt(speed * speed + deltaH * params.gravity);
			water *= (1 - params.evaporationRate);
			if (water < 0.01f) break;
		}
	}

	std::copy(work.begin(), work.end(), hm.dataPtr());
	hm.recomputeBounds();
}

void ErosionSimulation::applyThermalErosion(Heightmap& hm, float talusAngle, uint32_t iterations) {
	uint32_t res = hm.resolution();
	float ws = hm.worldSize();
	float ts = ws / float(res);
	float talusThreshold = std::tan(talusAngle * 3.14159265f / 180.0f) * ts;

	std::vector<float> work(hm.data());

	for (uint32_t iter = 0; iter < iterations; ++iter) {
		for (uint32_t z = 1; z < res - 1; ++z) {
			for (uint32_t x = 1; x < res - 1; ++x) {
				float h = work[size_t(z) * res + size_t(x)];
				float maxDiff = 0; int maxDx = 0, maxDz = 0;
				for (int dz = -1; dz <= 1; ++dz) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dz == 0) continue;
						float nh = work[size_t(z + dz) * res + size_t(x + dx)];
						float diff = h - nh;
						if (diff > maxDiff) { maxDiff = diff; maxDx = dx; maxDz = dz; }
					}
				}
				if (maxDiff > talusThreshold) {
					float excess = maxDiff - talusThreshold;
					float move = excess * 0.5f;
					work[size_t(z) * res + size_t(x)] -= move;
					work[size_t(z + maxDz) * res + size_t(x + maxDx)] += move;
				}
			}
		}
	}

	std::copy(work.begin(), work.end(), hm.dataPtr());
	hm.recomputeBounds();
}

} // namespace tucano::terrain
