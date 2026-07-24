#include "Terrain/HeightmapQuery.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12CommandList.h"

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

	rhi::ComputePipelineDesc csDesc{};
	csDesc.rootSignature = device.createComputeRootSignature();
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

	auto& dxDevice = static_cast<rhi::DX12Device&>(device);
	auto& queryBuf = static_cast<rhi::DX12Buffer&>(*m_queryBuffer);

	GpuHeightQuery* mapped = static_cast<GpuHeightQuery*>(m_queryBuffer->mapped());
	if (!mapped) {
		cmd.transition(*m_queryBuffer, rhi::ResourceState::UnorderedAccess);
	} else {
		for (uint32_t i = 0; i < m_pendingCount && i < kMaxQueries; ++i) {
			mapped[i].worldX = m_requests[i].worldX;
			mapped[i].worldZ = m_requests[i].worldZ;
			mapped[i].height = 0.0f;
			mapped[i].pad = 0.0f;
		}
	}

	struct QueryCB {
		float invWSX, invWSY;
		uint32_t hmIndex;
		uint32_t count;
	} cb;
	cb.invWSX = 1.0f / hm.worldSize();
	cb.invWSY = 1.0f / hm.worldSize();
	cb.hmIndex = hm.bindlessIndex();
	cb.count = m_pendingCount;
	std::memcpy(m_queryCB->mapped(), &cb, sizeof(cb));

	cmd.setPipeline(*m_queryPSO);
	cmd.setComputeRootCBV(0, *m_queryCB);
	cmd.setComputeRootSrvTable(2, 0);
	cmd.transition(queryBuf, rhi::ResourceState::UnorderedAccess);
	D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {queryBuf.uavCpu};
	cmd.setComputeRootUavTable(3, dxDevice.writeUavTable(uavs, 1));

	uint32_t groups = (m_pendingCount + 63) / 64;
	cmd.dispatch(groups, 1, 1);
	cmd.uavBarrier(nullptr);

	auto& rb = static_cast<rhi::DX12Buffer&>(*m_readbackBuffers[m_readbackFrame]);
	cmd.copyBuffer(rb, 0, queryBuf, 0, m_pendingCount * sizeof(GpuHeightQuery));

	m_readbackFrame = (m_readbackFrame + 1) % kRingFrames;
	++m_currentFrame;
}

void HeightmapQuery::readback(rhi::Device& device) {
	uint32_t frame = (m_readbackFrame + kRingFrames - 2) % kRingFrames;
	auto& rb = static_cast<rhi::DX12Buffer&>(*m_readbackBuffers[frame]);
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
