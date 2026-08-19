#include "Renderer/PostFX/ExposurePass.h"

#include <cstring>
#include <span>

namespace tucano {
namespace {

uint32_t bindlessOf(rhi::Texture& t) { return t.bindlessIndex(); }

void updateCB(rhi::Buffer& buffer, const void* data, size_t size) {
  std::memcpy(buffer.mapped(), data, size);
}

struct ExposureCB {
  glm::vec4 screenSize;
  glm::vec4 adaptParams;
  glm::uvec4 texIds;
};

struct PostCB {
  glm::vec4 params;
  glm::vec4 texelSize;
  glm::uvec4 texIds;
  glm::vec4 gtaoParams;
};

} // namespace

void executeExposurePass(ExposurePassContext& ctx) {
  rhi::Sampler* samplers[] = {&ctx.linearSamp};
  const uint32_t sampTable = ctx.device.writeSamplerTable(samplers);

  ExposureCB cb{{float(ctx.width), float(ctx.height), 1.f / float(ctx.width), 1.f / float(ctx.height)},
                {ctx.minExposure, ctx.maxExposure, ctx.adaptSpeed, ctx.targetLuma},
                {bindlessOf(ctx.hdr), 0, 0, 0}};
  updateCB(ctx.exposureCB, &cb, sizeof(cb));

  ctx.cmd.transition(ctx.histogram, rhi::ResourceState::UnorderedAccess);
  ctx.cmd.transition(ctx.exposure, rhi::ResourceState::UnorderedAccess);
  ctx.cmd.setRootSignature(ctx.rootCS);
  ctx.cmd.setComputeRootCBV(1, ctx.exposureCB);
  ctx.cmd.setComputeRootSrvTable(2, 0);
  ctx.cmd.setComputeRootSamplerTable(4, sampTable);

  // Clear histogram
  ctx.cmd.setPipeline(ctx.clearPSO);
  ctx.cmd.setComputeRootUavTable(3, ctx.histogram.bindlessUavIndex());
  ctx.cmd.dispatch((256 + 63) / 64, 1, 1);
  ctx.cmd.uavBarrier(&ctx.histogram);
  ctx.cmd.flushBarriers();

  // Build
  ctx.cmd.setPipeline(ctx.buildPSO);
  ctx.cmd.setComputeRootUavTable(3, ctx.histogram.bindlessUavIndex());
  ctx.cmd.dispatch((ctx.width + 7) / 8, (ctx.height + 7) / 8, 1);
  ctx.cmd.uavBarrier(&ctx.histogram);
  ctx.cmd.flushBarriers();

  // Reduce: histogram u0 + exposure u1 contiguous table
  rhi::Texture* uavs[] = {&ctx.histogram, &ctx.exposure};
  const uint32_t uavBase = ctx.device.writeTextureUavTable(uavs);
  ctx.cmd.setPipeline(ctx.reducePSO);
  ctx.cmd.setComputeRootUavTable(3, uavBase);
  ctx.cmd.dispatch(1, 1, 1);
  ctx.cmd.uavBarrier(&ctx.exposure);
  ctx.cmd.flushBarriers();

  ctx.cmd.transition(ctx.exposure, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.histogram, rhi::ResourceState::ShaderResource);
}

void executeTonemapPass(TonemapPassContext& ctx) {
  rhi::Sampler* samplers[] = {&ctx.linearSamp};
  const uint32_t sampTable = ctx.device.writeSamplerTable(samplers);

  ctx.cmd.transition(ctx.hdr, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.bloom, rhi::ResourceState::ShaderResource);
  if (ctx.exposureTex) {
    ctx.cmd.transition(*ctx.exposureTex, rhi::ResourceState::ShaderResource);
  }
  ctx.cmd.transition(ctx.swapChainRT, rhi::ResourceState::RenderTarget);
  rhi::Texture* bb = &ctx.swapChainRT;
  ctx.cmd.setRenderTargets(std::span<rhi::Texture*>(&bb, 1), nullptr);
  ctx.cmd.setRootSignature(ctx.rootFS);
  ctx.cmd.setPipeline(ctx.tonemapPSO);

  const uint32_t expIdx = ctx.exposureTex ? bindlessOf(*ctx.exposureTex) : 0u;
  PostCB tcb{{ctx.exposureFallback, ctx.bloomStrength, ctx.purkinjeStrength, 0},
             {0, 0, 0, 0},
             {bindlessOf(ctx.hdr), bindlessOf(ctx.bloom), expIdx, 0},
             {}};
  const uint64_t aligned = (sizeof(tcb) + 255ull) & ~255ull;
  if (ctx.cbBump + aligned > ctx.postCB.size()) {
    ctx.cbBump = 0;
  }
  std::memcpy(static_cast<char*>(ctx.postCB.mapped()) + ctx.cbBump, &tcb, sizeof(tcb));
  const uint64_t off = ctx.cbBump;
  ctx.cbBump += aligned;
  ctx.cmd.setGraphicsRootCBV(1, ctx.postCB, off);
  ctx.cmd.setGraphicsRootSrvTable(3, 0);
  ctx.cmd.setGraphicsRootSamplerTable(4, sampTable);
  ctx.cmd.setViewport(ctx.viewport);
  ctx.cmd.setScissor(ctx.scissor);
  ctx.cmd.draw(3);
}

} // namespace tucano
