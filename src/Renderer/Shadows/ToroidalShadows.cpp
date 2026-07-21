#include "Renderer/Shadows/ToroidalShadows.h"

#include <algorithm>
#include <cmath>

namespace tucano {

void ToroidalShadowAtlas::configure(uint32_t atlasSize, const float cascadeEnds[4]) {
  m_atlasSize = atlasSize;
  m_tileSize = atlasSize / 2;
  for (int i = 0; i < kCascades; ++i) {
    m_ends[static_cast<size_t>(i)] = cascadeEnds[i];
    m_cascades[static_cast<size_t>(i)].radius = cascadeEnds[i] * 0.55f;
    m_cascades[static_cast<size_t>(i)].texelWorld =
        (m_cascades[static_cast<size_t>(i)].radius * 2.0f) / static_cast<float>(std::max(1u, m_tileSize));
    m_cascades[static_cast<size_t>(i)].dirty = true;
  }
}

void ToroidalShadowAtlas::update(const Camera& cam, const glm::vec3& lightDir) {
  (void)lightDir;
  const glm::vec3 eye = cam.position();
  for (int i = 0; i < kCascades; ++i) {
    auto& c = m_cascades[static_cast<size_t>(i)];
    const float tw = std::max(c.texelWorld, 1e-4f);
    // Snap desired center to light-space texel grid in XZ (world-aligned approximation).
    const float snapX = std::floor(eye.x / tw) * tw;
    const float snapZ = std::floor(eye.z / tw) * tw;

    const float dx = snapX - c.originXZ.x;
    const float dz = snapZ - c.originXZ.y;
    const int sx = static_cast<int>(std::lround(dx / tw));
    const int sy = static_cast<int>(std::lround(dz / tw));

    ++c.framesSinceFull;
    // Full refresh periodically or on large scroll (wrap region would dominate).
    if (c.dirty || c.framesSinceFull > 90 || std::abs(sx) > int(m_tileSize) / 4 || std::abs(sy) > int(m_tileSize) / 4) {
      c.originXZ = {snapX, snapZ};
      c.scrollX = 0;
      c.scrollY = 0;
      c.dirty = true;
      c.framesSinceFull = 0;
    } else if (sx != 0 || sy != 0) {
      c.originXZ = {snapX, snapZ};
      c.lastScrollX = sx;
      c.lastScrollY = sy;
      c.scrollX += sx;
      c.scrollY += sy;
      c.dirty = true;
    } else {
      c.lastScrollX = 0;
      c.lastScrollY = 0;
    }
  }
}

glm::mat4 ToroidalShadowAtlas::viewProj(int cascade, const glm::vec3& lightDir) const {
  const auto& c = m_cascades[static_cast<size_t>(cascade)];
  const glm::vec3 center(c.originXZ.x, 0.0f, c.originXZ.y);
  const float radius = c.radius;
  const glm::vec3 lightPos = center - glm::normalize(lightDir) * radius * 2.0f;
  const glm::mat4 lightView = glm::lookAtLH(lightPos, center, glm::vec3(0, 1, 0));
  const glm::mat4 lightProj = glm::orthoLH_ZO(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
  return lightProj * lightView;
}

} // namespace tucano
