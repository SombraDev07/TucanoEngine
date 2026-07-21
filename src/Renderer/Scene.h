#pragma once

#include "Renderer/Camera.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"

#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

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
  Camera camera;

  void addDirectional(const glm::vec3& dir, const glm::vec3& color, float intensity);
  void addPoint(const glm::vec3& pos, const glm::vec3& color, float intensity, float range);
  void addSpot(const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& color, float intensity, float range,
               float innerDeg, float outerDeg);
};

} // namespace tucano
