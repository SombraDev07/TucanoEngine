#include "Terrain/TerrainRenderer.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12CommandList.h"

#include <glm/gtc/matrix_transform.hpp>

namespace tucano::terrain {

static std::string shaderPath(const char* name) {
	return std::string(TUCANO_SHADER_DIR) + "/" + name;
}

TerrainRenderer::TerrainRenderer(rhi::Device& device, std::shared_ptr<Heightmap> heightmap)
    : m_device(device), m_heightmap(std::move(heightmap)), m_atlas(device) {
	m_clipmap.init(device);
	createPipelines(device);
	uploadTerrainMaterialCB(device);
}

void TerrainRenderer::createPipelines(rhi::Device& device) {
	m_rootSig = device.createRootSignature(true);

	auto load = [](const char* file) {
		return rhi::ShaderBytecode::loadFromFile(shaderPath(file));
	};

	rhi::GraphicsPipelineDesc d{};
	d.rootSignature = m_rootSig;
	d.vs = load("Terrain_VSMain.cso");
	d.ps = load("Terrain_PSMain.cso");
	d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB, rhi::Format::R8G8B8A8_UNORM,
	                rhi::Format::R8G8B8A8_UNORM, rhi::Format::R8G8B8A8_UNORM,
	                rhi::Format::R32_FLOAT};
	d.dsvFormat = rhi::Format::D32_FLOAT;
	d.cullMode = rhi::CullMode::None;
	m_terrainPSO = device.createGraphicsPipeline(d);
}

void TerrainRenderer::uploadTerrainMaterialCB(rhi::Device& device) {
	rhi::BufferDesc cbDesc{};
	cbDesc.size = sizeof(MaterialLayerCB) + 256;
	cbDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
	cbDesc.debugName = "TerrainMaterialCB";
	m_materialCB = device.createBuffer(cbDesc);
}

void TerrainRenderer::prepareFrame(rhi::Device& device, const Camera& camera) {
	m_clipmap.updateCB(device, *m_heightmap, camera.position(), m_holeMaskBindless);
	m_clipmap.cullRings(camera.viewProj());

	if (m_useAtlas) {
		float ts = MaterialAtlas::tileWorldSize();
		for (uint32_t r = 0; r < kClipmapRingCount; ++r) {
			if (!m_clipmap.isRingVisible(r)) continue;
			float scale = m_heightmap->worldSize() / float(kClipmapRingSize) * std::exp2f(float(r));
			float radius = scale * float(kClipmapRingSize) * 0.5f;

			int minTX = int((camera.position().x - radius) / ts);
			int maxTX = int((camera.position().x + radius) / ts);
			int minTZ = int((camera.position().z - radius) / ts);
			int maxTZ = int((camera.position().z + radius) / ts);

			for (int tz = minTZ; tz <= maxTZ; ++tz) {
				for (int tx = minTX; tx <= maxTX; ++tx) {
					TileCoord tc{uint32_t(std::max(0, tx)), uint32_t(std::max(0, tz))};
					if (!m_tileCache.isResident(tc)) {
						m_tileCache.allocate(tc);
					}
					m_tileCache.touch(tc);
				}
			}
		}

		m_tileCache.evictLRU(TileCache::kMaxResident);
		m_atlas.uploadAllTiles(device, *m_heightmap, m_tileCache);
	}

	MaterialLayerCB layerCB{};
	m_materialLayers.fillCB(layerCB, m_heightmap->minHeight(), m_heightmap->maxHeight());
	std::memcpy(m_materialCB->mapped(), &layerCB, sizeof(layerCB));
}

void TerrainRenderer::setHoleMask(uint32_t bindlessIdx) {
	m_holeMaskBindless = bindlessIdx;
}

void TerrainRenderer::generateClipmap(rhi::Device& device, rhi::CommandList& cmd) {
	auto& dxDevice = static_cast<rhi::DX12Device&>(device);
	auto& vb = static_cast<rhi::DX12Buffer&>(m_clipmap.vertexBuffer());

	cmd.transition(vb, rhi::ResourceState::UnorderedAccess);
	cmd.setPipeline(m_clipmap.generatePSO());
	cmd.setComputeRootCBV(0, m_clipmap.clipmapCB());
	cmd.setComputeRootSrvTable(2, 0);

	D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {vb.uavCpu};
	cmd.setComputeRootUavTable(3, dxDevice.writeUavTable(uavs, 1));

	uint32_t groups = (kClipmapTotalVerts + 255) / 256;
	cmd.dispatch(groups, 1, 1);

	cmd.uavBarrier(nullptr);
}

void TerrainRenderer::render(rhi::CommandList& cmd, const glm::mat4& viewProj,
                             rhi::Texture& albedo, rhi::Texture& normal, rhi::Texture& orm,
                             rhi::Texture& emissive, rhi::Texture& depthColor, rhi::Texture& depth) {
	cmd.setDescriptorHeap();
	cmd.setRootSignature(*m_rootSig);
	cmd.setPipeline(*m_terrainPSO);

	{
		struct XForm { glm::mat4 viewProj; glm::mat4 world; };
		XForm xf{viewProj, glm::mat4(1.0f)};
		cmd.setGraphicsRootConstants(0, &xf, 32);
	}

	cmd.setGraphicsRootCBV(2, *m_materialCB);
	cmd.setGraphicsRootSrvTable(3, 0);
	cmd.setGraphicsRootSamplerTable(4, 0);

	cmd.setVertexBuffer(m_clipmap.vertexBuffer(), sizeof(TerrainVertex));
	cmd.setIndexBuffer(m_clipmap.indexBuffer(), true);
	cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);

	uint32_t vertStride = kClipmapRingVerts;
	uint32_t visMask = m_clipmap.visibleMask();
	for (uint32_t r = 0; r < kClipmapRingCount; ++r) {
		if (!(visMask & (1u << r))) continue;
		cmd.drawIndexed(kClipmapIndicesPerRing, 0, int32_t(r * vertStride));
	}
}

} // namespace tucano::terrain
