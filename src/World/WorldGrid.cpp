#include "World/WorldGrid.h"

#include <algorithm>
#include <cmath>

namespace tucano::world {

const char* layerName(WorldLayer layer) {
  switch (layer) {
    case WorldLayer::Gameplay: return "Gameplay";
    case WorldLayer::Visual: return "Visual";
    case WorldLayer::Audio: return "Audio";
    case WorldLayer::Detail: return "Detail";
    default: return "?";
  }
}

const char* cellStateName(CellState state) {
  switch (state) {
    case CellState::Unloaded: return "Unloaded";
    case CellState::Queued: return "Queued";
    case CellState::LoadingIO: return "LoadingIO";
    case CellState::LoadingCPU: return "LoadingCPU";
    case CellState::LoadingGPU: return "LoadingGPU";
    case CellState::Loaded: return "Loaded";
    case CellState::Unloading: return "Unloading";
    case CellState::Failed: return "Failed";
    default: return "?";
  }
}

bool WorldCell::fullyLoaded() const {
  for (const auto& l : layers) {
    if (l.state.load(std::memory_order_acquire) != CellState::Loaded) return false;
  }
  return true;
}

bool WorldCell::anyLoading() const {
  for (const auto& l : layers) {
    if (isLoading(l.state.load(std::memory_order_acquire))) return true;
  }
  return false;
}

uint64_t WorldCell::totalCpuBytes() const {
  uint64_t sum = 0;
  for (const auto& l : layers) sum += l.cpuBytes;
  return sum;
}

uint64_t WorldCell::totalGpuBytes() const {
  uint64_t sum = 0;
  for (const auto& l : layers) sum += l.gpuBytes;
  return sum;
}

WorldGrid::WorldGrid(const WorldGridDesc& desc) : m_desc(desc) {
  m_desc.streamLevel = std::min(m_desc.streamLevel, kMaxLevel);
}

float WorldGrid::cellSize(uint32_t level) const {
  return m_desc.rootCellSize / float(uint64_t(1) << std::min(level, kMaxLevel));
}

CellId WorldGrid::cellAt(const glm::vec3& p, uint32_t level) const {
  const float s = cellSize(level);
  const glm::vec3 local = (p - m_desc.origin) / s;
  // floor, not truncation: truncation would fold -0.5 and +0.5 into the same cell and put a seam
  // through the world origin.
  return CellId{int32_t(std::floor(local.x)), int32_t(std::floor(local.y)),
                int32_t(std::floor(local.z)), level};
}

void WorldGrid::boundsOf(const CellId& id, glm::vec3& outMin, glm::vec3& outMax) const {
  const float s = cellSize(id.level);
  outMin = m_desc.origin + glm::vec3(float(id.x), float(id.y), float(id.z)) * s;
  outMax = outMin + glm::vec3(s);
}

WorldCell& WorldGrid::getOrCreate(const CellId& id) {
  Shard& shard = m_shards[shardOf(id)];
  const uint64_t k = id.key();

  {
    std::shared_lock<std::shared_mutex> read(shard.mutex);
    auto it = shard.cells.find(k);
    if (it != shard.cells.end()) return *it->second;
  }

  std::unique_lock<std::shared_mutex> write(shard.mutex);
  // Re-check: another thread may have inserted between dropping the read lock and taking the
  // write lock.
  auto it = shard.cells.find(k);
  if (it != shard.cells.end()) return *it->second;

  auto cell = std::make_unique<WorldCell>();
  cell->id = id;
  boundsOf(id, cell->boundsMin, cell->boundsMax);
  WorldCell& ref = *cell;
  shard.cells.emplace(k, std::move(cell));
  return ref;
}

WorldCell* WorldGrid::find(const CellId& id) {
  Shard& shard = m_shards[shardOf(id)];
  std::shared_lock<std::shared_mutex> read(shard.mutex);
  auto it = shard.cells.find(id.key());
  return it == shard.cells.end() ? nullptr : it->second.get();
}

const WorldCell* WorldGrid::find(const CellId& id) const {
  const Shard& shard = m_shards[shardOf(id)];
  std::shared_lock<std::shared_mutex> read(shard.mutex);
  auto it = shard.cells.find(id.key());
  return it == shard.cells.end() ? nullptr : it->second.get();
}

bool WorldGrid::erase(const CellId& id) {
  Shard& shard = m_shards[shardOf(id)];
  std::unique_lock<std::shared_mutex> write(shard.mutex);
  return shard.cells.erase(id.key()) > 0;
}

size_t WorldGrid::cellCount() const {
  size_t n = 0;
  for (const auto& shard : m_shards) {
    std::shared_lock<std::shared_mutex> read(shard.mutex);
    n += shard.cells.size();
  }
  return n;
}

std::vector<CellId> WorldGrid::cellsInRadius(const glm::vec3& p, float radius,
                                             uint32_t level) const {
  std::vector<CellId> out;
  if (radius <= 0.0f) return out;

  const float s = cellSize(level);
  const CellId lo = cellAt(p - glm::vec3(radius), level);
  const CellId hi = cellAt(p + glm::vec3(radius), level);

  // Guard against a radius so large relative to the cell size that the loop would run for ever.
  //
  // The check has to be done per axis before multiplying. A radius of 1e9 over 4 m cells gives
  // 5e8 cells on each axis, and 5e8 cubed overflows int64 — the product comes out garbage, sails
  // past the limit and reserve() then tries to allocate it.
  constexpr int64_t kMaxSpan = 1 << 20;
  const int64_t nx = int64_t(hi.x) - int64_t(lo.x) + 1;
  const int64_t ny = int64_t(hi.y) - int64_t(lo.y) + 1;
  const int64_t nz = int64_t(hi.z) - int64_t(lo.z) + 1;
  if (nx <= 0 || ny <= 0 || nz <= 0) return out;
  if (nx > kMaxSpan || ny > kMaxSpan || nz > kMaxSpan) return out;
  // Each axis is now at most 2^20, so nx*ny is at most 2^40 and cannot overflow.
  if (nx * ny > kMaxSpan) return out;
  const int64_t span = nx * ny * nz;
  if (span > kMaxSpan) return out;

  out.reserve(size_t(span));
  const float r2 = radius * radius;
  for (int32_t z = lo.z; z <= hi.z; ++z) {
    for (int32_t y = lo.y; y <= hi.y; ++y) {
      for (int32_t x = lo.x; x <= hi.x; ++x) {
        // Box-to-point, so a cell counts as in range when any part of it is — matching how
        // WorldCell::distanceTo measures.
        const glm::vec3 bmin = m_desc.origin + glm::vec3(float(x), float(y), float(z)) * s;
        const glm::vec3 bmax = bmin + glm::vec3(s);
        const glm::vec3 d = glm::max(glm::max(bmin - p, glm::vec3(0.0f)), p - bmax);
        if (glm::dot(d, d) <= r2) out.push_back(CellId{x, y, z, level});
      }
    }
  }
  return out;
}

void WorldGrid::forEach(const std::function<void(WorldCell&)>& fn) {
  for (auto& shard : m_shards) {
    std::shared_lock<std::shared_mutex> read(shard.mutex);
    for (auto& [k, cell] : shard.cells) fn(*cell);
  }
}

void WorldGrid::forEach(const std::function<void(const WorldCell&)>& fn) const {
  for (const auto& shard : m_shards) {
    std::shared_lock<std::shared_mutex> read(shard.mutex);
    for (const auto& [k, cell] : shard.cells) fn(*cell);
  }
}

uint64_t WorldGrid::totalCpuBytes() const {
  uint64_t sum = 0;
  forEach([&sum](const WorldCell& c) { sum += c.totalCpuBytes(); });
  return sum;
}

uint64_t WorldGrid::totalGpuBytes() const {
  uint64_t sum = 0;
  forEach([&sum](const WorldCell& c) { sum += c.totalGpuBytes(); });
  return sum;
}

} // namespace tucano::world
