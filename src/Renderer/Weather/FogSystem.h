#pragma once

#include "Renderer/Weather/FogParams.h"
#include "RHI/RHI.h"

#include <array>
#include <memory>

namespace tucano {

/// Owns the froxel volumes and the two compute passes that fill them. The renderer drives it once
/// per frame before lighting, then hands the integrated volume's bindless index to the lighting
/// pass, which applies `color * transmittance + scattering` per pixel.
class FogSystem {
public:
  void init(rhi::Device& device, std::shared_ptr<rhi::RootSignature> computeRootSig);
  void resize(rhi::Device& device, uint32_t width, uint32_t height);

  FogParams& params() { return m_params; }
  const FogParams& params() const { return m_params; }

  /// True when the volumes and pipelines exist and the parameters ask for volumetric fog.
  bool active() const { return m_ready && m_params.enabled && m_params.volumetric; }

  struct FrameContext {
    glm::mat4 invViewProj{1.0f};
    glm::mat4 prevViewProj{1.0f};
    glm::mat4 view{1.0f};
    glm::vec3 cameraPos{0.0f};
    float timeSeconds = 0.0f;
    glm::vec3 sunDir{0.0f, -1.0f, 0.0f}; ///< sun -> scene
    float sunIntensity = 1.0f;
    glm::vec3 sunColor{1.0f};
    glm::vec3 ambientColor{0.1f};
    glm::vec3 wind{0.0f};
    std::array<glm::mat4, 4> lightViewProj{};
    glm::vec4 cascadeSplits{0.0f};
    uint32_t shadowBindless = 0;
    uint32_t frameIndex = 0;
  };

  /// Records both compute passes. Safe to call when inactive — it does nothing.
  void execute(rhi::Device& device, rhi::CommandList& cmd, const FrameContext& ctx);

  /// Bindless index of the integrated volume for the lighting pass, or 0 when inactive.
  uint32_t integratedBindless() const;
  rhi::Texture* integratedVolume() const { return m_integrated.get(); }

  /// Grid dimensions, so the lighting pass can convert a pixel depth into a froxel coordinate.
  glm::vec3 volumeDimensions() const {
    return glm::vec3(float(m_dimX), float(m_dimY), float(m_dimZ));
  }

private:
  void createVolumes(rhi::Device& device);

  FogParams m_params;
  std::shared_ptr<rhi::RootSignature> m_rootCS;
  std::shared_ptr<rhi::PipelineState> m_injectPSO;
  std::shared_ptr<rhi::PipelineState> m_integratePSO;

  // Ping-ponged so a frame reads the other volume as history while writing its own.
  std::array<std::shared_ptr<rhi::Texture>, 2> m_scatter;
  std::shared_ptr<rhi::Texture> m_integrated;
  std::shared_ptr<rhi::Buffer> m_cb;

  uint32_t m_dimX = 0;
  uint32_t m_dimY = 0;
  uint32_t m_dimZ = 0;
  uint32_t m_writeIndex = 0;
  bool m_ready = false;
};

} // namespace tucano
