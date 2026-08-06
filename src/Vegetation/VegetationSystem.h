#pragma once

#include "Vegetation/VegetationGPU.h"
#include "Vegetation/BiomeSystem.h"
#include "Vegetation/ProceduralPlacement.h"
#include "Vegetation/DensityMap.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

namespace tucano::veg {

struct VegetationInstance {
	glm::vec3 position{0};
	glm::vec3 scale{1};
	float rotation = 0;
	uint32_t typeId = 0;
	uint32_t instanceId = 0; // stable id across frames
	float health = 1.0f;
	uint32_t flags = 0;
};

struct VegetationType {
	std::string name;
	std::string meshPath;
	std::string billboardPath;
	float minScale = 0.7f;
	float maxScale = 1.3f;
	float windFlexibility = 0.5f;
	float windHeight = 1.0f;
	float windPhase = 0.0f;
	float cullDistance = 150.0f;
	float shadowDistance = 50.0f;
	float lodDistance0 = 30.0f;
	float lodDistance1 = 80.0f;
	float lodDistance2 = 150.0f;
	bool receiveDecals = true;
	bool castShadows = true;
	/// 0 = auto (grass cards for "grass-like" names, plant otherwise), 1 = grass, 2 = plant
	uint32_t proceduralKind = 0;
};

enum class VegLOD { Full = 0, Simplified = 1, Billboard = 2, Culled = 3 };

struct VegetationCell {
	glm::vec3 worldOrigin{0};
	float cellSize = 64.0f;
	std::vector<VegetationInstance> instances;
	bool loaded = false;
	bool dirty = true;
};

struct VegetationConfig {
	float globalWindStrength = 1.0f;
	float globalWindSpeed = 0.5f;
	glm::vec3 windDirection{1, 0, 0};
	bool enableWind = true;
	bool enableGPUWind = true;
	bool enableShadows = true;
	uint32_t maxInstances = 100000;
	float densityScale = 1.0f;
	float cellSize = 64.0f;
};

/// Viewport paint state consumed by the runtime (mouse → world → place/erase).
struct VegetationPaintState {
	bool enabled = false;
	enum class Mode : int {
		PlaceBrush = 0,
		EraseBrush = 1,
		PlaceSingle = 2,
		DensityPaint = 3,
		DensityErase = 4
	};
	Mode mode = Mode::PlaceBrush;
	float brushRadius = 5.0f;
	float brushStrength = 0.6f;
	float plantsPerSquareMeter = 2.0f;
	int typeId = 0;
	bool meshDirty = false; // editor asked to (re)load meshPath for typeId
};

class VegetationSystem {
public:
	static VegetationSystem& instance() {
		static VegetationSystem vs;
		return vs;
	}

	void configure(const VegetationConfig& cfg) {
		m_config = cfg;
		m_configDirty = true;
	}
	VegetationConfig& config() { return m_config; }
	const VegetationConfig& config() const { return m_config; }

	uint32_t registerType(const VegetationType& type) {
		uint32_t id = uint32_t(m_types.size());
		m_types.push_back(type);
		bumpRevision();
		return id;
	}

	const VegetationType* type(uint32_t id) const {
		return id < m_types.size() ? &m_types[id] : nullptr;
	}
	VegetationType* typeMutable(uint32_t id) {
		return id < m_types.size() ? &m_types[id] : nullptr;
	}
	uint32_t typeCount() const { return uint32_t(m_types.size()); }

	void addInstance(int32_t cellX, int32_t cellZ, VegetationInstance inst) {
		if (inst.instanceId == 0) inst.instanceId = m_nextInstanceId++;
		auto key = cellKey(cellX, cellZ);
		auto& cell = m_cells[key];
		if (!cell.loaded) {
			cell.worldOrigin = {float(cellX) * m_config.cellSize, 0, float(cellZ) * m_config.cellSize};
			cell.cellSize = m_config.cellSize;
			cell.loaded = true;
		}
		cell.instances.push_back(inst);
		cell.dirty = true;
		++m_cachedCount;
		bumpRevision();
	}

