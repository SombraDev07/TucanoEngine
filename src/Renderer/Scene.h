#pragma once

#include "Renderer/Camera.h"
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

struct Transform {
  glm::vec3 translation{0};
  glm::quat rotation{1, 0, 0, 0};
  glm::vec3 scale{1};
  glm::mat4 matrix() const;
};

struct RenderObject {
  std::shared_ptr<Mesh> mesh;
  std::vector<std::shared_ptr<Material>> materials;
  Transform transform;
  glm::mat4 worldMatrix{1.0f};
  std::string name;
  // Editor visibility. Hidden objects are skipped by every pass — including shadows, ray tracing
  // and GI — so hiding one removes it from the image completely, not just from the g-buffer.
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
  rhi::Buffer* instanceBuffer = nullptr; ///< StructuredBuffer<InstanceGpu>, per-instance transforms
  rhi::Buffer* visibleBuffer = nullptr;  ///< RWStructuredBuffer<uint>, compacted visible indices
  rhi::Buffer* argsBuffer = nullptr;     ///< DrawIndexedInstanced args, element 0 (indirect)
  Mesh* mesh = nullptr;                  ///< the shared single-submesh mesh every instance draws
  glm::vec4 baseColor{0.5f, 0.5f, 0.5f, 1.0f};
  glm::vec3 emissive{0.0f};
  float metallic = 0.0f;
  float roughness = 0.8f;
};

enum class LightType : uint32_t { Directional = 0, Point = 1, Spot = 2 };

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
