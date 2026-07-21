#pragma once

#include "Renderer/Weather/RainParams.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"

#include <array>
#include <memory>

namespace tucano {

class RainSystem {
public:
  void init(rhi::Device& device);
  void resize(rhi::Device& device, uint32_t width, uint32_t height);

  RainParams& params() { return m_params; }
  const RainParams& params() const { return m_params; }

  // After GBuffer, before lighting: wetness / puddles / splashes / ripples.
  void executeDeferredGBuffer(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& albedo,
                              rhi::Texture& normal, rhi::Texture& orm, rhi::Texture& depthColor,
                              rhi::Buffer& rainCB, rhi::Sampler& linearSamp, const glm::mat4& invViewProj,
                              const glm::mat4& viewProj, const glm::mat4& view, const glm::vec3& cameraPos,
                              float timeSeconds, uint32_t width, uint32_t height);

  // After HDR lighting: view-space streaks + puddle sheen + mist + lens drops.
  rhi::Texture* executePost(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& hdrIn,
                            rhi::Texture& hdrTemp, rhi::Texture& depthColor, rhi::Texture& normals,
                            rhi::Texture& bloomOrHdr, rhi::Buffer& rainCB, rhi::Sampler& linearSamp,
                            const glm::mat4& invViewProj, const glm::mat4& viewProj, const glm::mat4& view,
                            const glm::vec3& cameraPos, float timeSeconds, uint32_t width, uint32_t height);

private:
  void loadTextures(rhi::Device& device);
  void buildRainMesh(rhi::Device& device);
  void updateCB(rhi::Buffer& rainCB, const glm::mat4& invViewProj, const glm::mat4& viewProj,
                const glm::mat4& view, const glm::vec3& cameraPos, float timeSeconds, uint32_t width,
                uint32_t height, int layer, float amountScale);
  void runOcclusion(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& depthColor, rhi::Buffer& rainCB,
                    uint32_t sampTable, uint32_t width, uint32_t height);

  RainParams m_params;
  std::shared_ptr<rhi::RootSignature> m_rootFS;
  std::shared_ptr<rhi::RootSignature> m_rootMesh;
  std::shared_ptr<rhi::PipelineState> m_copySrgbPSO;
  std::shared_ptr<rhi::PipelineState> m_copyUnormPSO;
  std::shared_ptr<rhi::PipelineState> m_occPSO;
  std::shared_ptr<rhi::PipelineState> m_gbufferPSO;
  std::shared_ptr<rhi::PipelineState> m_streakPSO;
  std::shared_ptr<rhi::PipelineState> m_puddleSpecPSO;
  std::shared_ptr<rhi::PipelineState> m_mistPSO;
  std::shared_ptr<rhi::PipelineState> m_dropsPSO;

  std::shared_ptr<Texture> m_puddleMask;
  std::shared_ptr<Texture> m_rainSpatter;
  std::shared_ptr<Texture> m_surfaceFlow;
  std::shared_ptr<Texture> m_rainfall;
  std::shared_ptr<Texture> m_rainfallN;
  std::shared_ptr<Texture> m_moisture;
  std::shared_ptr<rhi::Texture> m_ripples; // Texture2DArray 24

  std::shared_ptr<rhi::Texture> m_albedoCopy;
  std::shared_ptr<rhi::Texture> m_normalCopy;
  std::shared_ptr<rhi::Texture> m_ormCopy;
  std::shared_ptr<rhi::Texture> m_occlusion;

  std::shared_ptr<rhi::Buffer> m_rainVB;
  uint32_t m_rainVertexCount = 0;

  std::shared_ptr<rhi::Sampler> m_wrapSamp;
  std::shared_ptr<rhi::Sampler> m_pointSamp;

  bool m_ready = false;
  static constexpr int kRippleCount = 24;
  static constexpr int kSlices = 12;
  static constexpr int kMaxLayers = 3;
};

} // namespace tucano
