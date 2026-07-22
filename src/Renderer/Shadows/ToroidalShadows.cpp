#include "Renderer/Shadows/ToroidalShadows.h"
#include "Renderer/Shadows/ShadowMath.h"

#include <algorithm>
#include <cmath>

namespace tucano {

void ToroidalShadowAtlas::configure(uint32_t atlasSize, const float cascadeEnds[4]) {
  m_atlasSize = atlasSize;
  m_tileSize = atlasSize / 2;
  for (int i = 0; i < kCascades; ++i) {
    auto& c = m_cascades[static_cast<size_t>(i)];
    m_ends[static_cast<size_t>(i)] = cascadeEnds[i];
    // Box must cover the whole split range around the camera (0.55 left a shadowless ring
    // between radius and the split distance); 1.05 adds a filtering guard band.
    c.radius = cascadeEnds[i] * 1.05f;
    c.texelWorld = (c.radius * 2.0f) / static_cast<float>(std::max(1u, m_tileSize));
    c.dirty = true;
    c.fullRefresh = true;
  }
}

void ToroidalShadowAtlas::update(const Camera& cam, const glm::vec3& lightDir) {
  const glm::vec3 dir = glm::normalize(lightDir);
  // Sun moved → every cascade's cached depth is stale (view matrix changed).
  const bool sunMoved = glm::dot(dir, m_lastLightDir) < 0.99996f; // ~0.5 degree
  m_lastLightDir = dir;

  const glm::mat4 lightView = shadows::lightDirOrthoMatrix(dir);
  const glm::vec3 eyeLS = glm::vec3(lightView * glm::vec4(cam.position(), 1.0f));

  for (int i = 0; i < kCascades; ++i) {
    auto& c = m_cascades[static_cast<size_t>(i)];
    const float tw = std::max(c.texelWorld, 1e-4f);

    // Snap desired center to the light-space texel grid (no crawl for any sun angle).
    const float snapX = std::floor(eyeLS.x / tw) * tw;
    const float snapY = std::floor(eyeLS.y / tw) * tw;
    // Depth window recenters in coarse steps so cached depth stays valid between them.
    const float zStep = std::max(c.radius, 1.0f);
    const float snapZ = std::floor(eyeLS.z / zStep) * zStep;

    const int sx = static_cast<int>(std::lround((snapX - c.centerLS.x) / tw));
    // Atlas V grows downward while light-space Y grows upward → texture scroll is negated.
    const int sy = -static_cast<int>(std::lround((snapY - c.centerLS.y) / tw));
    const bool zMoved = std::abs(snapZ - c.zCenter) > zStep * 0.5f;

    ++c.framesSinceFull;
    const bool bigScroll = std::abs(sx) > int(m_tileSize) / 4 || std::abs(sy) > int(m_tileSize) / 4;
    if (c.fullRefresh || sunMoved || zMoved || c.framesSinceFull > 240 || bigScroll) {
      c.centerLS = {snapX, snapY};
      c.zCenter = snapZ;
      c.lastScrollX = 0;
      c.lastScrollY = 0;
      c.dirty = true;
      c.fullRefresh = true;
      c.framesSinceFull = 0;
    } else if (sx != 0 || sy != 0) {
      c.centerLS = {snapX, snapY};
      c.lastScrollX = sx;
      c.lastScrollY = sy;
      c.dirty = true;
      c.fullRefresh = false;
    } else {
      c.lastScrollX = 0;
      c.lastScrollY = 0;
    }
  }
}

glm::mat4 ToroidalShadowAtlas::viewProj(int cascade, const glm::vec3& lightDir) const {
  const auto& c = m_cascades[static_cast<size_t>(cascade)];
  const glm::mat4 lightView = shadows::lightDirOrthoMatrix(glm::normalize(lightDir));
  const float r = c.radius;
  // Ortho box around the snapped light-space center; Z window covers ±3r along the light.
  const glm::mat4 lightProj = glm::orthoLH_ZO(c.centerLS.x - r, c.centerLS.x + r, c.centerLS.y - r,
                                              c.centerLS.y + r, c.zCenter - 3.0f * r, c.zCenter + 3.0f * r);
  return lightProj * lightView;
}

} // namespace tucano
