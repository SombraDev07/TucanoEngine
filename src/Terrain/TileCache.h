#pragma once

#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tucano::terrain {

inline constexpr uint32_t kAtlasSize = 4096;
inline constexpr uint32_t kTileSizeTexels = 128;
inline constexpr uint32_t kTilesPerSideCount = kAtlasSize / kTileSizeTexels;
inline constexpr uint32_t kMaxTiles = kTilesPerSideCount * kTilesPerSideCount;

struct TileCoord {
	uint32_t x, y;
	bool operator==(const TileCoord& o) const { return x == o.x && y == o.y; }
	bool operator!=(const TileCoord& o) const { return !(*this == o); }
};

struct TileCoordHash {
	size_t operator()(const TileCoord& tc) const {
		return (static_cast<uint64_t>(tc.y) << 32) | tc.x;
	}
};

class TileCache {
public:
	static constexpr uint32_t kMaxResident = 512;

	TileCache();

	uint32_t allocate(TileCoord coord);
	void touch(TileCoord coord);
	void evictLRU(uint32_t maxToKeep = kMaxResident);
	bool isResident(TileCoord coord) const;
	uint32_t physicalIndex(TileCoord coord) const;
	const std::vector<TileCoord>& residentTiles() const { return m_residentList; }
	size_t residentCount() const { return m_residentList.size(); }

private:
	struct TileEntry {
		uint32_t physicalIdx = ~0u;
		uint64_t lastAccess = 0;
	};

	std::unordered_map<TileCoord, TileEntry, TileCoordHash> m_table;
	std::vector<TileCoord> m_residentList;
	std::vector<uint32_t> m_freeSlots;
	uint64_t m_frameCounter = 0;
};

} // namespace tucano::terrain