	void removeInstances(int32_t cellX, int32_t cellZ) {
		auto it = m_cells.find(cellKey(cellX, cellZ));
		if (it != m_cells.end()) {
			m_cachedCount -= uint32_t(it->second.instances.size());
			it->second.instances.clear();
			it->second.dirty = true;
			bumpRevision();
		}
	}

	/// Place one plant at world position (snaps into the owning cell).
	uint32_t placeAt(const glm::vec3& worldPos, uint32_t typeId, float rotation = -1.0f,
	                 float scale = -1.0f) {
		if (typeId >= m_types.size()) return 0;
		const auto& t = m_types[typeId];
		const float cellSize = m_config.cellSize;
		const int32_t cx = int32_t(std::floor(worldPos.x / cellSize));
		const int32_t cz = int32_t(std::floor(worldPos.z / cellSize));

		VegetationInstance inst;
		inst.position = worldPos;
		inst.typeId = typeId;
		inst.rotation = rotation >= 0.0f ? rotation : float(m_rng() % 10000) / 10000.0f * 6.28318f;
		float s = scale;
		if (s < 0.0f) {
			std::uniform_real_distribution<float> distScale(t.minScale, t.maxScale);
			s = distScale(m_rng);
		}
		inst.scale = {s, s, s};
		inst.health = 1.0f;
		inst.flags = 1;
		inst.instanceId = m_nextInstanceId++;
		addInstance(cx, cz, inst);
		return inst.instanceId;
	}

	/// Scatter plants in a brush circle (density ≈ plants / m² × strength).
	uint32_t paintBrush(const glm::vec3& center, float radius, uint32_t typeId, float strength,
	                    float plantsPerM2,
	                    const std::function<float(float, float)>& heightFn = nullptr) {
		if (typeId >= m_types.size() || radius <= 0.0f) return 0;
		const float area = 3.14159265f * radius * radius;
		uint32_t target = uint32_t(std::max(1.0f, area * plantsPerM2 * strength));
		target = std::min(target, 64u);

		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		uint32_t placed = 0;
		for (uint32_t i = 0; i < target; ++i) {
			const float a = dist01(m_rng) * 6.28318f;
			const float r = radius * std::sqrt(dist01(m_rng));
			const float x = center.x + std::cos(a) * r;
			const float z = center.z + std::sin(a) * r;

			if (m_exclusionZones && m_exclusionZones->isExcluded(x, z)) continue;
			if (m_densityMap) {
				float d = m_densityMap->sample(x, z);
				if (dist01(m_rng) > d) continue;
			}

			float y = center.y;
			if (heightFn) y = heightFn(x, z);
			placeAt({x, y, z}, typeId);
			++placed;
		}
		return placed;
	}

	/// Remove instances whose XZ distance to center is ≤ radius.
	uint32_t eraseBrush(const glm::vec3& center, float radius) {
		const float r2 = radius * radius;
		uint32_t removed = 0;
		for (auto& [_, cell] : m_cells) {
			auto& v = cell.instances;
			const size_t before = v.size();
			v.erase(std::remove_if(v.begin(), v.end(),
			                       [&](const VegetationInstance& inst) {
				                       const float dx = inst.position.x - center.x;
				                       const float dz = inst.position.z - center.z;
				                       return dx * dx + dz * dz <= r2;
			                       }),
			        v.end());
			if (v.size() != before) {
				removed += uint32_t(before - v.size());
				cell.dirty = true;
			}
		}
		if (removed) bumpRevision();
		m_cachedCount = recount();
		return removed;
	}

