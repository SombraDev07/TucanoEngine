#include "Renderer/Deferred/LightingPass.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <cstring>
#include <span>

namespace tucano {
namespace {

::tucano::rhi::DX12Sampler& asDxS(rhi::Sampler& s) { return static_cast<::tucano::rhi::DX12Sampler&>(s); }
uint32_t bindlessOf(rhi::Texture& t) { return t.bindlessIndex(); }

void updateCB(rhi::Buffer& buffer, const void* data, size_t size) {
  std::memcpy(buffer.mapped(), data, size);
}

struct LightingCB {
  glm::mat4 invViewProj;
  glm::vec4 cameraPos;
  glm::vec4 sunDirectionIntensity;
  glm::vec4 sunColor;
  glm::vec4 ambientColor;
  glm::mat4 lightViewProj[4];
  glm::vec4 cascadeSplits;
  glm::vec4 flags;
  glm::vec4 screenSize;
  glm::vec4 iblParams;
  glm::vec4 shadowParams;
  glm::uvec4 texIds0;
  glm::uvec4 texIds1;
  glm::uvec4 texIds2;
  glm::uvec4 texIds3;
  glm::vec4 vsmMeta;
  glm::vec4 atmParams;
  glm::vec4 brunetonParams;
  glm::uvec4 brunetonTexIds;
};

} // namespace

void executeLightingPass(LightingPassContext& ctx) {
  auto& dx = static_cast<rhi::DX12Device&>(ctx.device);

  LightingCB lcb{};
  lcb.invViewProj = ctx.invViewProj;
  lcb.cameraPos = ctx.cameraPos;
  lcb.sunDirectionIntensity = ctx.sunDirectionIntensity;
  lcb.sunColor = ctx.sunColor;
  lcb.ambientColor = ctx.ambientColor;
  for (int i = 0; i < 4; ++i) {
    lcb.lightViewProj[i] = ctx.lightViewProj[i];
  }
  lcb.cascadeSplits = ctx.cascadeSplits;
  lcb.flags = ctx.flags;
  lcb.screenSize = ctx.screenSize;
  lcb.iblParams = glm::vec4(ctx.iblMaxMip, ctx.iblExposure, 0.0f, 0.0f);
  lcb.shadowParams = ctx.shadowParams;
  lcb.texIds0 = {bindlessOf(ctx.albedo), bindlessOf(ctx.normal), bindlessOf(ctx.orm), bindlessOf(ctx.emissive)};
  lcb.texIds1 = {bindlessOf(ctx.depthColor), bindlessOf(ctx.shadowMap), bindlessOf(ctx.brdfLUT),
                 bindlessOf(ctx.irradiance)};
  const uint32_t octaIdx = ctx.octaAtlas ? bindlessOf(*ctx.octaAtlas) : 0u;
  lcb.texIds2 = {bindlessOf(ctx.prefiltered), bindlessOf(ctx.ao), octaIdx, 0};
  const uint32_t vsmPhys = ctx.vsmPhysical ? bindlessOf(*ctx.vsmPhysical) : 0u;
  const uint32_t vsmTable = ctx.vsmPageTable ? bindlessOf(*ctx.vsmPageTable) : 0u;
  const uint32_t rtMask = ctx.rtShadowMask ? bindlessOf(*ctx.rtShadowMask) : 0u;
  lcb.texIds3 = {vsmPhys, vsmTable, rtMask, 0};
  lcb.vsmMeta = ctx.vsmMeta;
  lcb.atmParams = ctx.atmParams;
  lcb.brunetonParams = ctx.brunetonParams;
  lcb.brunetonTexIds = ctx.brunetonTexIds;
  if (ctx.brunetonTransmittance) {
    lcb.brunetonTexIds.x = bindlessOf(*ctx.brunetonTransmittance);
  }
  if (ctx.brunetonScattering) {
    lcb.brunetonTexIds.y = bindlessOf(*ctx.brunetonScattering);
  }
  if (ctx.brunetonIrradiance) {
    lcb.brunetonTexIds.z = bindlessOf(*ctx.brunetonIrradiance);
  }
  updateCB(ctx.frameCB, &lcb, sizeof(lcb));

  ctx.cmd.transition(ctx.hdr, rhi::ResourceState::RenderTarget);
  rhi::Texture* hdrRT = &ctx.hdr;
  ctx.cmd.setRenderTargets(std::span<rhi::Texture*>(&hdrRT, 1), nullptr);
  const float hdrClear[4] = {0, 0, 0, 1};
  ctx.cmd.clearRenderTarget(ctx.hdr, hdrClear);
  ctx.cmd.setViewport(ctx.viewport);
  ctx.cmd.setScissor(ctx.scissor);
  ctx.cmd.setRootSignature(ctx.rootFS);
  ctx.cmd.setPipeline(ctx.lightingPSO);
  ctx.cmd.setGraphicsRootCBV(1, ctx.frameCB);
  ctx.cmd.setGraphicsRootCBV(2, ctx.lightCB);

  D3D12_CPU_DESCRIPTOR_HANDLE lightSamp[] = {asDxS(ctx.linearSamp).cpu, asDxS(ctx.shadowSamp).cpu};
  ctx.cmd.setGraphicsRootSrvTable(3, 0);
  ctx.cmd.setGraphicsRootSamplerTable(4, dx.writeSamplerTable(lightSamp, 2));
  ctx.cmd.draw(3);
  if (ctx.drawCalls) {
    ++(*ctx.drawCalls);
  }
  ctx.cmd.transition(ctx.hdr, rhi::ResourceState::ShaderResource);
}

} // namespace tucano
