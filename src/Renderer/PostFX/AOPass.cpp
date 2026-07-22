#include "Renderer/PostFX/AOPass.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <cstring>
#include <span>

namespace tucano {
namespace {

::tucano::rhi::DX12Sampler& asDxS(rhi::Sampler& s) { return static_cast<::tucano::rhi::DX12Sampler&>(s); }
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

} // namespace

void executeAOPass(AOPassContext& ctx) {
  auto& dx = static_cast<rhi::DX12Device&>(ctx.device);
  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {asDxS(ctx.linearSamp).cpu};
  const uint32_t sampTable = dx.writeSamplerTable(sampCpu, 1);

  auto drawGtao = [&](rhi::PipelineState& pso, rhi::Texture& dest, uint32_t srcAo, float blurDir) {
    ctx.cmd.transition(dest, rhi::ResourceState::RenderTarget);
    rhi::Texture* rt = &dest;
    ctx.cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
    ctx.cmd.setRootSignature(ctx.rootFS);
    ctx.cmd.setPipeline(pso);
    PostCB pcb{{1, 0, ctx.intensity, blurDir},
               {1.f / float(ctx.width), 1.f / float(ctx.height), 0, 0},
               {srcAo, 0, bindlessOf(ctx.depthColor), bindlessOf(ctx.normal)},
               {ctx.radius, ctx.thickness, ctx.power, 0}};
    const uint64_t off = pushUploadCB(ctx.postCB, ctx.cbBump, &pcb, sizeof(pcb));
    ctx.cmd.setGraphicsRootCBV(1, ctx.postCB, off);
    ctx.cmd.setGraphicsRootSrvTable(3, 0);
    ctx.cmd.setGraphicsRootSamplerTable(4, sampTable);
    ctx.cmd.draw(3);
    ctx.cmd.transition(dest, rhi::ResourceState::ShaderResource);
  };

  drawGtao(ctx.gtaoPSO, ctx.ao, 0, 0.f);
  drawGtao(ctx.blurPSO, ctx.aoTemp, bindlessOf(ctx.ao), 1.f);   // horizontal
  drawGtao(ctx.blurPSO, ctx.ao, bindlessOf(ctx.aoTemp), 0.f); // vertical → final
}

} // namespace tucano
