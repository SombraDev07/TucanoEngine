#pragma once

#include "Renderer/Weather/WaterParams.h"
#include "RHI/RHI.h"

#include <memory>

namespace tucano {

class WaterSystem {
public:
  void init(rhi::Device& device, std::shared_ptr<rhi::RootSignature> sharedRootFS);
  void resize(uint32_t width, uint32_t height);

  WaterParams& params() { return m_params; }
  const WaterParams& params() const { return m_params; }

  void execute(rhi::CommandList& cmd, rhi::Texture& hdr, rhi::Texture& depthColor,
               rhi::Texture* weatherMap, rhi::Buffer& waterCB, rhi::Sampler& linearSamp,
               const glm::mat4& invViewProj, const glm::mat4& viewProj, const glm::mat4& view,
               const glm::vec3& cameraPos, const glm::vec3& sunDir, const glm::vec3& sunColor,
               float turbidity, float timeSeconds, uint32_t width, uint32_t height,
               uint32_t depthBindless, uint32_t hdrBindless, uint32_t weatherBindless,
               uint32_t prefilteredBindless, float iblMaxMip, float iblExposure);

  std::shared_ptr<rhi::PipelineState> waterPSO() { return m_pso; }
  const std::shared_ptr<rhi::PipelineState>& waterPSO() const { return m_pso; }

private:
  WaterParams m_params;
  std::shared_ptr<rhi::PipelineState> m_pso;
  bool m_ready = false;
};

} // namespace tucano
