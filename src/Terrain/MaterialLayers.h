#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace tucano::terrain {

inline constexpr uint32_t kMaxMaterialLayers = 8;

struct MaterialLayerDef {
	float  slopeMin = 0.0f;
	float  slopeMax = 1.0f;
	float  heightMin = -9999.0f;
	float  heightMax = 9999.0f;
	float  uvScale = 1.0f;
	float  roughness = 0.8f;
	float  metallic = 0.0f;
	float  normalStrength = 1.0f;
	float  albedoR = 0.5f;
	float  albedoG = 0.5f;
	float  albedoB = 0.5f;
	uint32_t triplanar = 0;
	uint32_t textureIndex = 0;
};

struct MaterialLayerGpu {
	float  slopeMin;
	float  slopeMax;
	float  heightMin;
	float  heightMax;
	float  uvScale;
	float  roughness;
	float  metallic;
	float  normalStrength;
	float  albedoR;
	float  albedoG;
	float  albedoB;
	uint32_t triplanar;
	uint32_t textureIndex;
	float  pad[2];
};

struct MaterialLayerCB {
	uint32_t layerCount;
	float    heightScale;
	float    heightBias;
	float    pad0;
	MaterialLayerGpu layers[kMaxMaterialLayers];
};

class MaterialLayers {
public:
	MaterialLayers();

	void setDefaultGrassRock();

	MaterialLayerDef& operator[](uint32_t i) { return m_layers[i]; }
	const MaterialLayerDef& operator[](uint32_t i) const { return m_layers[i]; }
	uint32_t count() const { return m_count; }
	void setCount(uint32_t c) { m_count = c < kMaxMaterialLayers ? c : kMaxMaterialLayers; }

	void fillCB(MaterialLayerCB& cb, float heightmapMin, float heightmapMax) const;

private:
	MaterialLayerDef m_layers[kMaxMaterialLayers];
	uint32_t m_count = 0;
};

} // namespace tucano::terrain
