#pragma once

#include "RHI/RHI.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace tucano {

struct BloomPassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& rootFS;
  rhi::PipelineState& downPSO;
  rhi::PipelineState& upPSO;
  rhi::Buffer& postCB;
  uint64_t& cbBump;
  rhi::Texture& hdrSrc;
  std::array<std::shared_ptr<rhi::Texture>, 5>& mips;
  rhi::Texture* scratch = nullptr;
  rhi::Sampler& linearSamp;
  rhi::Viewport fullViewport{};
  rhi::Scissor fullScissor{};
  uint32_t levels = 4;
};

void executeBloomPass(BloomPassContext& ctx);

} // namespace tucano
