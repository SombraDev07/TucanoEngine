#include "Renderer/Shadows/OctahedralShadows.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace tucano {

void OctahedralShadowAtlas::init(rhi::Device& device, uint32_t atlasSize, uint32_t tileSize) {
  m_atlasSize = atlasSize;
  m_tileSize = tileSize;
  m_tilesPerRow = std::max(1u, atlasSize / tileSize);
  rhi::TextureDesc d{};
  d.width = atlasSize;
  d.height = atlasSize;
  d.format = rhi::Format::R32_FLOAT;
  d.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  d.debugName = "OctaShadowAtlas";
  m_atlas = device.createTexture(d, nullptr, 0);
}

uint32_t OctahedralShadowAtlas::updateLights(const Scene& scene) {
  m_pointCount = 0;
  m_spotCount = 0;
  for (const auto& l : scene.lights) {
    if (!l.castShadows) {
      continue;
    }
    if (l.type == LightType::Point && m_pointCount < kMaxPointLights) {
      m_points[m_pointCount].pos = l.position;
      m_points[m_pointCount].range = std::max(l.range, 0.5f);
      ++m_pointCount;
    } else if (l.type == LightType::Spot && m_spotCount < kMaxSpotLights) {
      m_spots[m_spotCount].pos = l.position;
      m_spots[m_spotCount].dir = glm::normalize(l.direction);
      m_spots[m_spotCount].range = std::max(l.range, 0.5f);
      m_spots[m_spotCount].outerCos = (l.outerCone > 1.0f)
                                          ? std::cos(glm::radians(l.outerCone))
                                          : std::clamp(l.outerCone, -1.0f, 1.0f);
      ++m_spotCount;
    }
  }
  return m_pointCount;
}

glm::vec2 OctahedralShadowAtlas::tileUvOffset(uint32_t slot) const {
  const uint32_t x = slot % m_tilesPerRow;
  const uint32_t y = slot / m_tilesPerRow;
  const float s = float(m_tileSize) / float(m_atlasSize);
  return {x * s, y * s};
}

float OctahedralShadowAtlas::tileUvScale() const {
  return float(m_tileSize) / float(m_atlasSize);
}

glm::mat4 OctahedralShadowAtlas::spotViewProj(uint32_t spotIndex) const {
  const auto& S = m_spots[spotIndex];
  const glm::vec3 target = S.pos + S.dir;
  const glm::vec3 up = (std::abs(S.dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  const glm::mat4 view = glm::lookAtLH(S.pos, target, up);
  // Fit perspective to outer cone (+ margin)
  const float outerDeg = glm::degrees(std::acos(std::clamp(S.outerCos, -1.f, 1.f)));
  const float fov = glm::radians(std::min(outerDeg * 2.15f, 160.0f));
  const glm::mat4 proj = glm::perspectiveLH_ZO(fov, 1.0f, 0.05f, S.range);
  return proj * view;
}

glm::vec2 OctahedralShadowAtlas::encodeOcta(const glm::vec3& n) {
  const glm::vec3 v = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
  glm::vec2 enc = {v.x, v.y};
  if (n.z < 0.0f) {
    enc = (1.0f - glm::abs(glm::vec2(v.y, v.x))) *
          glm::vec2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
  }
  return enc * 0.5f + 0.5f;
}

glm::vec3 OctahedralShadowAtlas::decodeOcta(const glm::vec2& e) {
  glm::vec2 f = e * 2.0f - 1.0f;
  glm::vec3 n(f.x, f.y, 1.0f - std::abs(f.x) - std::abs(f.y));
  const float t = std::max(-n.z, 0.0f);
  n.x += (n.x >= 0.0f) ? -t : t;
  n.y += (n.y >= 0.0f) ? -t : t;
  return glm::normalize(n);
}

} // namespace tucano
