#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace tucano::veg {

enum class VegLODLevel : uint8_t {
	Full3D = 0,
	Simplified = 1,
	Billboard = 2,
	Culled = 3,
	Count = 4
};

struct LODRange {
	float distance0 = 30.0f;
	float distance1 = 80.0f;
	float distance2 = 150.0f;
	float cullDistance = 200.0f;

	VegLODLevel select(float distance) const {
		if (distance > cullDistance) return VegLODLevel::Culled;
		if (distance > distance2)   return VegLODLevel::Billboard;
		if (distance > distance1)   return VegLODLevel::Simplified;
		return VegLODLevel::Full3D;
	}

	float crossFadeRange(VegLODLevel level) const {
		switch (level) {
			case VegLODLevel::Full3D:     return distance0 * 0.15f;
			case VegLODLevel::Simplified:  return distance1 * 0.15f;
			case VegLODLevel::Billboard:   return distance2 * 0.15f;
			default: return 1.0f;
		}
	}

	float nextDistance(VegLODLevel level) const {
		switch (level) {
			case VegLODLevel::Full3D:     return distance0;
			case VegLODLevel::Simplified:  return distance1;
			case VegLODLevel::Billboard:   return distance2;
			default: return cullDistance;
		}
	}
};

struct LODConfig {
	float globalDensityScale = 1.0f;
	float screenSizeThreshold0 = 0.05f;  // Full3D
	float screenSizeThreshold1 = 0.02f;  // Simplified
	float screenSizeThreshold2 = 0.005f; // Billboard
	float crossFadeWidth = 0.15f;
	bool enableDitherFade = true;
	bool enableDensityScaling = true;
	float densityFalloffStart = 0.3f;
	float densityFalloffEnd = 1.0f;
};

struct DensityRegion {
	glm::vec2 worldMin{0};
	glm::vec2 worldMax{0};
	float density = 1.0f;
	float falloff = 10.0f;
};

struct DitherState {
	uint32_t frameCounter = 0;
	static constexpr uint32_t kDitherMatrix[16] = {
		 0, 8, 2,10,
		12, 4,14, 6,
		 3,11, 1, 9,
		15, 7,13, 5
	};

	float ditherThreshold(uint32_t pixelX, uint32_t pixelY, float fadeAlpha) const {
		uint32_t x = pixelX & 3;
		uint32_t y = pixelY & 3;
		uint32_t index = y * 4 + x;
		uint32_t matrixIndex = (kDitherMatrix[index] + frameCounter) & 15;
		return float(matrixIndex) / 16.0f * fadeAlpha;
	}
};

class LODManager {
public:
	static LODManager& instance() { static LODManager lm; return lm; }

	void configure(const LODConfig& cfg) { m_config = cfg; }
	LODConfig& config() { return m_config; }

	void setPerTypeLODRanges(uint32_t typeId, const LODRange& range) {
		if (typeId >= m_perTypeRanges.size()) m_perTypeRanges.resize(typeId + 1);
		m_perTypeRanges[typeId] = range;
	}

	const LODRange& rangeForType(uint32_t typeId) const {
		static LODRange defaultRange;
		if (typeId >= m_perTypeRanges.size()) return defaultRange;
		return m_perTypeRanges[typeId];
	}

	VegLODLevel selectLOD(float distance, uint32_t typeId) const {
		return rangeForType(typeId).select(distance);
	}

	float computeDensityScale(float distance, uint32_t typeId) const {
		if (!m_config.enableDensityScaling) return m_config.globalDensityScale;

		const auto& range = rangeForType(typeId);
		float t = (distance - range.cullDistance * m_config.densityFalloffStart) /
		          (range.cullDistance * (m_config.densityFalloffEnd - m_config.densityFalloffStart));
		t = std::clamp(t, 0.0f, 1.0f);
		return m_config.globalDensityScale * (1.0f - t * 0.8f);
	}

	float computeCrossFade(float distance, uint32_t typeId) const {
		if (!m_config.enableDitherFade) return 0;

		const auto& range = rangeForType(typeId);
		VegLODLevel level = range.select(distance);
		if (level == VegLODLevel::Culled) return 1.0f;

		float nextDist = range.nextDistance(level);
		float fadeRange = range.crossFadeRange(level);
		float t = (nextDist - distance) / fadeRange;
		return 1.0f - std::clamp(t, 0.0f, 1.0f);
	}

	bool shouldCull(float distance, uint32_t typeId) const {
		return rangeForType(typeId).select(distance) == VegLODLevel::Culled;
	}

	bool shouldRenderLOD(float distance, uint32_t typeId, VegLODLevel lod) const {
		VegLODLevel selected = selectLOD(distance, typeId);

		if (m_config.enableDitherFade) {
			VegLODLevel next = static_cast<VegLODLevel>(
				std::min(int(selected) + 1, int(VegLODLevel::Culled)));
			return lod == selected || lod == next;
		}

		return lod == selected;
	}

	void addDensityRegion(const DensityRegion& region) {
		m_densityRegions.push_back(region);
	}

	float sampleDensity(const glm::vec2& worldPos) const {
		float density = 1.0f;
		for (auto& r : m_densityRegions) {
			if (worldPos.x >= r.worldMin.x && worldPos.x <= r.worldMax.x &&
			    worldPos.y >= r.worldMin.y && worldPos.y <= r.worldMax.y) {
				float edgeX = std::min(worldPos.x - r.worldMin.x, r.worldMax.x - worldPos.x) / r.falloff;
				float edgeY = std::min(worldPos.y - r.worldMin.y, r.worldMax.y - worldPos.y) / r.falloff;
				float edge = std::min(edgeX, edgeY);
				density *= r.density * std::clamp(edge, 0.0f, 1.0f);
			}
		}
		return density * m_config.globalDensityScale;
	}

	void advanceFrame() { m_dither.frameCounter++; }
	const DitherState& dither() const { return m_dither; }

	void clearRegions() { m_densityRegions.clear(); }
	size_t regionCount() const { return m_densityRegions.size(); }

private:
	LODConfig m_config;
	std::vector<LODRange> m_perTypeRanges;
	std::vector<DensityRegion> m_densityRegions;
	DitherState m_dither;
};

} // namespace tucano::veg
