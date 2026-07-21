#pragma once

#include "RHI/RHI.h"
#include "Renderer/Scene.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

// Sparse-ish occupancy clipmap fallback for DDGI holes (daGI2-inspired, simplified).
// Stores a single 32^3 occupancy volume centered on the camera; updates temporally
// (amortizeFraction of voxels per frame) via CPU rasterization of scene AABBs.
class VoxelGI {
public:
  static constexpr uint32_t kRes = 32;

  void init(rhi::Device& device);
  void update(rhi::Device& device, const Scene& scene, float amortizeFraction = 0.25f);
  rhi::Texture* occupancy() const { return m_occupancy.get(); }
  float worldExtent() const { return m_extent; }
  glm::vec3 origin() const { return m_origin; }
  uint32_t voxelsUpdated() const { return m_updatedThisFrame; }

private:
  std::shared_ptr<rhi::Texture> m_occupancy;
  std::vector<uint8_t> m_cpu; // R8 occupancy
  glm::vec3 m_origin{0};
  float m_extent = 40.0f;
  uint32_t m_cursor = 0;
  uint32_t m_updatedThisFrame = 0;
  bool m_ready = false;
};

// SH L0 sky occlusion probe grid (toroidal clipmap, low-res).
class SkyVisibility {
public:
  static constexpr int kProbes = 16; // 16x16 XZ

  void init();
  void update(const Scene& scene, const glm::vec3& cameraPos, float amortizeFraction = 0.2f);
  float sample(const glm::vec3& worldPos) const; // 0=fully occluded, 1=open sky
  uint32_t probesUpdated() const { return m_updated; }

private:
  std::vector<float> m_sh0; // L0 coefficient per probe
  glm::vec2 m_originXZ{0};
  float m_spacing = 4.0f;
  uint32_t m_cursor = 0;
  uint32_t m_updated = 0;
};

} // namespace tucano
