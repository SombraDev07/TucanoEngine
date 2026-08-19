#include "Renderer/PostFX/BloomPass.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace tucano {
namespace {

uint32_t bindlessOf(rhi::Texture& t) { return t.bindlessIndex(); }

struct PostCB {
  glm::vec4 params;
  glm::vec4 texelSize;
  glm::uvec4 texIds;
  glm::vec4 gtaoParams;
};

uint64_t pushUploadCB(rhi::Buffer& buffer, uint64_t& bump, const void* data, size_t size) {
  const uint64_t aligned = (static_cast<uint64_t>(size) + 255ull) & ~255ull;
  if (bump + aligned > buffer.size()) {
    bump = 0;
  }
  std::memcpy(static_cast<char*>(buffer.mapped()) + bump, data, size);
  const uint64_t off = bump;
  bump += aligned;
  return off;
}

void drawFS(BloomPassContext& ctx, uint32_t sampTable, rhi::PipelineState& pso, rhi::Texture& dest,
            const PostCB& pcb) {
  ctx.cmd.transition(dest, rhi::ResourceState::RenderTarget);
  rhi::Texture* brt = &dest;
  ctx.cmd.setRenderTargets(std::span<rhi::Texture*>(&brt, 1), nullptr);
  ctx.cmd.setPipeline(pso);
  const uint64_t off = pushUploadCB(ctx.postCB, ctx.cbBump, &pcb, sizeof(pcb));
  ctx.cmd.setGraphicsRootCBV(1, ctx.postCB, off);
  ctx.cmd.setGraphicsRootSrvTable(3, 0);
  ctx.cmd.setGraphicsRootSamplerTable(4, sampTable);
  ctx.cmd.setViewport({0, 0, float(dest.width()), float(dest.height()), 0, 1});
  ctx.cmd.setScissor({0, 0, int(dest.width()), int(dest.height())});
  ctx.cmd.draw(3);
  ctx.cmd.transition(dest, rhi::ResourceState::ShaderResource);
}

} // namespace

void executeBloomPass(BloomPassContext& ctx) {
  rhi::Sampler* samplers[] = {&ctx.linearSamp};
  const uint32_t sampTable = ctx.device.writeSamplerTable(samplers);
  const uint32_t levels = std::min(ctx.levels, static_cast<uint32_t>(ctx.mips.size()));

  ctx.cmd.setRootSignature(ctx.rootFS);

  rhi::Texture* src = &ctx.hdrSrc;
  for (uint32_t i = 0; i < levels; ++i) {
    PostCB pcb{{1, 0, 0, 0},
               {1.f / float(ctx.mips[i]->width()), 1.f / float(ctx.mips[i]->height()), 0, 0},
               {bindlessOf(*src), 0, 0, 0},
               {}};
    drawFS(ctx, sampTable, ctx.downPSO, *ctx.mips[i], pcb);
    src = ctx.mips[i].get();
  }

  // Upsample: fold coarse → fine. Scratch must be ≥ mips[0].
  if (!ctx.scratch) {
    ctx.cmd.setViewport(ctx.fullViewport);
    ctx.cmd.setScissor(ctx.fullScissor);
    return;
  }

  rhi::Texture* low = ctx.mips[levels - 1].get();
  for (int i = int(levels) - 2; i >= 0; --i) {
    auto& high = *ctx.mips[i];
    PostCB pcb{{1, 0, 0, 0},
               {1.f / float(high.width()), 1.f / float(high.height()), 0, 0},
               {bindlessOf(*low), bindlessOf(high), 0, 0},
               {}};
    drawFS(ctx, sampTable, ctx.upPSO, *ctx.scratch, pcb);
    PostCB copyCb{{1, 0, 0, 0},
                  {1.f / float(high.width()), 1.f / float(high.height()), 0, 0},
                  {bindlessOf(*ctx.scratch), 0, 0, 0},
                  {}};
    drawFS(ctx, sampTable, ctx.downPSO, high, copyCb);
    low = &high;
  }

  ctx.cmd.setViewport(ctx.fullViewport);
  ctx.cmd.setScissor(ctx.fullScissor);
}

} // namespace tucano
