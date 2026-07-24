#include "Terrain/Clipmap.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/DX12Device.h"

#include <cmath>
#include <cstring>

namespace tucano::terrain {

void Clipmap::init(rhi::Device& device) {
	createIndexBuffer(device);
	createVertexBuffer(device);

	rhi::ComputePipelineDesc csDesc{};
	csDesc.rootSignature = device.createComputeRootSignature();
	csDesc.cs = rhi::ShaderBytecode::loadFromFile("shaders/TerrainClipmap_CSMain.cso");
	m_generatePSO = device.createComputePipeline(csDesc);

	rhi::BufferDesc cbDesc{};
	cbDesc.size = sizeof(ClipmapCB);
	cbDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
	cbDesc.debugName = "ClipmapCB";
	m_clipmapCB = device.createBuffer(cbDesc);
}

void Clipmap::createIndexBuffer(rhi::Device& device) {
	std::vector<uint32_t> indices;
	indices.reserve(kClipmapIndicesPerRing);

	uint32_t stride = kClipmapRingSize + 1;
	for (uint32_t z = 0; z < kClipmapRingSize; ++z) {
		for (uint32_t x = 0; x < kClipmapRingSize; ++x) {
			uint32_t i00 = z * stride + x;
			uint32_t i10 = z * stride + x + 1;
			uint32_t i01 = (z + 1) * stride + x;
			uint32_t i11 = (z + 1) * stride + x + 1;
			indices.push_back(i00); indices.push_back(i01); indices.push_back(i10);
			indices.push_back(i10); indices.push_back(i01); indices.push_back(i11);
		}
	}

	rhi::BufferDesc ibDesc{};
	ibDesc.size = indices.size() * sizeof(uint32_t);
	ibDesc.usage = rhi::BufferUsage::Index;
	ibDesc.debugName = "ClipmapIB";
	m_indexBuffer = device.createBuffer(ibDesc, indices.data());
}

void Clipmap::createVertexBuffer(rhi::Device& device) {
	rhi::BufferDesc vbDesc{};
	vbDesc.size = kClipmapTotalVerts * sizeof(TerrainVertex);
	vbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
	vbDesc.stride = sizeof(TerrainVertex);
	vbDesc.debugName = "ClipmapVB";
	m_vertexBuffer = device.createBuffer(vbDesc);
}

static float snapToGrid(float val, float gridSize) {
	return std::floor(val / gridSize) * gridSize;
}

void Clipmap::updateCB(rhi::Device& /*device*/, const Heightmap& hm, const glm::vec3& cameraPos,
                       uint32_t holeMaskIdx) {
	ClipmapCB& cb = m_cachedCB;
	cb.invWorldSizeX = 1.0f / hm.worldSize();
	cb.invWorldSizeY = 1.0f / hm.worldSize();
	cb.heightmapIndex = hm.bindlessIndex();
	cb.heightScale = 1.0f;
	cb.baseScale = hm.worldSize() / float(kClipmapRingSize);
	cb.morphWidth = float(kMorphWidth);
	cb.holeMaskIndex = holeMaskIdx;
	cb.padCB = 0.0f;

	for (uint32_t r = 0; r < kClipmapRingCount; ++r) {
		float scale = cb.baseScale * std::exp2f(float(r));
		float sx = snapToGrid(cameraPos.x, scale);
		float sz = snapToGrid(cameraPos.z, scale);

		cb.rings[r].scale = scale;
		cb.rings[r].pad0 = 0.0f;
		cb.rings[r].worldOriginX = sx - scale * float(kClipmapRingSize) * 0.5f;
		cb.rings[r].worldOriginZ = sz - scale * float(kClipmapRingSize) * 0.5f;
	}

	std::memcpy(m_clipmapCB->mapped(), &cb, sizeof(cb));
}

static bool frustumTestAABB(const glm::mat4& vp, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
	glm::vec4 corners[8] = {
		{minX, minY, minZ, 1}, {maxX, minY, minZ, 1}, {minX, maxY, minZ, 1}, {maxX, maxY, minZ, 1},
		{minX, minY, maxZ, 1}, {maxX, minY, maxZ, 1}, {minX, maxY, maxZ, 1}, {maxX, maxY, maxZ, 1},
	};
	glm::vec4 planeX[6] = {
		vp[3] + vp[0], vp[3] - vp[0], vp[3] + vp[1],
		vp[3] - vp[1], vp[3] + vp[2], vp[3] - vp[2],
	};

	for (int p = 0; p < 6; ++p) {
		bool inside = false;
		for (int c = 0; c < 8; ++c) {
			if (glm::dot(planeX[p], corners[c]) > 0.0f) { inside = true; break; }
		}
		if (!inside) return false;
	}
	return true;
}

void Clipmap::cullRings(const glm::mat4& viewProj) {
	m_visibleMask = 0;
	for (uint32_t r = 0; r < kClipmapRingCount; ++r) {
		float scale = m_cachedCB.rings[r].scale;
		float ox = m_cachedCB.rings[r].worldOriginX;
		float oz = m_cachedCB.rings[r].worldOriginZ;
		float half = scale * float(kClipmapRingSize) * 0.5f;
		float hMin = m_cachedCB.heightScale * -500.0f;
		float hMax = m_cachedCB.heightScale * 500.0f;

		if (frustumTestAABB(viewProj, ox, hMin, oz, ox + half * 2.0f, hMax, oz + half * 2.0f)) {
			m_visibleMask |= (1u << r);
		}
	}
}

} // namespace tucano::terrain
