#pragma once

#include <cstdint>

namespace tucano::terrain {

inline constexpr uint32_t kClipmapRingCount = 8;
inline constexpr uint32_t kClipmapRingSize = 128;
inline constexpr uint32_t kClipmapRingVerts = (kClipmapRingSize + 1) * (kClipmapRingSize + 1);
inline constexpr uint32_t kClipmapTotalVerts = kClipmapRingCount * kClipmapRingVerts;
inline constexpr uint32_t kClipmapQuadsPerRing = kClipmapRingSize * kClipmapRingSize;
inline constexpr uint32_t kClipmapIndicesPerRing = kClipmapQuadsPerRing * 6;
inline constexpr uint32_t kMorphWidth = 2;

struct TerrainVertex {
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvU, uvV;
};

struct ClipmapRingCB {
	float scale;
	float pad0;
	float worldOriginX;
	float worldOriginZ;
};

struct ClipmapCB {
	float invWorldSizeX;
	float invWorldSizeY;
	uint32_t heightmapIndex;
	float heightScale;
	float baseScale;
	float morphWidth;
	uint32_t holeMaskIndex;
	float padCB;
	ClipmapRingCB rings[kClipmapRingCount];
};

} // namespace tucano::terrain
