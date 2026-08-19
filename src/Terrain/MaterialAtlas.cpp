#include "Terrain/MaterialAtlas.h"

#include <cstring>

namespace tucano::terrain {

MaterialAtlas::MaterialAtlas(rhi::Device& device) {
	rhi::TextureDesc desc{};
	desc.width = kAtlasSize;
	desc.height = kAtlasSize;
	desc.format = rhi::Format::R8G8B8A8_UNORM_SRGB;
	desc.usage = rhi::TextureUsage::ShaderResource;
	desc.debugName = "MaterialAtlas";
	m_texture = device.createTexture(desc);
}

uint32_t MaterialAtlas::bindlessIndex() const {
	return m_texture ? m_texture->bindlessIndex() : 0u;
}

void MaterialAtlas::generateTilePixels(const TileCoord& worldTile, const Heightmap& hm, std::vector<uint8_t>& rgba8) {
	rgba8.resize(size_t(kTileSizeTexels) * size_t(kTileSizeTexels) * 4);

	float ts = tileWorldSize();
	float wxBase = float(worldTile.x) * ts;
	float wzBase = float(worldTile.y) * ts;

	for (uint32_t ty = 0; ty < kTileSizeTexels; ++ty) {
		for (uint32_t tx = 0; tx < kTileSizeTexels; ++tx) {
			float wx = wxBase + (float(tx) + 0.5f) * (ts / float(kTileSizeTexels));
			float wz = wzBase + (float(ty) + 0.5f) * (ts / float(kTileSizeTexels));

			float h = hm.sampleHeight(wx, wz);
			glm::vec3 n = hm.sampleNormal(wx, wz);
			float slope = 1.0f - glm::abs(n.y);

			uint8_t r, g, b;
			if (slope < 0.2f) {
				r = 56; g = 107; b = 36;
			} else if (slope < 0.5f) {
				r = 89; g = 64; b = 38;
			} else {
				r = 115; g = 102; b = 89;
			}

			float hRange = glm::max(hm.maxHeight() - hm.minHeight(), 1.0f);
			float h01 = (h - hm.minHeight()) / hRange;
			float shade = 0.7f + 0.3f * h01 + 0.15f * (1.0f - slope);
			r = uint8_t(glm::clamp(float(r) * shade, 0.0f, 255.0f));
			g = uint8_t(glm::clamp(float(g) * shade, 0.0f, 255.0f));
			b = uint8_t(glm::clamp(float(b) * shade, 0.0f, 255.0f));

			size_t idx = (size_t(ty) * kTileSizeTexels + size_t(tx)) * 4;
			rgba8[idx + 0] = r;
			rgba8[idx + 1] = g;
			rgba8[idx + 2] = b;
			rgba8[idx + 3] = 255;
		}
	}
}

void MaterialAtlas::uploadAllTiles(rhi::Device& device, const Heightmap& hm, TileCache& cache) {
	std::vector<uint8_t> fullAtlas(size_t(kAtlasSize) * size_t(kAtlasSize) * 4, 0);

	std::vector<uint8_t> tilePixels;
	for (const auto& coord : cache.residentTiles()) {
		if (!cache.isResident(coord)) continue;

		generateTilePixels(coord, hm, tilePixels);
		uint32_t physIdx = cache.physicalIndex(coord);
		uint32_t px = (physIdx % kTilesPerSideCount) * kTileSizeTexels;
		uint32_t py = (physIdx / kTilesPerSideCount) * kTileSizeTexels;

		for (uint32_t ty = 0; ty < kTileSizeTexels; ++ty) {
			size_t srcOff = size_t(ty) * kTileSizeTexels * 4;
			size_t dstOff = (size_t(py + ty) * kAtlasSize + size_t(px)) * 4;
			std::memcpy(fullAtlas.data() + dstOff, tilePixels.data() + srcOff, kTileSizeTexels * 4);
		}
	}

	device.uploadTexture(*m_texture, fullAtlas.data(), kAtlasSize, kAtlasSize, kAtlasSize * 4);
}

void MaterialAtlas::clearAtlas(rhi::Device& device) {
	std::vector<uint8_t> black(size_t(kAtlasSize) * size_t(kAtlasSize) * 4, 0);
	device.uploadTexture(*m_texture, black.data(), kAtlasSize, kAtlasSize, kAtlasSize * 4);
}

} // namespace tucano::terrain
