#pragma once

#include "RHI/RHI.h"
#include "Renderer/Scene.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

// daGI2-inspired world SDF: 4 camera-centered clipmap cascades (64³ each),
// mesh-raster triangle seeds + meshlet interior + 3D JFA signed distance.
// SH1 volume packs L1.xyz + L0 (sample: L0 + max(dot(L1,n),0)).
class WorldSDF {
public:
  static constexpr uint32_t kRes = 64;
  static constexpr uint32_t kCascades = 4;

  void init(rhi::Device& device);
  void update(rhi::Device& device, const Scene& scene, const glm::vec3& cameraPos, const glm::vec3& sunDir,
              float sunIntensity, float amortizeFraction = 0.35f);

  rhi::Texture* sdfAtlas() const { return m_sdf.get(); }
  rhi::Texture* shAtlas() const { return m_sh.get(); }
  glm::vec3 origin(uint32_t cascade) const { return m_origin[cascade]; }
  float extent(uint32_t cascade) const { return m_extent[cascade]; }
  uint32_t voxelsUpdated() const { return m_updated; }
  bool ready() const { return m_ready; }

  // Pack cascade origins/extents for GPU CB (4 × float4).
  void fillCascadeCB(glm::vec4 out[4]) const;

  // CPU sample helpers for reflection-probe bake / tools.
  float sampleDistanceCpu(const glm::vec3& worldPos) const;
  glm::vec3 sampleShCpu(const glm::vec3& worldPos) const;

private:
  void rebuildCascade(uint32_t cascade, const Scene& scene, const glm::vec3& sunDir, float sunIntensity);
  void uploadCascade(rhi::Device& device, uint32_t cascade);

  std::shared_ptr<rhi::Texture> m_sdf; // R32_FLOAT, W=kRes*kCascades, H=kRes*kRes
  std::shared_ptr<rhi::Texture> m_sh;  // RGBA16F, same layout (L0.rgb approx + L1.z in a unused; rgb=irradiance)
  std::vector<float> m_sdfCpu;        // full atlas
  std::vector<float> m_shCpu;         // RGBA interleaved
  std::array<glm::vec3, kCascades> m_origin{};
  std::array<float, kCascades> m_extent{16.f, 32.f, 64.f, 128.f};
  uint32_t m_cascadeCursor = 0;
  uint32_t m_updated = 0;
  bool m_ready = false;
};

} // namespace tucano
