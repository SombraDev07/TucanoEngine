#include "Renderer/Camera.h"

#include <algorithm>
#include <cmath>

namespace tucano {

void Camera::setPerspective(float fovYRadians, float aspect, float zNear, float zFar) {
  m_fov = fovYRadians;
  m_aspect = aspect;
  m_near = zNear;
  m_far = zFar;
  // Explicit LH ZO to match engine defines even if GLM flags differ per TU
  m_proj = glm::perspectiveLH_ZO(m_fov, m_aspect, m_near, m_far);
}

void Camera::lookAt(const glm::vec3& target) {
  const glm::vec3 dir = glm::normalize(target - m_position);
  m_pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
  m_yaw = std::atan2(dir.x, dir.z);
  update();
}

glm::vec3 Camera::forward() const {
  return glm::normalize(glm::vec3(std::sin(m_yaw) * std::cos(m_pitch), std::sin(m_pitch),
                                  std::cos(m_yaw) * std::cos(m_pitch)));
}

glm::vec3 Camera::right() const { return glm::normalize(glm::cross(forward(), up())); }

void Camera::fly(float dt, const glm::vec3& move, float yawDelta, float pitchDelta) {
  m_yaw += yawDelta;
  m_pitch = std::clamp(m_pitch + pitchDelta, -1.55f, 1.55f);
  m_position += (forward() * move.z + right() * move.x + up() * move.y) * dt;
  update();
}

void Camera::update() {
  m_view = glm::lookAtLH(m_position, m_position + forward(), up());
}

} // namespace tucano
