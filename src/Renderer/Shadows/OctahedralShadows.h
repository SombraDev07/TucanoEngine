#pragma once

#include "RHI/RHI.h"
#include "Renderer/Scene.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

// True octahedral point-light atlas + perspective spot tiles (Dagor shadowSystem-class).
class OctahedralShadowAtlas {
public:
  static constexpr int kMaxPointLights = 8;
  static constexpr int kMaxSpotLights = 4;

  void init(rhi::Device& device, uint32_t atlasSize = 2048, uint32_t tileSize = 512);

  uint32_t updateLights(const Scene& scene);

  // Point (octa) slots
  uint32_t pointCount() const { return m_pointCount; }
  glm::vec2 tileUvOffset(uint32_t slot) const;
  float tileUvScale() const;
  const glm::vec3& lightPos(uint32_t slot) const { return m_points[slot].pos; }
  float lightRange(uint32_t slot) const { return m_points[slot].range; }

  // Spot (perspective) slots — packed after points in the same atlas grid
  uint32_t spotCount() const { return m_spotCount; }
  uint32_t spotAtlasSlot(uint32_t spotIndex) const { return m_pointCount + spotIndex; }
  glm::mat4 spotViewProj(uint32_t spotIndex) const;
  const glm::vec3& spotPos(uint32_t i) const { return m_spots[i].pos; }
  const glm::vec3& spotDir(uint32_t i) const { return m_spots[i].dir; }
  float spotRange(uint32_t i) const { return m_spots[i].range; }
  float spotOuterCos(uint32_t i) const { return m_spots[i].outerCos; }

  rhi::Texture* atlas() const { return m_atlas.get(); }
  uint32_t tileSize() const { return m_tileSize; }
  uint32_t atlasSize() const { return m_atlasSize; }

  static glm::vec2 encodeOcta(const glm::vec3& n);
  static glm::vec3 decodeOcta(const glm::vec2& e);

private:
  struct PointSlot {
    glm::vec3 pos{0};
    float range = 1.0f;
  };
  struct SpotSlot {
    glm::vec3 pos{0};
    glm::vec3 dir{0, -1, 0};
    float range = 1.0f;
    float outerCos = 0.5f;
  };

  std::shared_ptr<rhi::Texture> m_atlas;
  std::array<PointSlot, kMaxPointLights> m_points{};
  std::array<SpotSlot, kMaxSpotLights> m_spots{};
  uint32_t m_pointCount = 0;
  uint32_t m_spotCount = 0;
  uint32_t m_atlasSize = 2048;
  uint32_t m_tileSize = 512;
  uint32_t m_tilesPerRow = 4;
};

} // namespace tucano
