#pragma once

#include <glm/glm.hpp>

namespace tucano {

class AudioListener {
public:
  static AudioListener& instance();

  void setPosition(const glm::vec3& pos);
  glm::vec3 position() const { return m_position; }

  void setOrientation(const glm::vec3& forward, const glm::vec3& up);
  glm::vec3 forward() const { return m_forward; }
  glm::vec3 up() const { return m_up; }

  void applyToEngine();

private:
  AudioListener() = default;

  glm::vec3 m_position{0};
  glm::vec3 m_forward{0, 0, -1};
  glm::vec3 m_up{0, 1, 0};
};

} // namespace tucano
