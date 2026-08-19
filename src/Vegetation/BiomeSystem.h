#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::veg {

struct BiomeRule {
	enum Condition : uint8_t {
		SlopeLess, SlopeGreater, SlopeBetween,
		AltitudeLess, AltitudeGreater, AltitudeBetween,
		WaterDistanceLess, WaterDistanceGreater,
		SunExposureLess, SunExposureGreater,
		AlwaysTrue
	};

	Condition condition = AlwaysTrue;
	float valueA = 0;
	float valueB = 0;
	float weight = 1.0f;
};

struct BiomeLayer {
	std::string name;
	float priority = 0;
	std::vector<BiomeRule> rules;

	using VegetationEntry = std::pair<uint32_t, float>;
	std::vector<VegetationEntry> vegetationTypes;
	float density = 1.0f;
	float clusterSize = 0;
	float minScale = 0.5f;
	float maxScale = 2.0f;
	bool enabled = true;

	bool evaluate(const glm::vec3& position, float slope, float altitude,
	              float waterDist, float sunExposure) const {
		float totalWeight = 0;
		float matchedWeight = 0;

		for (auto& rule : rules) {
			totalWeight += rule.weight;
			if (evaluateRule(rule, slope, altitude, waterDist, sunExposure))
				matchedWeight += rule.weight;
		}

		return totalWeight > 0 ? (matchedWeight / totalWeight) >= 0.5f : true;
	}

private:
	static bool evaluateRule(const BiomeRule& rule, float slope, float altitude,
	                         float waterDist, float sunExposure) {
		switch (rule.condition) {
			case BiomeRule::SlopeLess:         return slope < rule.valueA;
			case BiomeRule::SlopeGreater:      return slope > rule.valueA;
			case BiomeRule::SlopeBetween:      return slope >= rule.valueA && slope <= rule.valueB;
			case BiomeRule::AltitudeLess:      return altitude < rule.valueA;
			case BiomeRule::AltitudeGreater:   return altitude > rule.valueA;
			case BiomeRule::AltitudeBetween:   return altitude >= rule.valueA && altitude <= rule.valueB;
			case BiomeRule::WaterDistanceLess:  return waterDist < rule.valueA;
			case BiomeRule::WaterDistanceGreater: return waterDist > rule.valueA;
			case BiomeRule::SunExposureLess:    return sunExposure < rule.valueA;
			case BiomeRule::SunExposureGreater: return sunExposure > rule.valueA;
			case BiomeRule::AlwaysTrue:        return true;
		}
		return false;
	}
};

struct TerrainSample {
	glm::vec3 position{0};
	float height = 0;
	float slope = 0;
	float waterDistance = 1000.0f;
	float sunExposure = 0.5f;
	bool excluded = false;
};

class TerrainQuery {
public:
	using HeightFunc = std::function<float(float, float)>;
	using SlopeFunc = std::function<float(float, float)>;
	using WaterDistFunc = std::function<float(float, float)>;
	using ExclusionFunc = std::function<bool(float, float)>;

	void setHeightQuery(HeightFunc fn) { m_heightFn = std::move(fn); }
	void setSlopeQuery(SlopeFunc fn) { m_slopeFn = std::move(fn); }
	void setWaterDistance(WaterDistFunc fn) { m_waterFn = std::move(fn); }
	void setExclusion(ExclusionFunc fn) { m_exclusionFn = std::move(fn); }
	void setSunDirection(const glm::vec3& dir) { m_sunDir = glm::normalize(dir); }

	TerrainSample sample(float x, float z) const {
		TerrainSample s;
		s.position = {x, 0, z};
		s.height = m_heightFn ? m_heightFn(x, z) : 0;
		s.slope = m_slopeFn ? m_slopeFn(x, z) : 0;
		s.waterDistance = m_waterFn ? m_waterFn(x, z) : 1000.0f;
		s.sunExposure = computeSunExposure(x, z);
		s.excluded = m_exclusionFn ? m_exclusionFn(x, z) : false;
		s.position.y = s.height;
		return s;
	}

private:
	float computeSunExposure(float x, float z) const {
		if (!m_slopeFn || !m_heightFn) return 0.5f;
		float slope = m_slopeFn(x, z);
		glm::vec3 normal(0, 1, 0);
		if (slope > 0.001f) {
			float hL = m_heightFn(x - 0.1f, z);
			float hR = m_heightFn(x + 0.1f, z);
			float hD = m_heightFn(x, z - 0.1f);
			float hU = m_heightFn(x, z + 0.1f);
			normal = glm::normalize(glm::vec3((hL - hR) / 0.2f, 1.0f, (hD - hU) / 0.2f));
		}
		return std::max(0.0f, glm::dot(normal, m_sunDir)) * 0.7f + 0.3f;
	}

	HeightFunc m_heightFn;
	SlopeFunc m_slopeFn;
	WaterDistFunc m_waterFn;
	ExclusionFunc m_exclusionFn;
	glm::vec3 m_sunDir{0.3f, 0.8f, 0.5f};
};

class BiomeSystem {
public:
	static BiomeSystem& instance() { static BiomeSystem bs; return bs; }

	void addLayer(const BiomeLayer& layer) {
		m_layers.push_back(layer);
		std::sort(m_layers.begin(), m_layers.end(),
		          [](const BiomeLayer& a, const BiomeLayer& b) { return a.priority > b.priority; });
	}

	void removeLayer(const std::string& name) {
		m_layers.erase(std::remove_if(m_layers.begin(), m_layers.end(),
			[&](const BiomeLayer& l) { return l.name == name; }), m_layers.end());
	}

	const BiomeLayer* evaluate(const TerrainSample& sample) const {
		if (sample.excluded) return nullptr;

		for (auto& layer : m_layers) {
			if (!layer.enabled) continue;
			if (layer.evaluate(sample.position, sample.slope, sample.height,
			                  sample.waterDistance, sample.sunExposure)) {
				return &layer;
			}
		}
		return nullptr;
	}

	struct PlacementResult {
		uint32_t typeId = 0;
		float scale = 1.0f;
		float rotation = 0;
	};

	std::vector<PlacementResult> generatePlacement(const TerrainSample& sample, uint32_t seed) const {
		std::vector<PlacementResult> results;
		const BiomeLayer* layer = evaluate(sample);
		if (!layer || layer->vegetationTypes.empty()) return results;

		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> dist(0, 1);

		if (dist(rng) > layer->density) return results;

		for (auto& [typeId, probability] : layer->vegetationTypes) {
			if (dist(rng) > probability) continue;

			PlacementResult r;
			r.typeId = typeId;
			r.scale = layer->minScale + dist(rng) * (layer->maxScale - layer->minScale);
			r.rotation = dist(rng) * 2.0f * 3.14159265f;
			results.push_back(r);
			break;
		}

		return results;
	}

	void setTerrainQuery(std::shared_ptr<TerrainQuery> query) { m_terrain = query; }
	TerrainQuery* terrain() { return m_terrain.get(); }
	const TerrainQuery* terrain() const { return m_terrain.get(); }

	void clear() { m_layers.clear(); }
	size_t layerCount() const { return m_layers.size(); }
	const std::vector<BiomeLayer>& layers() const { return m_layers; }

	void saveToFile(const std::string& path) const;
	bool loadFromFile(const std::string& path);

private:
	std::vector<BiomeLayer> m_layers;
	std::shared_ptr<TerrainQuery> m_terrain;
};

} // namespace tucano::veg
