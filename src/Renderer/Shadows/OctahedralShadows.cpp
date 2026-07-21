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
  m_count = 0;
  for (const auto& l : scene.lights) {
    if (l.type != LightType::Point || m_count >= kMaxLights) {
      continue;
    }
    m_lights[m_count].pos = l.position;
    m_lights[m_count].range = std::max(l.range, 0.5f);
    ++m_count;
  }
  return m_count;
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

glm::mat4 OctahedralShadowAtlas::lightViewProj(uint32_t slot, int hemi) const {
  const auto& L = m_lights[slot];
  const glm::vec3 target = L.pos + (hemi == 0 ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1));
  const glm::mat4 view = glm::lookAtLH(L.pos, target, glm::vec3(0, 1, 0));
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.05f, L.range);
  return proj * view;
}

glm::vec2 OctahedralShadowAtlas::encodeOcta(const glm::vec3& n) {
  const glm::vec3 v = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
  glm::vec2 enc = {v.x, v.y};
  if (n.z < 0.0f) {
    enc = (1.0f - glm::abs(glm::vec2(v.y, v.x))) * glm::vec2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
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
