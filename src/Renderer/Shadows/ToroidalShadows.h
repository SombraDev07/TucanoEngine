#pragma once

#include "Renderer/Camera.h"

#include <array>
#include <glm/glm.hpp>

namespace tucano {

// Cascaded shadow atlas with toroidal scrolling (Dagor toroidalStaticShadows, ported math).
// Each cascade snaps its ortho center to texel boundaries IN LIGHT SPACE (stable for any sun
// direction); camera motion scrolls the cached depth and only the newly exposed strips are
// re-rendered (incremental invalidation).
struct ToroidalCascade {
  glm::vec2 centerLS{0.0f}; // snapped ortho center in light-space XY
  float zCenter = 0.0f;     // snapped light-space Z of the coverage center
  float texelWorld = 1.0f;
  float radius = 10.0f;
  int lastScrollX = 0; // texel scroll applied this frame (atlas convention)
  int lastScrollY = 0;
  bool dirty = true;
  bool fullRefresh = true; // dirty without valid scrolled content (render whole tile)
  uint32_t framesSinceFull = 0;
};

class ToroidalShadowAtlas {
public:
  static constexpr int kCascades = 4;

  void configure(uint32_t atlasSize, const float cascadeEnds[4]);
  // Updates cascade origins from camera + sun; marks dirty/scroll state per cascade.
  void update(const Camera& cam, const glm::vec3& lightDir);
  glm::mat4 viewProj(int cascade, const glm::vec3& lightDir) const;
  const ToroidalCascade& cascade(int i) const { return m_cascades[static_cast<size_t>(i)]; }
  bool isDirty(int i) const { return m_cascades[static_cast<size_t>(i)].dirty; }
  void clearDirty(int i) { m_cascades[static_cast<size_t>(i)].dirty = false; }
  float split(int i) const { return m_ends[static_cast<size_t>(i)]; }
  uint32_t tileSize() const { return m_tileSize; }

private:
  std::array<ToroidalCascade, kCascades> m_cascades{};
  std::array<float, kCascades> m_ends{5.f, 20.f, 60.f, 200.f};
  glm::vec3 m_lastLightDir{0.0f, -1.0f, 0.0f};
  uint32_t m_atlasSize = 2048;
  uint32_t m_tileSize = 1024;
};

} // namespace tucano
