#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"
#include "Renderer/Camera.h"
#include "Renderer/LightType.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"

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
};

} // namespace tucano
