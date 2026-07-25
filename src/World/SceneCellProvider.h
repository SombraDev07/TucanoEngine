#pragma once

// The real CellDataProvider: reads `.tcell` files off disk and turns them into live RenderObjects.
//
// This is the only place the World module and the renderer meet, and it is where the threading
// contract from CellDataProvider actually earns its keep:
//
//   readBytes    — background worker. Touches only the filesystem. Thread-safe.
//   deserialize  — background worker. Pure CPU parsing into CellObject records. Thread-safe.
//   upload       — CALLING thread (the one that owns the device). Creates meshes and inserts into
//                  the Scene. NOT thread-safe, and must not be, because both the RHI and the Scene
//                  vector are single-threaded.
//   release      — calling thread. Removes those objects again.
//
// Meshes are shared through a cache keyed by primitive kind: a world of ten thousand cubes creates
// one cube mesh, not ten thousand. Without that the GPU cost of streaming would be dominated by
// duplicate vertex buffers rather than by the streaming itself.

#include "RHI/RHI.h"
#include "Renderer/Scene.h"
#include "World/CellFile.h"
#include "World/CellPersistence.h" // CellDelta: forward-declared in StreamingTypes, needed whole here
#include "World/StreamingTypes.h"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {
class Mesh;
struct Material;
namespace physics {
class PhysicsWorld;
}
} // namespace tucano

namespace tucano::world {

/// CPU-side geometry, kept because merging needs vertices the GPU copy no longer exposes.
struct CpuMesh {
  std::vector<Vertex> verts;
  std::vector<uint32_t> idx;
};

/// Live objects created for one (cell, layer), so release() can find and remove exactly those.
struct LoadedCellContent {
  CellId id;
  uint32_t layer = 0;
  /// Stable handles into the scene. Object indices shift when other cells unload, so identity is
  /// tracked by a monotonic id stamped into the object's name rather than by position in the array.
  std::vector<uint64_t> objectIds;

  /// Index of each object within its `.tcell` file. Parallel to objectIds.
  ///
  /// This is what a persistence delta is keyed on, and the choice matters: the runtime id changes
  /// every time a cell reloads, so a delta keyed on it would apply to the wrong object (or none)
  /// on the next visit. The position in the authored file is the one identity that survives.
  std::vector<uint32_t> cellIndices;

  /// The authored transform of each object, kept as the baseline to diff against on capture. A
  /// delta must record what CHANGED, not the whole cell — otherwise every visited cell would cost
  /// persistence memory whether the player touched it or not.
  std::vector<Transform> authored;

  /// Physics bodies this cell created, so unloading can remove exactly those. Jolt has a fixed
  /// body budget; leaking these would make physics quietly stop working after enough streaming.
  std::vector<uint32_t> bodyIds;

  /// The merged HLOD mesh, when this cell was loaded at a coarse detail level. Held here so unload
  /// can hand it to the provider's retirement queue instead of destroying it on the spot.
  std::shared_ptr<Mesh> mergedMesh;

  /// Local matrix of each object within its source asset. Identity for primitives; for a glTF part
  /// it is that part's place inside the model. The world matrix is `transform.matrix() * partLocal`,
  /// so a glTF with several nodes keeps its shape when the whole instance is moved.
  std::vector<glm::mat4> partLocal;
};

class SceneCellProvider : public CellDataProvider {
public:
  SceneCellProvider(rhi::Device& device, Scene& scene, std::string worldRoot);
  ~SceneCellProvider() override;

  bool readBytes(const CellId& id, WorldLayer layer, std::vector<uint8_t>& out) override;
  bool deserialize(const CellId& id, WorldLayer layer, const std::vector<uint8_t>& bytes,
                   CellContent& out) override;
  bool upload(const CellId& id, WorldLayer layer, CellContent& content) override;
  void release(const CellId& id, WorldLayer layer, CellContent& content) override;

  // ── Persistence (WM-2.5) ──
  bool captureDelta(const CellId& id, WorldLayer layer, const CellContent& content,
                    CellDelta& out) override;
  void applyDelta(const CellId& id, WorldLayer layer, CellContent& content,
                  const CellDelta& delta) override;

  /// Moves a live streamed object, as gameplay would. Returns false if that cell/index is not
  /// currently resident. Exists so the persistence round trip can be exercised without a player.
  bool moveObject(const CellId& id, WorldLayer layer, uint32_t cellIndex, const glm::vec3& delta);

  /// Current world position of a streamed object, or false if it is not resident.
  bool objectPosition(const CellId& id, WorldLayer layer, uint32_t cellIndex, glm::vec3& out) const;

  /// Gives streamed cells a physics world. When set, objects whose cell file declares a collider
  /// get a static body on load and lose it on unload. Null (the default) streams visuals only, at
  /// zero physics cost — which is the right thing for a render-only client or a preview tool.
  void setPhysics(physics::PhysicsWorld* world) { m_physics = world; }
  physics::PhysicsWorld* physics() const { return m_physics; }

