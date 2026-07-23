#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>

namespace tucano {

struct ExposurePassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& rootCS;
  rhi::PipelineState& clearPSO;
  rhi::PipelineState& buildPSO;
  rhi::PipelineState& reducePSO;
  rhi::Buffer& exposureCB;
  rhi::Texture& hdr;
  rhi::Texture& histogram; // R32_UINT 256x1
  rhi::Texture& exposure;  // R32_FLOAT 1x1
  rhi::Sampler& linearSamp;
  uint32_t width = 0;
  uint32_t height = 0;
  float minExposure = 0.08f;
  float maxExposure = 4.0f;
  float adaptSpeed = 0.08f;
  float targetLuma = 0.18f;
};

void executeExposurePass(ExposurePassContext& ctx);

struct TonemapPassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& rootFS;
  rhi::PipelineState& tonemapPSO;
  rhi::Buffer& postCB;
  uint64_t& cbBump;
  rhi::Texture& hdr;
  rhi::Texture& bloom;
  rhi::Texture* exposureTex = nullptr; // optional 1x1
  rhi::Texture& swapChainRT;
  rhi::Sampler& linearSamp;
  float bloomStrength = 0.25f;
  float exposureFallback = 1.0f;
  /// Purkinje strength: how strongly dim parts of the frame desaturate toward blue. 0 disables it.
  float purkinjeStrength = 0.0f;
  rhi::Viewport viewport{};
  rhi::Scissor scissor{};
};

void executeTonemapPass(TonemapPassContext& ctx);

} // namespace tucano
