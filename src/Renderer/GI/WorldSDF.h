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
  // CPU seeds the surface voxels; the GPU runs the 3D JFA + finalize (SDF + SH) per cascade.
  void update(rhi::Device& device, rhi::CommandList& cmd, const Scene& scene, const glm::vec3& cameraPos,
              const glm::vec3& sunDir, float sunIntensity, float amortizeFraction = 0.35f);

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
  // CPU: rasterize triangles → surface seed volume + per-voxel albedo/inside aux.
  void seedCascade(uint32_t cascade, const Scene& scene);
  void ensureGpu(rhi::Device& device);

  std::shared_ptr<rhi::Texture> m_sdf; // R32_FLOAT, W=kRes*kCascades, H=kRes*kRes  (UAV)
  std::shared_ptr<rhi::Texture> m_sh;  // RGBA16F, same layout (UAV)
  std::vector<float> m_sdfCpu;         // CPU mirror (reflection-probe bake / tools)
  std::vector<float> m_shCpu;

  // GPU JFA resources.
  std::shared_ptr<rhi::Texture> m_seedA;   // R32_UINT, kRes³ (ping)
  std::shared_ptr<rhi::Texture> m_seedB;   // R32_UINT, kRes³ (pong)
  std::shared_ptr<rhi::Texture> m_aux;     // RGBA8, kRes³ (rgb=albedo, a=inside)
  std::shared_ptr<rhi::RootSignature> m_computeRoot;
  std::shared_ptr<rhi::PipelineState> m_jfaPSO;
  std::shared_ptr<rhi::PipelineState> m_finalizePSO;
  std::shared_ptr<rhi::Buffer> m_seedUpload; // persistent upload staging (256-aligned rows)
  std::shared_ptr<rhi::Buffer> m_auxUpload;
  std::vector<uint32_t> m_seedCpu; // kRes³ upload staging
  std::vector<uint32_t> m_auxCpu;  // kRes³ RGBA8 packed
  bool m_gpuReady = false;

  std::array<glm::vec3, kCascades> m_origin{};
  std::array<float, kCascades> m_extent{16.f, 32.f, 64.f, 128.f};
  uint32_t m_cascadeCursor = 0;
  uint32_t m_updated = 0;
  bool m_ready = false;
};

} // namespace tucano
