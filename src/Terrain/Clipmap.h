#pragma once

#include "Terrain/ClipmapTypes.h"
#include "Terrain/Heightmap.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <memory>

namespace tucano::terrain {

class Clipmap {
public:
	void init(rhi::Device& device);
	void updateCB(rhi::Device& device, const Heightmap& hm, const glm::vec3& cameraPos,
	              uint32_t holeMaskIdx = ~0u);

	rhi::Buffer& vertexBuffer() { return *m_vertexBuffer; }
	rhi::Buffer& indexBuffer() { return *m_indexBuffer; }
	rhi::Buffer& clipmapCB() { return *m_clipmapCB; }
	rhi::PipelineState& generatePSO() { return *m_generatePSO; }

	void cullRings(const glm::mat4& viewProj);
	bool isRingVisible(uint32_t ringIdx) const { return (m_visibleMask >> ringIdx) & 1u; }
	uint32_t visibleMask() const { return m_visibleMask; }

	static constexpr uint32_t ringCount() { return kClipmapRingCount; }
	static constexpr uint32_t vertsPerRing() { return kClipmapRingVerts; }
	static constexpr uint32_t indicesPerRing() { return kClipmapIndicesPerRing; }

private:
	void createIndexBuffer(rhi::Device& device);
	void createVertexBuffer(rhi::Device& device);

	std::shared_ptr<rhi::Buffer> m_vertexBuffer;
	std::shared_ptr<rhi::Buffer> m_indexBuffer;
	std::shared_ptr<rhi::Buffer> m_clipmapCB;
	std::shared_ptr<rhi::PipelineState> m_generatePSO;

	ClipmapCB m_cachedCB{};
	uint32_t m_visibleMask = 0xFF;
};

} // namespace tucano::terrain
