#pragma once

// WM-8: terrain streaming.
//
// The insight that makes this small instead of a fourth streaming system: terrain is just another
// streamed content type. Each world cell owns a heightmap tile covering its XZ footprint; the tile
// is generated on a worker thread, its GPU heightmap + mesh built on the device thread, and it is
// added to and removed from the scene exactly like a cell of props. That means terrain inherits the
// whole World Machine for free — the frame budget (WM-1), the three-stage pipeline and LRU unload
// (WM-2), persistence (WM-2.5), prediction (WM-3), GPU cull (WM-4), layers (WM-7) and the replay
// recorder (WM-10) all apply to terrain with no new code. A separate terrain streamer would have
// re-implemented every one of them worse.
//
// This provider bridges the pure World module (CellDataProvider) and the terrain module the editor
// uses (Heightmap): it USES tucano::terrain::Heightmap as the tile data structure — CPU + GPU
// height field, bilinear sampleHeight, normals, bounds — and tessellates a lean per-tile mesh from
// it. Tiles seam because every tile samples the SAME world-continuous fbm at absolute coordinates:
// a shared edge is one coordinate, so it is one height on both sides.
//
// Threading contract (from CellDataProvider):
//   readBytes    — worker. Emits the tile's parameters. No disk for procedural terrain.
//   deserialize  — worker. Runs the fbm into a float field. CPU-only, thread-safe.
//   upload       — device thread. Builds the GPU heightmap + mesh, inserts the scene object.
//   release      — device thread. Removes the object and any physics body.

#include "RHI/RHI.h"
#include "Renderer/Scene.h"
#include "Terrain/Heightmap.h"
#include "World/StreamingTypes.h"
#include "World/WorldGrid.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {
namespace physics {
class PhysicsWorld;
}
} // namespace tucano

namespace tucano::terrain {

struct TerrainStreamSettings {
  /// Heightmap texels per tile side. One more than a power of two keeps texel centres aligned to
  /// tile edges so neighbours share their border row exactly.
  uint32_t heightmapResolution = 129;
  /// Mesh grid quads per tile side. Vertices = (meshResolution+1)². Kept modest because dozens of
  /// tiles are resident at once; the clipmap is what would drive close-up density in a full build.
  uint32_t meshResolution = 48;
  float heightScale = 90.0f;   ///< metres from trough to crest
  float baseFrequency = 0.0018f; ///< cycles per metre; low = broad hills
  uint32_t octaves = 5;
  float lacunarity = 2.0f;
  float persistence = 0.5f;
  uint32_t seed = 1337;
  /// Layer terrain streams on. One tile per cell, on one layer, so terrain does not multiply into
  /// four payloads per cell. Visual is the natural home; Gameplay would tie it to the collider band.
  world::WorldLayer layer = world::WorldLayer::Visual;
  bool buildPhysics = false; ///< when set (and a physics world is given), each tile gets a Jolt heightfield
};

class TerrainCellProvider : public world::CellDataProvider {
public:
  TerrainCellProvider(rhi::Device& device, Scene& scene, const world::WorldGrid& grid,
                      const TerrainStreamSettings& settings = {});
  ~TerrainCellProvider() override;

  bool readBytes(const world::CellId& id, world::WorldLayer layer, std::vector<uint8_t>& out) override;
  bool deserialize(const world::CellId& id, world::WorldLayer layer, const std::vector<uint8_t>& bytes,
                   world::CellContent& out) override;
  bool upload(const world::CellId& id, world::WorldLayer layer, world::CellContent& content) override;
  void release(const world::CellId& id, world::WorldLayer layer, world::CellContent& content) override;

  void setPhysics(physics::PhysicsWorld* world) { m_physics = world; }

  /// CPU height query against the currently resident tiles: the same value physics and a player
  /// controller would use. Returns false when no resident tile covers (worldX, worldZ) — which is
  /// the honest answer for terrain that has not streamed in yet. The GPU-side query is the job of
  /// HeightQuery.hlsl; this is its CPU reference and the version usable off the render thread.
  bool sampleHeight(float worldX, float worldZ, float& outHeight) const;

  /// The world-continuous height at a point, generated directly from the fbm without needing the
  /// tile resident. This is ground truth: sampleHeight() on a resident tile must match it within
  /// the tile's texel interpolation error, which is what the gate checks.
  float referenceHeight(float worldX, float worldZ) const;

  size_t liveTileCount() const { return m_live.size(); }
  uint64_t residentVertexCount() const { return m_residentVerts; }
  uint32_t tilesBuilt() const { return m_tilesBuilt; }

private:
  /// A resident tile: the object that draws it, the heightmap for CPU queries, its world footprint.
  struct LiveTile {
    uint64_t objectId = 0;
    std::shared_ptr<Heightmap> heightmap;
    std::shared_ptr<Mesh> mesh; ///< owned here so unload retires it safely (per-tile, uncached)
    glm::vec3 worldOrigin{0.0f};
    float worldSize = 0.0f;
    uint32_t physicsBody = 0; ///< Jolt BodyID bits, 0xFFFFFFFF = none
  };

  /// Heap payload passed from deserialize (worker) to upload (device thread).
  struct TileData {
    std::vector<float> heights;
    glm::vec3 worldOrigin{0.0f};
    float worldSize = 0.0f;
  };

  float fbm(float worldX, float worldZ) const;
  world::CellId cellCovering(float worldX, float worldZ) const;

  rhi::Device& m_device;
  Scene& m_scene;
  const world::WorldGrid& m_grid;
  TerrainStreamSettings m_settings;
  physics::PhysicsWorld* m_physics = nullptr;

  std::vector<int> m_perm; ///< permutation table for the fbm, built once

  std::shared_ptr<Material> m_material; ///< one shared terrain material for every tile

  std::unordered_map<uint64_t, LiveTile> m_live; ///< keyed by cell key
  uint64_t m_nextObjectId = 1;
  uint64_t m_residentVerts = 0;
  uint32_t m_tilesBuilt = 0;

  /// Per-tile meshes are uncached (one owner each), so unloading drops the last GPU reference. Same
  /// hazard as WM-5's merged meshes: retire rather than free, so the GPU is never mid-frame on a
  /// buffer that just died.
  std::vector<std::shared_ptr<Mesh>> m_retired;
  static constexpr size_t kRetireDepth = 64;
};

} // namespace tucano::terrain
