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

#include <array>
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
  /// Mesh grid quads per tile side AT LOD 0 (closest). Vertices = (meshResolution+1)². Each coarser
  /// LOD halves this, so a distant tile is a fraction of the geometry — that is the whole point of
  /// the per-tile LOD. Kept modest because dozens of tiles are resident at once.
  uint32_t meshResolution = 48;
  /// Depth of the vertical skirt dropped around every tile, in metres. Hides the T-junction cracks
  /// where a dense (near) tile meets a coarse (far) one — a curtain of geometry behind the seam
  /// rather than a gap through to the sky. 0 disables skirts.
  float skirtDepth = 12.0f;
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

  // ── Composite window for the clipmap (WM-8 Phase 2) ──
  //
  // Instead of one mesh per tile, the streamed tiles are blitted into a single heightmap texture — a
  // window that recentres on the observer — which the continuous-LOD clipmap samples. This is the
  // AAA shape: streaming decides what heightmap data is resident, the clipmap turns it into geometry.
  // Enabling this makes upload() skip the per-tile mesh entirely; the tile's heights feed the window.

  /// Turns on composite mode with a window of `worldSize` metres at `resolution` texels per side.
  void enableComposite(rhi::Device& device, float worldSize, uint32_t resolution);
  bool compositeEnabled() const { return m_compositeMode; }

  /// Recentres the window on the observer if it has drifted too far, re-blits resident tiles, and
  /// re-uploads the texture when dirty. MUST be called OUTSIDE a beginFrame()/endFrame() pair — it
  /// uploads a texture, which resets the shared upload arena.
  void updateComposite(rhi::Device& device, const glm::vec2& observerXZ);

  rhi::Texture* compositeTexture() const { return m_compositeTex.get(); }
  uint32_t compositeBindlessIndex() const;
  glm::vec2 compositeMin() const { return m_compositeMin; }
  float compositeWorldSize() const { return m_compositeWorldSize; }

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

  /// How many resident tiles sit at each LOD (index 0 = full detail). A spread across several
  /// buckets is the signature that distance LOD is actually working — everything at LOD 0 would
  /// mean the band logic never fired. Buckets beyond 7 are folded into 7.
  std::array<uint32_t, 8> tilesPerLod() const;

  /// LOD and vertex count of the tile covering (worldX, worldZ), or false if none is resident. For
  /// the gate to compare a near tile against a far one.
  bool tileLodAt(float worldX, float worldZ, uint32_t& outLod, uint32_t& outVerts) const;

  /// The resident tile's heightmap covering (worldX, worldZ) plus its world origin, or null. Lets the
  /// GPU height query (HeightmapQuery) dispatch against a streamed tile's texture: submit LOCAL
  /// coordinates (world minus origin) since the tile heightmap spans [0, cellSize] in its own space.
  std::shared_ptr<Heightmap> tileHeightmapAt(float worldX, float worldZ, glm::vec3& outOrigin) const;

private:
  /// A resident tile: the object that draws it, the heightmap for CPU queries, its world footprint.
  struct LiveTile {
    uint64_t objectId = 0;
    std::shared_ptr<Heightmap> heightmap;
    std::shared_ptr<Mesh> mesh; ///< owned here so unload retires it safely (per-tile, uncached)
    glm::vec3 worldOrigin{0.0f};
    float worldSize = 0.0f;
    uint32_t physicsBody = 0; ///< Jolt BodyID bits, 0xFFFFFFFF = none
    uint32_t lod = 0;         ///< detail level this tile was built at (0 = full)
    uint32_t vertexCount = 0; ///< verts in this tile's mesh, for the LOD gate
    /// CPU heights kept in composite mode so the window can be rebuilt when it recentres. Empty in
    /// the per-tile-mesh mode, where the mesh owns the geometry instead.
    std::vector<float> cpuHeights;
    uint32_t heightRes = 0;
  };

  /// Heap payload passed from deserialize (worker) to upload (device thread).
  struct TileData {
    std::vector<float> heights;
    glm::vec3 worldOrigin{0.0f};
    float worldSize = 0.0f;
  };

  float fbm(float worldX, float worldZ) const;
  world::CellId cellCovering(float worldX, float worldZ) const;

  /// Blits one tile's heights into the composite window at the current window position.
  void blitTileToComposite(const glm::vec3& origin, float tileSize, const std::vector<float>& heights,
                           uint32_t heightRes);

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

  // Composite window state (Phase 2).
  bool m_compositeMode = false;
  std::shared_ptr<rhi::Texture> m_compositeTex; ///< R32_FLOAT window sampled by the clipmap
  std::vector<float> m_compositeData;           ///< CPU mirror, re-uploaded when dirty
  glm::vec2 m_compositeMin{0.0f};               ///< world min corner of the window
  float m_compositeWorldSize = 0.0f;
  uint32_t m_compositeRes = 0;
  float m_compositeTexel = 0.0f;                ///< worldSize / resolution
  bool m_compositeDirty = false;
};

} // namespace tucano::terrain
