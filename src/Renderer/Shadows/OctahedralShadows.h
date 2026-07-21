#pragma once

#include "RHI/RHI.h"
#include "Renderer/Scene.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace tucano {

// Octahedral point-light shadow atlas (Dagor shadowSystem-inspired).
// Each point light occupies one octahedral hemi-pair tile in a 2D atlas.
class OctahedralShadowAtlas {
public:
  static constexpr int kMaxLights = 8;

  void init(rhi::Device& device, uint32_t atlasSize = 2048, uint32_t tileSize = 512);
  // Update light slots from scene point lights; returns count used.
  uint32_t updateLights(const Scene& scene);
  glm::mat4 lightViewProj(uint32_t slot, int hemi) const; // hemi 0=+Z, 1=-Z octa pack
  glm::vec2 tileUvOffset(uint32_t slot) const;
  float tileUvScale() const;
  rhi::Texture* atlas() const { return m_atlas.get(); }
  uint32_t lightCount() const { return m_count; }
  uint32_t tileSize() const { return m_tileSize; }
  const glm::vec3& lightPos(uint32_t slot) const { return m_lights[slot].pos; }
  float lightRange(uint32_t slot) const { return m_lights[slot].range; }

  // Octahedral encode/decode helpers for shaders (CPU reference).
  static glm::vec2 encodeOcta(const glm::vec3& n);
  static glm::vec3 decodeOcta(const glm::vec2& e);

private:
  struct Slot {
    glm::vec3 pos{0};
    float range = 1.0f;
  };
  std::shared_ptr<rhi::Texture> m_atlas;
  std::array<Slot, kMaxLights> m_lights{};
  uint32_t m_count = 0;
  uint32_t m_atlasSize = 2048;
  uint32_t m_tileSize = 512;
  uint32_t m_tilesPerRow = 4;
};

} // namespace tucano
