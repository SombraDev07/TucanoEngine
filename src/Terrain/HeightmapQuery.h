#pragma once

#include "Terrain/Heightmap.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace tucano::terrain {

struct HeightQueryRequest {
	float worldX, worldZ;
	float resultHeight;
	uint32_t frameIssued;
};

class HeightmapQuery {
public:
	static constexpr uint32_t kMaxQueries = 256;
	static constexpr uint32_t kRingFrames = 3;

	HeightmapQuery(rhi::Device& device);

	void submit(float worldX, float worldZ);
	float getResult(uint32_t queryIndex) const;
	uint32_t pendingCount() const { return m_pendingCount; }

	void dispatch(rhi::Device& device, rhi::CommandList& cmd, const Heightmap& hm);
	void readback(rhi::Device& device);

private:
	std::shared_ptr<rhi::Buffer> m_queryBuffer;
	std::shared_ptr<rhi::Buffer> m_queryUpload; ///< CPU-writable staging for the query coords
	std::shared_ptr<rhi::Buffer> m_readbackBuffers[kRingFrames];
	std::shared_ptr<rhi::Buffer> m_queryCB;
	std::shared_ptr<rhi::RootSignature> m_rootSig;
	std::shared_ptr<rhi::PipelineState> m_queryPSO;

	std::vector<HeightQueryRequest> m_requests;
	uint32_t m_pendingCount = 0;
	uint32_t m_currentFrame = 0;
	uint32_t m_readbackFrame = 0;
};

} // namespace tucano::terrain
