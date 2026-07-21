#include "Renderer/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tucano {

glm::mat4 Transform::matrix() const {
  const glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
  const glm::mat4 R = glm::mat4_cast(rotation);
  const glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
  return T * R * S;
}

void Scene::addDirectional(const glm::vec3& dir, const glm::vec3& color, float intensity) {
  Light l;
  l.type = LightType::Directional;
  l.direction = glm::normalize(dir);
  l.color = color;
  l.intensity = intensity;
  lights.push_back(l);
}

void Scene::addPoint(const glm::vec3& pos, const glm::vec3& color, float intensity, float range) {
  Light l;
  l.type = LightType::Point;
  l.position = pos;
  l.color = color;
  l.intensity = intensity;
  l.range = range;
  lights.push_back(l);
}

void Scene::addSpot(const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& color, float intensity, float range,
                    float innerDeg, float outerDeg) {
  Light l;
  l.type = LightType::Spot;
  l.position = pos;
  l.direction = glm::normalize(dir);
  l.color = color;
  l.intensity = intensity;
  l.range = range;
  l.innerCone = glm::cos(glm::radians(innerDeg));
  l.outerCone = glm::cos(glm::radians(outerDeg));
  lights.push_back(l);
}

} // namespace tucano
