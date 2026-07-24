#include "Terrain/TileCache.h"

#include <algorithm>

namespace tucano::terrain {

TileCache::TileCache() {
	for (uint32_t i = 0; i < kMaxTiles; ++i) {
		m_freeSlots.push_back(i);
	}
	std::reverse(m_freeSlots.begin(), m_freeSlots.end());
}

uint32_t TileCache::allocate(TileCoord coord) {
	auto it = m_table.find(coord);
	if (it != m_table.end()) {
		it->second.lastAccess = ++m_frameCounter;
		return it->second.physicalIdx;
	}

	if (m_freeSlots.empty()) {
		evictLRU(kMaxResident - 1);
	}

	uint32_t idx = m_freeSlots.back();
	m_freeSlots.pop_back();

	TileEntry entry;
	entry.physicalIdx = idx;
	entry.lastAccess = ++m_frameCounter;
	m_table[coord] = entry;
	m_residentList.push_back(coord);

	return idx;
}

void TileCache::touch(TileCoord coord) {
	auto it = m_table.find(coord);
	if (it != m_table.end()) {
		it->second.lastAccess = ++m_frameCounter;
	}
}

void TileCache::evictLRU(uint32_t maxToKeep) {
	while (m_residentList.size() > maxToKeep) {
		TileCoord oldest{};
		uint64_t oldestTime = UINT64_MAX;
		size_t oldestIdx = 0;

		for (size_t i = 0; i < m_residentList.size(); ++i) {
			auto it = m_table.find(m_residentList[i]);
			if (it != m_table.end() && it->second.lastAccess < oldestTime) {
				oldestTime = it->second.lastAccess;
				oldest = m_residentList[i];
				oldestIdx = i;
			}
		}

		if (oldestTime == UINT64_MAX) break;

		auto it = m_table.find(oldest);
		if (it != m_table.end()) {
			m_freeSlots.push_back(it->second.physicalIdx);
			m_table.erase(it);
		}
		m_residentList.erase(m_residentList.begin() + oldestIdx);
	}
}

bool TileCache::isResident(TileCoord coord) const {
	return m_table.find(coord) != m_table.end();
}

uint32_t TileCache::physicalIndex(TileCoord coord) const {
	auto it = m_table.find(coord);
	return (it != m_table.end()) ? it->second.physicalIdx : ~0u;
}

} // namespace tucano::terrain
