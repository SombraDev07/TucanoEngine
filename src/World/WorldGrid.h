#pragma once

// The world partition: a sparse 3D grid of cells, sharded so streaming threads and the frame
// thread can touch it at the same time.
//
// Cells exist only where content exists. An empty sky above a landscape costs nothing because no
// cell is ever created there — which is the actual benefit the roadmap wanted from an octree, and
// it comes from sparsity, not from the tree.

#include "World/CellId.h"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::world {

/// Independent slices of a cell's content. Each loads on its own schedule so the player can
/// interact with a place before it finishes looking like one.
enum class WorldLayer : uint8_t {
  Gameplay = 0, ///< ECS entities, colliders, triggers — must exist before the player arrives
  Visual = 1,   ///< meshes, materials, lights
  Audio = 2,    ///< emitters, reverb zones
  Detail = 3,   ///< decals, particles, small vegetation
  Count = 4,
};

inline constexpr uint32_t kLayerCount = uint32_t(WorldLayer::Count);

/// Relative urgency of each layer, used to scale streaming priority.
/// Gameplay outranks everything: a missing collider is a bug, a missing bush is not.
inline constexpr float kLayerWeight[kLayerCount] = {2.0f, 1.0f, 0.5f, 0.3f};

const char* layerName(WorldLayer layer);

/// Lifecycle of one layer inside one cell.
///
/// The three Loading* states are not bookkeeping — they are the three pipeline stages (disk read,
/// deserialize, GPU upload) that WM-2 overlaps across cells. Making them distinct states is what
/// lets a cell in GPU upload coexist with another still reading from disk.
enum class CellState : uint8_t {
  Unloaded = 0,
  Queued,      ///< scheduled, not yet started
  LoadingIO,   ///< stage 1: bytes coming off disk
  LoadingCPU,  ///< stage 2: deserializing into engine objects
  LoadingGPU,  ///< stage 3: uploading buffers and textures
  Loaded,
  Unloading,
  Failed,
};

const char* cellStateName(CellState state);

/// True while the layer is somewhere in the load pipeline.
inline bool isLoading(CellState s) {
  return s == CellState::Queued || s == CellState::LoadingIO || s == CellState::LoadingCPU ||
         s == CellState::LoadingGPU;
}

struct CellLayer {
  std::atomic<CellState> state{CellState::Unloaded};
  uint64_t cpuBytes = 0;
  uint64_t gpuBytes = 0;

  CellLayer() = default;
  CellLayer(const CellLayer& o) : state(o.state.load()), cpuBytes(o.cpuBytes), gpuBytes(o.gpuBytes) {}
};

/// One cell of the world. Owns nothing directly yet — content pointers arrive with WM-2.
struct WorldCell {
  CellId id;
  glm::vec3 boundsMin{0.0f};
  glm::vec3 boundsMax{0.0f};

  std::array<CellLayer, kLayerCount> layers;

  /// Frame this cell was last inside an observer's radius. Drives LRU eviction.
  std::atomic<uint64_t> lastTouchFrame{0};
  /// Streaming priority computed for the current frame. Lower is more urgent.
  std::atomic<float> priority{1e30f};

  /// Where the cell's data lives, relative to the world root.
  std::string path;

  WorldCell() = default;
  WorldCell(const WorldCell&) = delete;
  WorldCell& operator=(const WorldCell&) = delete;

  glm::vec3 center() const { return (boundsMin + boundsMax) * 0.5f; }

  /// Distance from `p` to the cell's box; zero when inside. Streaming distances must use the box,
  /// not the centre — a large cell whose edge is at the player's feet is not "far away".
  float distanceTo(const glm::vec3& p) const {
    const glm::vec3 d = glm::max(glm::max(boundsMin - p, glm::vec3(0.0f)), p - boundsMax);
    return glm::length(d);
  }

  /// True when every layer has finished loading.
  bool fullyLoaded() const;
  /// True when any layer is mid-pipeline.
  bool anyLoading() const;

  uint64_t totalCpuBytes() const;
  uint64_t totalGpuBytes() const;
};

/// Geometry of the partition: where the world starts and how big cells are at each level.
struct WorldGridDesc {
  glm::vec3 origin{0.0f};      ///< world-space position of cell (0,0,0) at every level
  float rootCellSize = 65536.f; ///< edge length at level 0, in metres
  uint32_t streamLevel = 10;    ///< level the streamer operates on (65536 / 2^10 = 64 m cells)
};

/// Sparse store of cells, keyed by CellId.
///
/// Sharded: the morton key's low bits pick one of kShards independent maps, each with its own
/// mutex. Because morton keys of neighbouring cells differ in their low bits, spatially adjacent
/// cells land on different shards — so a streaming thread working one region and the frame thread
/// walking another almost never contend.
class WorldGrid {
public:
  static constexpr uint32_t kShards = 16;

  explicit WorldGrid(const WorldGridDesc& desc = {});

  const WorldGridDesc& desc() const { return m_desc; }

  /// Edge length of a cell at `level`, in metres.
  float cellSize(uint32_t level) const;

  /// The cell at `level` containing world position `p`.
  CellId cellAt(const glm::vec3& p, uint32_t level) const;

  /// World-space bounds of `id`.
  void boundsOf(const CellId& id, glm::vec3& outMin, glm::vec3& outMax) const;

  /// Creates the cell if absent and returns it. Thread-safe.
  WorldCell& getOrCreate(const CellId& id);

  /// Returns the cell or nullptr. Thread-safe.
  WorldCell* find(const CellId& id);
  const WorldCell* find(const CellId& id) const;

  /// Removes a cell. Returns false if it was not present. Thread-safe.
  bool erase(const CellId& id);

  size_t cellCount() const;

  /// Every cell at `level` whose box is within `radius` of `p`.
  /// Only enumerates the integer range that can possibly intersect, so cost scales with the
  /// radius, not with the size of the world.
  std::vector<CellId> cellsInRadius(const glm::vec3& p, float radius, uint32_t level) const;

  /// Visits every existing cell. `fn` must not add or remove cells.
  void forEach(const std::function<void(WorldCell&)>& fn);
  void forEach(const std::function<void(const WorldCell&)>& fn) const;

  uint64_t totalCpuBytes() const;
  uint64_t totalGpuBytes() const;

private:
  struct Shard {
    mutable std::shared_mutex mutex;
    std::unordered_map<uint64_t, std::unique_ptr<WorldCell>> cells;
  };

  static uint32_t shardOf(const CellId& id) { return uint32_t(id.key()) & (kShards - 1); }

  WorldGridDesc m_desc;
  mutable std::array<Shard, kShards> m_shards;
};

} // namespace tucano::world
