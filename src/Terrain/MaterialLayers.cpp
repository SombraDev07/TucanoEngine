#include "Terrain/MaterialLayers.h"

#include <algorithm>

namespace tucano::terrain {

MaterialLayers::MaterialLayers() {
	setDefaultGrassRock();
}

void MaterialLayers::setDefaultGrassRock() {
	m_count = 5;

	m_layers[0] = {0.0f, 0.15f, -9999, 9999, 0.5f, 0.85f, 0.0f, 1.0f, 0.18f, 0.40f, 0.10f, 0, 0};
	m_layers[1] = {0.15f, 0.35f, -9999, 9999, 0.4f, 0.80f, 0.0f, 0.8f, 0.25f, 0.22f, 0.12f, 0, 0};
	m_layers[2] = {0.35f, 0.55f, -9999, 9999, 0.3f, 0.70f, 0.02f, 0.7f, 0.40f, 0.35f, 0.28f, 0, 0};
	m_layers[3] = {0.55f, 0.80f, -9999, 9999, 0.25f, 0.65f, 0.05f, 0.6f, 0.50f, 0.45f, 0.38f, 1, 0};
	m_layers[4] = {0.80f, 1.10f, -9999, 9999, 0.20f, 0.55f, 0.10f, 0.5f, 0.55f, 0.50f, 0.45f, 1, 0};
}

void MaterialLayers::fillCB(MaterialLayerCB& cb, float heightmapMin, float heightmapMax) const {
	cb.layerCount = m_count;
	cb.heightScale = 1.0f / std::max(heightmapMax - heightmapMin, 1.0f);
	cb.heightBias = -heightmapMin * cb.heightScale;
	cb.pad0 = 0.0f;

	for (uint32_t i = 0; i < m_count; ++i) {
		cb.layers[i].slopeMin = m_layers[i].slopeMin;
		cb.layers[i].slopeMax = m_layers[i].slopeMax;
		cb.layers[i].heightMin = m_layers[i].heightMin;
		cb.layers[i].heightMax = m_layers[i].heightMax;
		cb.layers[i].uvScale = m_layers[i].uvScale;
		cb.layers[i].roughness = m_layers[i].roughness;
		cb.layers[i].metallic = m_layers[i].metallic;
		cb.layers[i].normalStrength = m_layers[i].normalStrength;
		cb.layers[i].albedoR = m_layers[i].albedoR;
		cb.layers[i].albedoG = m_layers[i].albedoG;
		cb.layers[i].albedoB = m_layers[i].albedoB;
		cb.layers[i].triplanar = m_layers[i].triplanar;
		cb.layers[i].textureIndex = m_layers[i].textureIndex;
		cb.layers[i].pad[0] = 0.0f;
		cb.layers[i].pad[1] = 0.0f;
	}
}

} // namespace tucano::terrain
