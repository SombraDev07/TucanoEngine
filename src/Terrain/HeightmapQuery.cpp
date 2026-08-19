#include "Terrain/HeightmapQuery.h"

#include <cstring>

namespace tucano::terrain {

struct GpuHeightQuery {
	float worldX, worldZ;
	float height;
	float pad;
};

HeightmapQuery::HeightmapQuery(rhi::Device& device) {
	rhi::BufferDesc qbDesc{};
	qbDesc.size = kMaxQueries * sizeof(GpuHeightQuery);
	qbDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
	qbDesc.stride = sizeof(GpuHeightQuery);
	qbDesc.debugName = "HeightQueryBuf";
	m_queryBuffer = device.createBuffer(qbDesc);

	// The query buffer above is a DEFAULT-heap UAV, so it is not CPU-mappable — the original code
	// wrote the query coordinates through mapped() and it silently no-op'd, leaving the shader to read
	// zeros and sample the (0,0) corner. This upload buffer stages the coordinates; dispatch copies it
	// into the UAV before the shader runs. Same input→copy→UAV pattern GpuCellCuller uses.
	rhi::BufferDesc upDesc{};
	upDesc.size = kMaxQueries * sizeof(GpuHeightQuery);
	upDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::Upload;
	upDesc.stride = sizeof(GpuHeightQuery);
	upDesc.debugName = "HeightQueryUpload";
	m_queryUpload = device.createBuffer(upDesc);

	for (uint32_t i = 0; i < kRingFrames; ++i) {
		rhi::BufferDesc rbDesc{};
		rbDesc.size = kMaxQueries * sizeof(GpuHeightQuery);
		rbDesc.usage = rhi::BufferUsage::Readback;
		rbDesc.debugName = "HeightQueryRB" + std::to_string(i);
		m_readbackBuffers[i] = device.createBuffer(rbDesc);
	}

	rhi::BufferDesc cbDesc{};
	cbDesc.size = 32;
	cbDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
	cbDesc.debugName = "HeightQueryCB";
	m_queryCB = device.createBuffer(cbDesc);

	m_rootSig = device.createComputeRootSignature();
	rhi::ComputePipelineDesc csDesc{};
	csDesc.rootSignature = m_rootSig;
	csDesc.cs = rhi::ShaderBytecode::loadFromFile("shaders/HeightQuery_CSMain.cso");
	m_queryPSO = device.createComputePipeline(csDesc);

	m_requests.reserve(kMaxQueries);
}

void HeightmapQuery::submit(float worldX, float worldZ) {
	if (m_requests.size() >= kMaxQueries) return;
	HeightQueryRequest req;
	req.worldX = worldX;
	req.worldZ = worldZ;
	req.resultHeight = 0.0f;
	req.frameIssued = m_currentFrame;
	m_requests.push_back(req);
	++m_pendingCount;
}

float HeightmapQuery::getResult(uint32_t queryIndex) const {
	if (queryIndex >= m_requests.size()) return 0.0f;
	return m_requests[queryIndex].resultHeight;
}

void HeightmapQuery::dispatch(rhi::Device& device, rhi::CommandList& cmd, const Heightmap& hm) {
	if (m_pendingCount == 0) return;

	rhi::Buffer& queryBuf = *m_queryBuffer;

	// Stage the query coordinates in the CPU-visible upload buffer, then copy them into the UAV the
	// shader reads. Writing straight to the UAV is impossible — it lives on the default heap.
	GpuHeightQuery* mapped = static_cast<GpuHeightQuery*>(m_queryUpload->mapped());
	if (mapped) {
		for (uint32_t i = 0; i < m_pendingCount && i < kMaxQueries; ++i) {
			mapped[i].worldX = m_requests[i].worldX;
			mapped[i].worldZ = m_requests[i].worldZ;
			mapped[i].height = 0.0f;
			mapped[i].pad = 0.0f;
		}
		cmd.copyBuffer(*m_queryBuffer, 0, *m_queryUpload, 0, m_pendingCount * sizeof(GpuHeightQuery));
	}
	cmd.transition(*m_queryBuffer, rhi::ResourceState::UnorderedAccess);

	struct QueryCB {
		float invWSX, invWSY;
		float offX, offY;
		uint32_t hmIndex;
		uint32_t count;
	} cb;
	// The heightmap stores texel i at world i/(res-1)*worldSize (its CPU sampleHeight convention). The
	// GPU sampler reads texel centres at (i+0.5)/res. Mapping world → (res-1)/(res*worldSize) plus a
	// 0.5/res offset lands the sample on the same point, so the GPU query agrees with the mesh/physics.
	const float res = float(hm.resolution());
	const float scale = (res - 1.0f) / (res * hm.worldSize());
	cb.invWSX = scale;
	cb.invWSY = scale;
	cb.offX = 0.5f / res;
	cb.offY = 0.5f / res;
	cb.hmIndex = hm.bindlessIndex();
	cb.count = m_pendingCount;
	std::memcpy(m_queryCB->mapped(), &cb, sizeof(cb));

	// The shared compute root signature: param 0 is root constants (b0), so the CBV binds at param 1
	// (register b1). Setting the root signature is mandatory — SetPipelineState does not do it, and
	// without it the root bindings resolve against whatever was last set and the GPU faults.
	cmd.setRootSignature(*m_rootSig);
	cmd.setPipeline(*m_queryPSO);
	cmd.setComputeRootCBV(1, *m_queryCB);
	cmd.setComputeRootSrvTable(2, 0);
	// Bind the sampler table (param 4). Without it s0 in the shader is unbound and SampleLevel reads
	// through an undefined sampler — the source of the systematic height error before this.
	cmd.setComputeRootSamplerTable(4, 0);
	cmd.transition(queryBuf, rhi::ResourceState::UnorderedAccess);
	rhi::Buffer* uavs[] = {&queryBuf};
	cmd.setComputeRootUavTable(3, device.writeBufferUavTable(uavs));

	uint32_t groups = (m_pendingCount + 63) / 64;
	cmd.dispatch(groups, 1, 1);
	cmd.uavBarrier(nullptr);

	rhi::Buffer& rb = *m_readbackBuffers[m_readbackFrame];
	cmd.copyBuffer(rb, 0, queryBuf, 0, m_pendingCount * sizeof(GpuHeightQuery));

	m_readbackFrame = (m_readbackFrame + 1) % kRingFrames;
	++m_currentFrame;
}

void HeightmapQuery::readback(rhi::Device& device) {
	uint32_t frame = (m_readbackFrame + kRingFrames - 2) % kRingFrames;
	rhi::Buffer& rb = *m_readbackBuffers[frame];
	GpuHeightQuery* data = static_cast<GpuHeightQuery*>(rb.mapped());
	if (!data) return;

	for (uint32_t i = 0; i < m_pendingCount && i < kMaxQueries; ++i) {
		if (m_requests[i].frameIssued + 2 <= m_currentFrame) {
			m_requests[i].resultHeight = data[i].height;
		}
	}

	m_requests.erase(
		std::remove_if(m_requests.begin(), m_requests.end(),
		               [this](const HeightQueryRequest& r) { return r.frameIssued + 3 < m_currentFrame; }),
		m_requests.end());
	m_pendingCount = std::min(uint32_t(m_requests.size()), kMaxQueries);
}

} // namespace tucano::terrain
