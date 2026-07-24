#pragma once

#include "Terrain/Clipmap.h"
#include "Terrain/Heightmap.h"
#include "Terrain/MaterialAtlas.h"
#include "Terrain/MaterialLayers.h"
#include "Terrain/TileCache.h"
#include "Renderer/Camera.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <memory>

namespace tucano::terrain {

class TerrainRenderer {
public:
	TerrainRenderer(rhi::Device& device, std::shared_ptr<Heightmap> heightmap);

	void prepareFrame(rhi::Device& device, const Camera& camera);
	void generateClipmap(rhi::Device& device, rhi::CommandList& cmd);
	void render(rhi::CommandList& cmd, const glm::mat4& viewProj,
	            rhi::Texture& albedo, rhi::Texture& normal, rhi::Texture& orm,
	            rhi::Texture& emissive, rhi::Texture& depthColor, rhi::Texture& depth);

	void setHoleMask(uint32_t bindlessIdx);
	void enableAtlas(bool enable) { m_useAtlas = enable; }
	MaterialLayers& materialLayers() { return m_materialLayers; }

	std::shared_ptr<Heightmap> heightmap() { return m_heightmap; }

private:
	void createPipelines(rhi::Device& device);
	void uploadTerrainMaterialCB(rhi::Device& device);

	rhi::Device& m_device;
	std::shared_ptr<Heightmap> m_heightmap;
	Clipmap m_clipmap;
	MaterialAtlas m_atlas;
	TileCache m_tileCache;
	MaterialLayers m_materialLayers;

	std::shared_ptr<rhi::RootSignature> m_rootSig;
	std::shared_ptr<rhi::PipelineState> m_terrainPSO;
	std::shared_ptr<rhi::Buffer> m_materialCB;

	uint32_t m_holeMaskBindless = ~0u;
	bool m_useAtlas = true;
};

} // namespace tucano::terrain
