#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"
#include "Renderer/Camera.h"
#include "Renderer/LightType.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Renderer/RenderObjectHandle.h"

#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

namespace terrain {
class ClipmapTerrain;
}

struct TUCANO_TYPE() Transform {
  TUCANO_FIELD(.label = "Position", .tooltip = "Object origin in world space", .step = 0.1f)
  glm::vec3 translation{0};

  TUCANO_FIELD(.label = "Rotation",
               .tooltip = "Shown and edited as Euler degrees; stored as a quaternion", .step = 0.5f)
  glm::quat rotation{1, 0, 0, 0};

  TUCANO_FIELD(.label = "Scale", .tooltip = "Per-axis scale; 1 is unscaled", .step = 0.01f)
  glm::vec3 scale{1};

  glm::mat4 matrix() const;
};

struct TUCANO_TYPE() RenderObject {
  // Not annotated, deliberately: mesh and materials are shared_ptr resources that need the Pickers
  // of P4-04 wired to a resource system before a row for them would mean anything, and worldMatrix
  // is derived every frame — editing it does nothing.
  std::shared_ptr<Mesh> mesh;
  std::vector<std::shared_ptr<Material>> materials;

  TUCANO_FIELD(.label = "Transform", .category = "Transform")
  Transform transform;

  glm::mat4 worldMatrix{1.0f};

  TUCANO_FIELD(.label = "Name", .category = "Object")
  std::string name;
  // Editor visibility. Hidden objects are skipped by every pass — including shadows, ray tracing
  // and GI — so hiding one removes it from the image completely, not just from the g-buffer.
  TUCANO_FIELD(.label = "Visible",
               .tooltip = "Hidden objects are skipped by every pass, including shadows, ray tracing and GI",
               .category = "Object")
  bool visible = true;

  // Skinning palette: one matrix per bone (world × inverseBindPose), produced by whatever drives
  // the animation. Non-empty means the renderer uploads it and the vertex shader deforms this
  // object. Keeping it here rather than owning an animation player lets the renderer stay unaware
  // of clips, blending and playback.
  std::vector<glm::mat4> skinningMatrices;
};

/// A GPU-driven instance cloud to draw this frame (WM-6). The caller runs the compute cull before
/// Renderer::render — writing `visibleBuffer` (compacted indices) and `argsBuffer` (the
/// DrawIndexedInstanced block) — and the renderer issues one drawIndexedIndirect for the whole set.
/// The renderer only reads these as opaque RHI handles, so the World module owns the culler and the
/// renderer stays free of a dependency on it.
struct InstanceCloudRender {
  rhi::Buffer* instanceBuffer = nullptr;
  rhi::Buffer* visibleBuffer = nullptr;
  rhi::Buffer* argsBuffer = nullptr;
  Mesh* mesh = nullptr;
  glm::vec4 baseColor{0.5f, 0.5f, 0.5f, 1.0f};
  glm::vec3 emissive{0.0f};
  float metallic = 0.0f;
  float roughness = 0.8f;
  float alphaCutoff = 0.0f;
  uint32_t albedoTexIndex = 0;
  bool castShadows = true;
  bool twoSided = false;
  bool billboard = false; ///< LOD2 camera-facing impostor path
  uint32_t billboardViews = 8;
  uint32_t billboardGrid = 16;
};

struct Light {
  LightType type = LightType::Point;
  glm::vec3 position{0};
  glm::vec3 direction{0, -1, 0};
  glm::vec3 color{1};
  float intensity = 1.0f;
  float range = 10.0f;
  float innerCone = 0.0f;
  float outerCone = 0.0f;
  bool castShadows = true;
};

class Scene {
public:
  // Still public, and still a dense vector the renderer walks start to finish. What changed is that
  // **nothing removes from the middle of it any more**: a removed object becomes a dead slot that
  // `addObject` reuses. Every draw loop already skips objects with no mesh, so a dead slot costs one
  // branch it was paying anyway.
  //
  // `push_back` straight onto this still works — the slot table catches up lazily. `erase` does not:
  // see the note on `removeObjectAt`.
  std::vector<RenderObject> objects;
  std::vector<Light> lights;
  /// GPU-driven instance clouds to draw this frame (WM-6). Empty for scenes that use none.
  std::vector<InstanceCloudRender> instanceClouds;
  /// Optional continuous-LOD clipmap terrain, drawn into the deferred g-buffer. Null = none. Not
  /// owned by the scene; the caller keeps it alive and calls its update() before render().
  terrain::ClipmapTerrain* clipmapTerrain = nullptr;
  Camera camera;