	void scatter(int32_t cellX, int32_t cellZ, uint32_t typeId, uint32_t count, uint32_t seed = 42,
	             const std::function<float(float, float)>& heightFn = nullptr) {
		if (typeId >= m_types.size()) return;
		auto& type = m_types[typeId];
		const float cellSize = m_config.cellSize;
		std::mt19937 rng(seed + uint32_t(cellX) * 7919u + uint32_t(cellZ) * 6271u);
		std::uniform_real_distribution<float> distPos(0, cellSize);
		std::uniform_real_distribution<float> distRot(0, 6.28318f);
		std::uniform_real_distribution<float> distScale(type.minScale, type.maxScale);
		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

		auto& cell = m_cells[cellKey(cellX, cellZ)];
		cell.worldOrigin = {float(cellX) * cellSize, 0, float(cellZ) * cellSize};
		cell.cellSize = cellSize;
		cell.loaded = true;

		for (uint32_t i = 0; i < count; ++i) {
			const float lx = distPos(rng);
			const float lz = distPos(rng);
			const float wx = cell.worldOrigin.x + lx;
			const float wz = cell.worldOrigin.z + lz;

			if (m_exclusionZones && m_exclusionZones->isExcluded(wx, wz)) continue;
			if (m_densityMap) {
				float d = m_densityMap->sample(wx, wz);
				if (dist01(rng) > d) continue;
			}

			float wy = 0.0f;
			if (heightFn) wy = heightFn(wx, wz);

			VegetationInstance inst;
			inst.position = {wx, wy, wz};
			inst.rotation = distRot(rng);
			float s = distScale(rng);
			inst.scale = {s, s, s};
			inst.typeId = typeId;
			inst.health = 1.0f;
			inst.flags = 1;
			inst.instanceId = m_nextInstanceId++;
			cell.instances.push_back(inst);
			++m_cachedCount;
		}
		cell.dirty = true;
		bumpRevision();
	}

	std::vector<VegetationInstance> queryVisible(const glm::vec3& cameraPos, float radius) const {
		std::vector<VegetationInstance> result;
		const float cellSize = m_config.cellSize;
		const float r2 = radius * radius;
		int minCX = int(std::floor((cameraPos.x - radius) / cellSize));
		int maxCX = int(std::floor((cameraPos.x + radius) / cellSize));
		int minCZ = int(std::floor((cameraPos.z - radius) / cellSize));
		int maxCZ = int(std::floor((cameraPos.z + radius) / cellSize));

		for (int cx = minCX; cx <= maxCX; ++cx) {
			for (int cz = minCZ; cz <= maxCZ; ++cz) {
				auto it = m_cells.find(cellKey(cx, cz));
				if (it == m_cells.end()) continue;
				for (const auto& inst : it->second.instances) {
					const glm::vec3 d = inst.position - cameraPos;
					if (glm::dot(d, d) <= r2) result.push_back(inst);
				}
			}
		}
		return result;
	}

	std::vector<VegetationInstance> queryVisibleByType(const glm::vec3& cameraPos, float radius,
	                                                   uint32_t typeId) const {
		std::vector<VegetationInstance> result;
		const float cellSize = m_config.cellSize;
		const float r2 = radius * radius;
		int minCX = int(std::floor((cameraPos.x - radius) / cellSize));
		int maxCX = int(std::floor((cameraPos.x + radius) / cellSize));
		int minCZ = int(std::floor((cameraPos.z - radius) / cellSize));
		int maxCZ = int(std::floor((cameraPos.z + radius) / cellSize));

		for (int cx = minCX; cx <= maxCX; ++cx) {
			for (int cz = minCZ; cz <= maxCZ; ++cz) {
				auto it = m_cells.find(cellKey(cx, cz));
				if (it == m_cells.end()) continue;
				for (const auto& inst : it->second.instances) {
					if (inst.typeId != typeId) continue;
					const glm::vec3 d = inst.position - cameraPos;
					if (glm::dot(d, d) <= r2) result.push_back(inst);
				}
			}
		}
		return result;
	}

	VegLOD computeLOD(uint32_t typeId, float distance) const {
		if (typeId >= m_types.size()) return VegLOD::Culled;
		auto& t = m_types[typeId];
		if (distance > t.cullDistance) return VegLOD::Culled;
		if (distance > t.lodDistance2) return VegLOD::Billboard;
		if (distance > t.lodDistance1) return VegLOD::Simplified;
		return VegLOD::Full;
	}

	uint32_t instanceCount() const { return m_cachedCount; }

	void clear() {
		m_cells.clear();
		m_types.clear();
		m_cachedCount = 0;
		m_nextInstanceId = 1;
		bumpRevision();
	}

	void clearInstancesOnly() {
		m_cells.clear();
		m_cachedCount = 0;
		bumpRevision();
	}

