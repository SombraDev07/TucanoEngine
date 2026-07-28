#pragma once

#include "Vegetation/VegetationSystem.h"
#include "Vegetation/LODManager.h"
#include "Vegetation/BillboardBaker.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace tucano::veg {

struct LODAsset {
	std::string path;
	VegLODLevel level = VegLODLevel::Full3D;
	uint32_t triangleCount = 0;
	uint32_t vertexCount = 0;
	float reductionRatio = 1.0f;
};

struct BakeConfig {
	std::string outputDir = "BakedVegetation/";
	uint32_t billboardResolution = 64;
	uint32_t billboardViews = 8;
	float lod0Ratio = 1.0f;
	float lod1Ratio = 0.3f;
	float lod2Ratio = 0.1f;
	bool generateBillboards = true;
	bool generateLODs = true;
	bool generateImpostors = false;
};

class BatchBaker {
public:
	BatchBaker() = default;

	void configure(const BakeConfig& cfg) { m_config = cfg; }
	BakeConfig& config() { return m_config; }

	struct BakeResult {
		uint32_t typeId = 0;
		std::string typeName;
		std::vector<LODAsset> assets;
		bool success = false;
		std::string error;
	};

	BakeResult bakeType(uint32_t typeId, const VegetationSystem& sys) {
		BakeResult result;
		result.typeId = typeId;
		result.success = true;

		auto* type = sys.type(typeId);
		if (!type) {
			result.error = "Type not found";
			result.success = false;
			return result;
		}
		result.typeName = type->name;

		if (m_config.generateLODs) {
			LODAsset full3D;
			full3D.path = m_config.outputDir + type->name + "_LOD0.mesh";
			full3D.level = VegLODLevel::Full3D;
			full3D.reductionRatio = m_config.lod0Ratio;
			full3D.triangleCount = 100;
			full3D.vertexCount = 300;
			result.assets.push_back(full3D);

			LODAsset simplified;
			simplified.path = m_config.outputDir + type->name + "_LOD1.mesh";
			simplified.level = VegLODLevel::Simplified;
			simplified.reductionRatio = m_config.lod1Ratio;
			simplified.triangleCount = uint32_t(100 * m_config.lod1Ratio);
			simplified.vertexCount = uint32_t(300 * m_config.lod1Ratio);
			result.assets.push_back(simplified);
		}

		if (m_config.generateBillboards) {
			LODAsset billboard;
			billboard.path = m_config.outputDir + type->name + "_billboard.atlas";
			billboard.level = VegLODLevel::Billboard;
			billboard.reductionRatio = m_config.lod2Ratio;
			billboard.triangleCount = 2;
			billboard.vertexCount = 4;
			result.assets.push_back(billboard);
		}

		return result;
	}

	void bakeAll(const VegetationSystem& sys,
	             std::function<void(uint32_t, uint32_t)> progressCb = nullptr) {
		uint32_t total = sys.typeCount();
		for (uint32_t i = 0; i < total; ++i) {
			auto result = bakeType(i, sys);
			if (progressCb) progressCb(i + 1, total);
		}
	}

	void generateBillboardAtlas(const VegetationSystem& sys, BillboardAtlas& atlas) {
		for (uint32_t i = 0; i < sys.typeCount(); ++i) {
			auto* type = sys.type(i);
			if (!type) continue;
			atlas.registerType(i, type->name, m_config.billboardViews);
		}
	}

	bool exportManifest(const std::string& path, const std::vector<BakeResult>& results) {
		std::ofstream f(path);
		if (!f) return false;

		f << "{\n  \"bakedAt\": \"" << "2026-07-27" << "\",\n";
		f << "  \"types\": [\n";
		for (size_t i = 0; i < results.size(); ++i) {
			auto& r = results[i];
			f << "    {\n";
			f << "      \"name\": \"" << r.typeName << "\",\n";
			f << "      \"assets\": [\n";
			for (size_t j = 0; j < r.assets.size(); ++j) {
				auto& a = r.assets[j];
				f << "        {\"path\":\"" << a.path << "\",\"lod\":" << int(a.level)
				  << ",\"tris\":" << a.triangleCount << "}";
				if (j < r.assets.size() - 1) f << ",";
				f << "\n";
			}
			f << "      ]\n";
			f << "    }";
			if (i < results.size() - 1) f << ",";
			f << "\n";
		}
		f << "  ]\n}\n";
		return true;
	}

private:
	BakeConfig m_config;
};

} // namespace tucano::veg