  void addDirectional(const glm::vec3& dir, const glm::vec3& color, float intensity);
  void addPoint(const glm::vec3& pos, const glm::vec3& color, float intensity, float range);
  void addSpot(const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& color, float intensity, float range,
               float innerDeg, float outerDeg);

  // ── Object lifetime (C-09) ──────────────────────────────────────────────────

  /// Takes a free slot when there is one, otherwise appends. The handle stays valid until the object
  /// is removed, whatever happens to the objects around it.
  RenderObjectHandle addObject(RenderObject object);

  /// Empties the slot and bumps its generation, so every handle to it stops resolving. The slot is
  /// **not** erased: erasing shifts every index above it, which is the bug this replaces. The
  /// emptied object has no mesh and is invisible, so every draw loop already skips it.
  /// False when the index is out of range or the slot is already free.
  bool removeObjectAt(uint32_t index);
  bool removeObject(RenderObjectHandle handle);

  /// Removes every live object the predicate accepts, and returns how many. This is what the
  /// streaming providers want: they match on the id stamped into the object's name.
  template <typename Predicate>
  size_t removeObjectsIf(Predicate predicate) {
    size_t removed = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(objects.size()); ++i) {
      if (!objectAlive(i) || !predicate(objects[i])) continue;
      if (removeObjectAt(i)) ++removed;
    }
    return removed;
  }

  /// Drops every object and invalidates every handle. What a scene reload wants — and what a bare
  /// `objects.clear()` does *not* do, because it leaves the generations behind.
  void clearObjects();

  /// The object, or null when the handle refers to a slot that has since been freed or reused. This
  /// is the whole point: a stale handle reads as "gone", not as somebody else's object.
  RenderObject* resolve(RenderObjectHandle handle);
  const RenderObject* resolve(RenderObjectHandle handle) const;

  /// A handle for a live slot, `kInvalidRenderObject` otherwise. For the paths that still work in
  /// raw indices — picking returns one, and the pre-ECS Outliner selects with one.
  RenderObjectHandle handleAt(uint32_t index) const;
  bool objectAlive(uint32_t index) const;
  /// How many slots are occupied. Differs from `objects.size()`, which counts dead slots too.
  size_t liveObjectCount() const;

private:
  struct ObjectSlot {
    uint32_t generation = 1;
    bool alive = true;
  };

  /// Brings the slot table back in line with `objects` for code that appends (or clears) the vector
  /// directly, which most of the engine still does.
  ///
  /// Growing is trivial — the new slots are live at generation 0. **Shrinking is the interesting
  /// case**: it means somebody erased or cleared behind our back, so every index may now mean
  /// something else. Rather than guess which, every surviving slot's generation is bumped, which
  /// invalidates every outstanding handle at once. Callers see their objects as gone and recreate
  /// them — a visible, recoverable failure instead of a silent mix-up.
  void syncSlots() const;

  mutable std::vector<ObjectSlot> m_objectSlots;
  mutable std::vector<uint32_t> m_freeObjects;

  /// The generation a brand-new slot starts at.
  ///
  /// Starts at 1, not 0, so a **zero-initialised handle never resolves**: `EntityManager` hands out
  /// zeroed memory for a component created with `createWith`, and with generation 0 in use that
  /// zeroed `RenderObjectComponent` would be a valid handle to object number zero — the same silent
  /// mis-aim this whole change is about, arriving through the back door.
  ///
  /// It also survives `clearObjects`, which is what makes a reload safe. Bumping the generations and
  /// then dropping the table (the first thing tried here, and caught by the gate) does nothing: the
  /// table is what held them, so slot 0 came back at the same generation and a handle from before
  /// the reload resolved against whatever was loaded into that index.
  mutable uint32_t m_generationFloor = 1;
};

} // namespace tucano
