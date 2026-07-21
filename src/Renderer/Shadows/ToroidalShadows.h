#pragma once

#include "Renderer/Camera.h"

#include <array>
#include <glm/glm.hpp>

namespace tucano {

// Cascaded shadow atlas with toroidal wrapping (Dagor toroidalStaticShadows algorithm, reimplemented).
// Each cascade snaps its ortho center to texel boundaries; camera motion scrolls the depth map
// instead of rebuilding the whole cascade every frame when possible.
struct ToroidalCascade {
  glm::vec2 originXZ{0.0f};
  float texelWorld = 1.0f;
  float radius = 10.0f;
  int scrollX = 0;
  int scrollY = 0;
  bool dirty = true;
  uint32_t framesSinceFull = 0;
  int lastScrollX = 0;
  int lastScrollY = 0;
};

class ToroidalShadowAtlas {
public:
  static constexpr int kCascades = 4;

  void configure(uint32_t atlasSize, const float cascadeEnds[4]);
  // Updates cascade origins from camera; returns per-cascade dirty flags.
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
  uint32_t m_atlasSize = 2048;
  uint32_t m_tileSize = 1024;
};

} // namespace tucano