  /// Bodies this provider currently owns. The number that must return to zero when the world
  /// unloads — a climbing figure is the signature of a physics leak.
  size_t liveBodyCount() const { return m_liveBodies; }

  /// How many scene objects this provider currently owns. The headline number for a streaming
  /// test: it should rise as you approach content and fall as you leave it.
  size_t liveObjectCount() const;
  /// Files that were missing (a cell with no content for that layer) — not an error, but a large
  /// number means the world was baked differently than the streamer expects.
  uint32_t missingFiles() const { return m_missingFiles; }
  /// Merged HLOD meshes built so far. Each one collapsed a whole layer of a cell into a single
  /// object, which is the entire point of WM-5.
  uint32_t hlodMeshesBuilt() const { return m_hlodBuilds; }

  /// Distinct glTF assets parsed from disk. Should stay tiny however many instances stream in —
  /// a number that tracks the instance count means the cache is not working.
  uint32_t gltfAssetsLoaded() const { return m_gltfLoads; }

private:
  /// The live content for a (cell, layer), or null when it is not resident. Persistence and the
  /// test helpers all need this lookup, so the provider keeps its own index of what it owns.
  LoadedCellContent* findLive(const CellId& id, WorldLayer layer);
  const LoadedCellContent* findLive(const CellId& id, WorldLayer layer) const;
  RenderObject* findObject(uint64_t runtimeId);
  const RenderObject* findObject(uint64_t runtimeId) const;

  std::shared_ptr<Mesh> meshFor(CellObjectKind kind);
  std::shared_ptr<Material> materialFor(const CellObject& o);

  const CpuMesh& cpuPrimitive(CellObjectKind kind);

  /// Builds one merged, simplified mesh from a group of placed primitives. This is HLOD: a distant
  /// cell becomes a single object instead of dozens, which is what actually relieves the per-frame
  /// descriptor ceiling — that ceiling is driven by object count, not by triangles.
  std::shared_ptr<Mesh> buildMerged(const std::vector<const CellObject*>& objects, uint32_t lod);

  /// One renderable piece of a loaded glTF model.
  struct GltfPart {
    std::shared_ptr<Mesh> mesh;
    std::vector<std::shared_ptr<Material>> materials;
    glm::mat4 local{1.0f};
  };

  /// Loads (or returns the cached) parts of a glTF asset. Null when the file could not be read.
  ///
  /// Caching by path is what makes glTF viable at all here: five hundred instances of the same rock
  /// share one set of meshes and materials. Without it each instance would re-parse the file and
  /// re-upload its buffers, and the GPU cost of streaming would be dominated by duplicates.
  const std::vector<GltfPart>* gltfFor(const std::string& path);

  rhi::Device& m_device;
  Scene& m_scene;
  std::string m_worldRoot;

  /// One mesh per primitive kind, shared by every object that uses it.
  std::unordered_map<uint32_t, std::shared_ptr<Mesh>> m_meshCache;

  /// Materials are shared too, keyed by their quantized appearance. Without this every streamed
  /// object allocates its own Material and its own descriptor, and a few hundred resident cells
  /// exhaust the SRV heap outright — which is exactly what happened the first time this ran.
  /// Sharing collapses tens of thousands of materials into the handful of distinct looks that
  /// actually exist.
  std::unordered_map<uint64_t, std::shared_ptr<Material>> m_materialCache;

  /// Loaded glTF assets, keyed by the path written in the cell file. An entry with an empty part
  /// list is a remembered failure, so a missing asset is not re-parsed once per instance.
  std::unordered_map<std::string, std::vector<GltfPart>> m_gltfCache;
  uint32_t m_gltfLoads = 0;

  std::unordered_map<uint32_t, CpuMesh> m_cpuPrimitives;
  uint32_t m_hlodBuilds = 0;

  /// Merged HLOD meshes kept alive for a while after their cell unloaded.
  ///
  /// Primitive and glTF meshes are shared through caches, so unloading a cell never frees GPU
  /// buffers. A merged mesh is different: it belongs to exactly one cell, so dropping the cell drops
  /// the last reference and destroys its vertex/index buffers immediately — while the GPU may still
  /// be executing a frame that references them. That is a use-after-free on the GPU, and it is why
  /// unloading HLOD content crashed. Holding the mesh for a few hundred subsequent releases is far
  /// more than the two or three frames the GPU can be behind.
  std::deque<std::shared_ptr<Mesh>> m_retiredMeshes;
  static constexpr size_t kRetireDepth = 256;

  /// Monotonic id stamped into each created object's name, so release() can find its own objects
  /// again even after unrelated cells shifted the array.
  uint64_t m_nextObjectId = 1;
  uint32_t m_missingFiles = 0;

  physics::PhysicsWorld* m_physics = nullptr;
  size_t m_liveBodies = 0;

  /// Everything currently resident, keyed by (cell, layer). Lets persistence and gameplay find a
  /// streamed object without walking the whole scene.
  std::unordered_map<uint64_t, LoadedCellContent*> m_live;
};

} // namespace tucano::world
