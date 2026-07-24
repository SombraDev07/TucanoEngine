#pragma once

#include "Terrain/TileCache.h"
#include "Terrain/Heightmap.h"
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tucano::terrain {

class MaterialAtlas {
public:
	MaterialAtlas(rhi::Device& device);

	void uploadAllTiles(rhi::Device& device, const Heightmap& hm, TileCache& cache);
	void clearAtlas(rhi::Device& device);

	rhi::Texture& texture() { return *m_texture; }
	uint32_t bindlessIndex() const;

	static float tileWorldSize() { return 64.0f; }
	static TileCoord worldToTile(float wx, float wz);
	static TileCoord worldToAtlasTile(float wx, float wz);

	static constexpr uint32_t atlasSize() { return kAtlasSize; }
	static constexpr uint32_t tileTexels() { return kTileSizeTexels; }

private:
	void generateTilePixels(const TileCoord& worldTile, const Heightmap& hm, std::vector<uint8_t>& rgba8);

	std::shared_ptr<rhi::Texture> m_texture;
};

inline TileCoord MaterialAtlas::worldToTile(float wx, float wz) {
	float ts = tileWorldSize();
	return {
		static_cast<uint32_t>(std::max(0.0f, wx / ts)),
		static_cast<uint32_t>(std::max(0.0f, wz / ts))
	};
}

inline TileCoord MaterialAtlas::worldToAtlasTile(float wx, float wz) {
	return worldToTile(wx, wz);
}

} // namespace tucano::terrain
