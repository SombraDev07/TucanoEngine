#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>

namespace tucano {

struct AOPassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& rootFS;
  rhi::PipelineState& gtaoPSO;
  rhi::PipelineState& blurPSO;
  rhi::Buffer& postCB;
  uint64_t& cbBump;
  rhi::Texture& ao;
  rhi::Texture& aoTemp;
  rhi::Texture& depthColor;
  rhi::Texture& normal;
  rhi::Sampler& linearSamp;
  uint32_t width = 0;
  uint32_t height = 0;
  float intensity = 1.0f;
  float radius = 0.85f;
  float thickness = 0.25f;
  float power = 1.4f;
};

// Horizon GTAO + separable bilateral blur.
void executeAOPass(AOPassContext& ctx);

} // namespace tucano
