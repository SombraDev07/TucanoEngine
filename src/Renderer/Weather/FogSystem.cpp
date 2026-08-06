#include "Renderer/Weather/FogSystem.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#ifndef TUCANO_SHADER_DIR
#define TUCANO_SHADER_DIR "Shaders"
#endif

namespace tucano {
namespace {

std::string shaderPath(const char* file) { return std::string(TUCANO_SHADER_DIR) + "/" + file; }

/// Froxel grid. 160x90 keeps the aspect close to 16:9 so froxels stay roughly square on screen,
/// and 64 slices is the usual point where the depth banding stops being visible once the volume
/// is temporally jittered.
constexpr uint32_t kFroxelsX = 160;
constexpr uint32_t kFroxelsY = 90;
constexpr uint32_t kFroxelsZ = 64;

struct FogCBData {
  glm::mat4 invViewProj;
  glm::mat4 prevViewProj;
  glm::vec4 cameraPos;             // xyz, time
  glm::vec4 volumeSize;            // x,y,z counts, w depthPower
  glm::vec4 fogMedium;             // density, baseHeight, heightFalloff, albedo
  glm::vec4 fogPhase;              // anisotropy, maxDistance, temporalBlend, shadowStrength
  glm::vec4 fogLight;              // sunIntensity, ambientIntensity, noiseStrength, noiseScale
  glm::vec4 fogWind;               // xyz wind*speed, w jitter
  glm::vec4 scatterColor;
  glm::vec4 sunDirectionIntensity;
  glm::vec4 sunColor;
  glm::vec4 ambientColor;
  glm::mat4 lightViewProj[4];
  glm::vec4 cascadeSplits;
  glm::uvec4 fogTexIds;            // shadowCSM, _, _, _
};

/// Van der Corput radical inverse, base 2. A low-discrepancy sequence spreads the per-frame slice
/// jitter evenly instead of clustering the way a random offset would.
float halton2(uint32_t i) {
  float f = 1.0f, r = 0.0f;
  uint32_t n = i + 1;
  while (n > 0) {
    f *= 0.5f;
    r += f * float(n & 1u);
    n >>= 1;
  }
  return r;
}

} // namespace

void FogSystem::init(rhi::Device& device, std::shared_ptr<rhi::RootSignature> computeRootSig) {
  m_rootCS = std::move(computeRootSig);
  if (!m_rootCS) return;

  try {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = rhi::ShaderBytecode::loadFromFile(shaderPath("VolumetricFog_CSInject.cso"));
    m_injectPSO = device.createComputePipeline(d);

    rhi::ComputePipelineDesc d2{};
    d2.rootSignature = m_rootCS;
    d2.cs = rhi::ShaderBytecode::loadFromFile(shaderPath("VolumetricFog_CSIntegrate.cso"));
    m_integratePSO = device.createComputePipeline(d2);
  } catch (...) {
    // Missing or stale .cso: stay off and let the analytic height fog carry the frame rather
    // than taking the renderer down.
    m_injectPSO.reset();
    m_integratePSO.reset();
    return;
  }
  if (!m_injectPSO || !m_integratePSO) return;

  rhi::BufferDesc cb{};
  cb.size = sizeof(FogCBData);
  cb.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
  cb.debugName = "FogCB";
  m_cb = device.createBuffer(cb, nullptr);

  createVolumes(device);
  m_ready = m_scatter[0] && m_scatter[1] && m_integrated && m_cb;
  std::cout << "[Fog] volumetric " << (m_ready ? "ready" : "unavailable") << " (" << kFroxelsX << "x"
            << kFroxelsY << "x" << kFroxelsZ << " froxels)\n";
}

void FogSystem::createVolumes(rhi::Device& device) {
  m_dimX = kFroxelsX;
  m_dimY = kFroxelsY;
  m_dimZ = kFroxelsZ;

  auto make = [&](const char* name) {
    rhi::TextureDesc d{};
    d.width = m_dimX;
    d.height = m_dimY;
    d.depth = m_dimZ;
    d.format = rhi::Format::R16G16B16A16_FLOAT;
    d.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
    d.debugName = name;
    return device.createTexture(d, nullptr, 0);
  };
  m_scatter[0] = make("FogScatterA");
  m_scatter[1] = make("FogScatterB");
  m_integrated = make("FogIntegrated");
}

void FogSystem::resize(rhi::Device&, uint32_t, uint32_t) {
  // The grid is fixed and camera-fitted, so a window resize changes nothing about it. Kept so
  // the renderer can call it uniformly with the other weather systems.
}

uint32_t FogSystem::integratedBindless() const {
  if (!active() || !m_integrated) return 0;
  return m_integrated->bindlessIndex();
}

void FogSystem::execute(rhi::Device& device, rhi::CommandList& cmd, const FrameContext& ctx) {
  if (!active()) return;

  auto& dx = static_cast<rhi::DX12Device&>(device);
  auto asTex = [](rhi::Texture& t) -> rhi::DX12Texture& { return static_cast<rhi::DX12Texture&>(t); };

  const uint32_t writeIdx = m_writeIndex;
  const uint32_t historyIdx = 1u - m_writeIndex;
  rhi::Texture& scatter = *m_scatter[writeIdx];
  rhi::Texture& history = *m_scatter[historyIdx];

  FogCBData cb{};
  cb.invViewProj = ctx.invViewProj;
  cb.prevViewProj = ctx.prevViewProj;
  cb.cameraPos = glm::vec4(ctx.cameraPos, ctx.timeSeconds);
  cb.volumeSize = glm::vec4(float(m_dimX), float(m_dimY), float(m_dimZ),
                            std::max(m_params.depthPower, 1.0f));
  cb.fogMedium = glm::vec4(std::max(m_params.density, 0.0f), m_params.baseHeight,
                           std::max(m_params.heightFalloff, 0.1f),
                           std::clamp(m_params.albedo, 0.0f, 1.0f));
  cb.fogPhase = glm::vec4(std::clamp(m_params.anisotropy, -0.95f, 0.95f),
                          std::max(m_params.maxDistance, 1.0f),
                          std::clamp(m_params.temporalBlend, 0.0f, 0.98f),
                          std::clamp(m_params.shadowStrength, 0.0f, 1.0f));
  cb.fogLight = glm::vec4(std::max(m_params.sunIntensity, 0.0f),
                          std::max(m_params.ambientIntensity, 0.0f),
                          std::clamp(m_params.noiseStrength, 0.0f, 1.0f),
                          std::max(m_params.noiseScale, 1.0f));
  cb.fogWind = glm::vec4(ctx.wind * m_params.noiseSpeed, halton2(ctx.frameIndex % 64u));
  cb.scatterColor = glm::vec4(m_params.scatteringColor, 0.0f);
  cb.sunDirectionIntensity = glm::vec4(ctx.sunDir, ctx.sunIntensity);
  cb.sunColor = glm::vec4(ctx.sunColor, 0.0f);
  cb.ambientColor = glm::vec4(ctx.ambientColor, 0.0f);
  for (int i = 0; i < 4; ++i) cb.lightViewProj[i] = ctx.lightViewProj[i];
  cb.cascadeSplits = ctx.cascadeSplits;
  cb.fogTexIds = glm::uvec4(ctx.shadowBindless, 0, 0, 0);

  if (void* mapped = m_cb->mapped()) {
    std::memcpy(mapped, &cb, sizeof(cb));
  }

  cmd.setRootSignature(*m_rootCS);
  cmd.setDescriptorHeap();
  cmd.setComputeRootCBV(1, *m_cb);

  cmd.transition(scatter, rhi::ResourceState::UnorderedAccess);
  cmd.transition(history, rhi::ResourceState::UnorderedAccess);
  cmd.transition(*m_integrated, rhi::ResourceState::UnorderedAccess);

  D3D12_CPU_DESCRIPTOR_HANDLE uavs[3]{};
  uavs[0] = asTex(scatter).uavCpu;
  uavs[1] = asTex(history).uavCpu;
  uavs[2] = asTex(*m_integrated).uavCpu;
  cmd.setComputeRootUavTable(3, dx.writeUavTable(uavs, 3));

  // The shadow map is read through the bindless heap, so the SRV table just needs the heap base.
  cmd.setComputeRootSrvTable(2, 0);

  cmd.setPipeline(*m_injectPSO);
  cmd.dispatch((m_dimX + 7) / 8, (m_dimY + 7) / 8, (m_dimZ + 3) / 4);

  // Integration reads every slice the injection wrote.
  cmd.uavBarrier(&scatter);

  cmd.setPipeline(*m_integratePSO);
  cmd.dispatch((m_dimX + 7) / 8, (m_dimY + 7) / 8, 1);

  // The lighting pass samples it as an SRV.
  cmd.transition(*m_integrated, rhi::ResourceState::ShaderResource);

  m_writeIndex = historyIdx;
}

} // namespace tucano