	void scatterBiome(int32_t cellX, int32_t cellZ, const BiomeSystem& biome,
	                  const ScatterConfig& scatterCfg) {
		auto* terrain = biome.terrain();
		const float cellSize = m_config.cellSize;
		glm::vec2 regionMin(float(cellX) * cellSize, float(cellZ) * cellSize);
		glm::vec2 regionMax(regionMin.x + cellSize, regionMin.y + cellSize);

		auto& cell = m_cells[cellKey(cellX, cellZ)];
		cell.worldOrigin = {float(cellX) * cellSize, 0, float(cellZ) * cellSize};
		cell.cellSize = cellSize;
		cell.loaded = true;

		ProceduralPlacement placement;
		std::function<float(float, float)> heightFn = nullptr;
		if (terrain) {
			heightFn = [terrain](float x, float z) { return terrain->sample(x, z).height; };
		}

		for (uint32_t typeId = 0; typeId < m_types.size(); ++typeId) {
			auto results = placement.scatter(scatterCfg, regionMin, regionMax, typeId, heightFn);
			for (auto& pt : results) {
				if (!terrain) continue;
				auto sample = terrain->sample(pt.position.x, pt.position.z);
				if (sample.excluded) continue;
				if (m_exclusionZones && m_exclusionZones->isExcluded(pt.position.x, pt.position.z))
					continue;
				if (m_densityMap) {
					float d = m_densityMap->sample(pt.position.x, pt.position.z);
					if (d < 0.01f) continue;
				}

				const BiomeLayer* layer = biome.evaluate(sample);
				if (!layer) continue;

				bool typeInLayer = false;
				for (auto& [tid, prob] : layer->vegetationTypes) {
					if (tid == typeId) {
						typeInLayer = true;
						break;
					}
				}
				if (!typeInLayer) continue;

				VegetationInstance inst;
				inst.position = sample.position;
				inst.scale = glm::vec3(pt.scale);
				inst.rotation = pt.rotation;
				inst.typeId = typeId;
				inst.health = 1.0f;
				inst.flags = 1;
				inst.instanceId = m_nextInstanceId++;
				cell.instances.push_back(inst);
				++m_cachedCount;
			}
		}
		cell.dirty = true;
		bumpRevision();
	}

	void setDensityMap(std::shared_ptr<DensityMap> map) { m_densityMap = map; }
	DensityMap* densityMap() { return m_densityMap.get(); }

	void setExclusionZones(std::shared_ptr<ExclusionZone> zones) { m_exclusionZones = zones; }
	ExclusionZone* exclusionZones() { return m_exclusionZones.get(); }

	size_t cellCount() const { return m_cells.size(); }

	VegetationPaintState& paint() { return m_paint; }
	const VegetationPaintState& paint() const { return m_paint; }

	void worldToCell(float x, float z, int32_t& outX, int32_t& outZ) const {
		outX = int32_t(std::floor(x / m_config.cellSize));
		outZ = int32_t(std::floor(z / m_config.cellSize));
	}

	/// Bumped by every call that adds, removes or moves instances or types. Consumers cache
	/// derived data (the renderer's GPU instance arrays) against this instead of rebuilding it
	/// from scratch every frame.
	uint64_t revision() const { return m_revision; }

private:
	void bumpRevision() { ++m_revision; }

	static uint64_t cellKey(int32_t x, int32_t z) {
		return (uint64_t(uint32_t(x)) << 32) | uint32_t(z);
	}

	uint32_t recount() const {
		uint32_t count = 0;
		for (auto& [_, cell] : m_cells) count += uint32_t(cell.instances.size());
		return count;
	}

	std::unordered_map<uint64_t, VegetationCell> m_cells;
	std::vector<VegetationType> m_types;
	VegetationConfig m_config;
	bool m_configDirty = false;
	float m_windTime = 0;
	uint32_t m_cachedCount = 0;
	uint32_t m_nextInstanceId = 1;
	uint64_t m_revision = 1;
	std::mt19937 m_rng{42};
	std::shared_ptr<DensityMap> m_densityMap;
	std::shared_ptr<ExclusionZone> m_exclusionZones;
	VegetationPaintState m_paint;
};

} // namespace tucano::veg
