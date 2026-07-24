#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace tucano::terrain {

class Heightmap {
public:
	static std::shared_ptr<Heightmap> createFlat(rhi::Device& device, uint32_t resolution, float worldSize, float height = 0.0f);
	static std::shared_ptr<Heightmap> createFromData(rhi::Device& device, uint32_t resolution, float worldSize, const std::vector<float>& data);
	static std::shared_ptr<Heightmap> loadFromFile(rhi::Device& device, const std::string& path);
	static bool saveToFile(const Heightmap& hm, const std::string& path);

	void uploadToGPU(rhi::Device& device);

	float sampleHeight(float worldX, float worldZ) const;
	glm::vec3 sampleNormal(float worldX, float worldZ) const;
	float sampleHeightNearest(int x, int z) const;

	uint32_t resolution() const { return m_resolution; }
	float worldSize() const { return m_worldSize; }
	float minHeight() const { return m_minHeight; }
	float maxHeight() const { return m_maxHeight; }
	float texelSize() const { return m_worldSize / float(m_resolution); }
	const std::vector<float>& data() const { return m_data; }
	rhi::Texture* gpuTexture() const { return m_gpuTexture.get(); }
	uint32_t bindlessIndex() const;

	void setHeight(uint32_t x, uint32_t z, float h);
	bool isDirty() const { return m_dirty; }
	void markClean() { m_dirty = false; }

	float* dataPtr() { return m_data.data(); }
	const float* dataPtr() const { return m_data.data(); }

	void recomputeBounds();

	uint32_t m_resolution = 0;
	float m_worldSize = 0.0f;
	float m_minHeight = 0.0f;
	float m_maxHeight = 0.0f;
	std::vector<float> m_data;
	std::shared_ptr<rhi::Texture> m_gpuTexture;
	bool m_dirty = true;
};

} // namespace tucano::terrain
