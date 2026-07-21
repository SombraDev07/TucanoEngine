#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tucano {

class Camera {
public:
  void setPerspective(float fovYRadians, float aspect, float zNear, float zFar);
  void setPosition(const glm::vec3& p) { m_position = p; update(); }
  void lookAt(const glm::vec3& target);
  void fly(float dt, const glm::vec3& move, float yawDelta, float pitchDelta);

  const glm::mat4& view() const { return m_view; }
  const glm::mat4& proj() const { return m_proj; }
  glm::mat4 viewProj() const { return m_proj * m_view; }
  const glm::vec3& position() const { return m_position; }
  float nearPlane() const { return m_near; }
  float farPlane() const { return m_far; }
  glm::vec3 forward() const;
  glm::vec3 right() const;
  glm::vec3 up() const { return {0, 1, 0}; }

private:
  void update();
  glm::vec3 m_position{0, 2, 5};
  float m_yaw = 0.0f;
  float m_pitch = 0.0f;
  float m_fov = glm::radians(60.0f);
  float m_aspect = 16.0f / 9.0f;
  float m_near = 0.1f;
  float m_far = 200.0f;
  glm::mat4 m_view{1.0f};
  glm::mat4 m_proj{1.0f};
};

} // namespace tucano
