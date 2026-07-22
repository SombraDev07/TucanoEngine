#include "Renderer/Weather/CloudSystem.h"
#include "Renderer/Weather/CloudNoise.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <span>
#include <string>

#ifndef TUCANO_SHADER_DIR
#define TUCANO_SHADER_DIR "shaders"
#endif

namespace tucano {
namespace {

std::string shaderPath(const char* file) { return std::string(TUCANO_SHADER_DIR) + "/" + file; }

struct CloudCBData {
  glm::mat4 invViewProj;
  glm::mat4 viewProj;
  glm::mat4 prevViewProj;
  glm::vec4 cameraPos;
  glm::vec4 screenSize;
  glm::vec4 sunDirIntensity;
  glm::vec4 sunColor;
  glm::vec4 cloudShape;
  glm::vec4 cloudAnim;
  glm::vec4 cloudQuality;
  glm::uvec4 texIds0;
  glm::uvec4 texIds1;
  glm::vec4 ambientSky;
};
static_assert(sizeof(CloudCBData) <= 512, "CloudCB must fit 512-aligned ring slot");

rhi::DX12Sampler& dxSamp(rhi::Sampler& s) { return static_cast<rhi::DX12Sampler&>(s); }

uint32_t bindlessOf(rhi::Texture& t) { return t.bindlessIndex(); }

} // namespace

void CloudSystem::init(rhi::Device& device) {
  m_rootFS = device.createRootSignature(false);
  auto load = [](const char* file) { return rhi::ShaderBytecode::loadFromFile(shaderPath(file)); };
  auto makeFS = [&](const char* ps, rhi::Format fmt) {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("Clouds_VSMain.cso");
    d.ps = load(ps);
    d.rtvFormats = {fmt};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    d.blend = rhi::BlendMode::Opaque;
    return device.createGraphicsPipeline(d);
  };

  m_marchPSO = makeFS("Clouds_PSCloudMarch.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_temporalPSO = makeFS("Clouds_PSCloudTemporal.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_weatherPSO = makeFS("Clouds_PSWeatherMap.cso", rhi::Format::R8G8B8A8_UNORM);
  m_compositePSO = makeFS("Clouds_PSCloudComposite.cso", rhi::Format::R16G16B16A16_FLOAT);

  {
    rhi::TextureDesc d{};
    d.width = d.height = kWeatherSize;
    d.format = rhi::Format::R8G8B8A8_UNORM;
    d.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
    d.debugName = "CloudWeatherA";
    m_weatherA = device.createTexture(d, nullptr, 0);
    d.debugName = "CloudWeatherB";
    m_weatherB = device.createTexture(d, nullptr, 0);
  }
  {
    // Precomputed tileable 3D noise (Nubis/Hillaire): Perlin-Worley base + Worley detail.
    const auto base = cloudnoise::bakeBase("cache");
    rhi::TextureDesc d{};
    d.width = d.height = cloudnoise::kBaseSize;
    d.depth = cloudnoise::kBaseSize;
    d.format = rhi::Format::R8G8B8A8_UNORM;
    d.usage = rhi::TextureUsage::ShaderResource;
    d.debugName = "CloudNoiseBase3D";
    m_noiseBase = device.createTexture(d, base.data(), cloudnoise::kBaseSize * 4);

    const auto detail = cloudnoise::bakeDetail("cache");
    d.width = d.height = cloudnoise::kDetailSize;
    d.depth = cloudnoise::kDetailSize;
    d.debugName = "CloudNoiseDetail3D";
    m_noiseDetail = device.createTexture(d, detail.data(), cloudnoise::kDetailSize * 4);
  }
  m_ready = true;
  std::cout << "[Clouds] volumetric system ready (3D Perlin-Worley + half-res temporal + weather map)\n";
}

void CloudSystem::resize(rhi::Device& device, uint32_t width, uint32_t height) {
  m_halfW = std::max(1u, width / 2);
  m_halfH = std::max(1u, height / 2);
  const rhi::TextureUsage rtSrv = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  auto make = [&](uint32_t w, uint32_t h, rhi::Format fmt, const char* name) {
    rhi::TextureDesc d{};
    d.width = w;
    d.height = h;
    d.format = fmt;
    d.usage = rtSrv;
    d.debugName = name;
    return device.createTexture(d, nullptr, 0);
  };
  m_cloudHalf = make(m_halfW, m_halfH, rhi::Format::R16G16B16A16_FLOAT, "CloudHalf");
  m_cloudHalfTemp = make(m_halfW, m_halfH, rhi::Format::R16G16B16A16_FLOAT, "CloudHalfTemp");
  m_cloudHistory = make(m_halfW, m_halfH, rhi::Format::R16G16B16A16_FLOAT, "CloudHistory");
}

float CloudSystem::weatherRainScale() const {
  if (!m_params.enabled || !m_params.driveRain) {
    return 1.0f;
  }
  const float c = std::clamp(m_params.coverage, 0.0f, 1.0f);
  const float s = std::clamp(m_params.storminess, 0.0f, 1.0f);
  return 0.35f + c * (0.9f + s * 0.9f);
}

void CloudSystem::updateCB(rhi::Buffer& cloudCB, uint64_t& bump, const glm::mat4& invViewProj,
                           const glm::mat4& viewProj, const glm::mat4& prevViewProj, const glm::vec3& cameraPos,
                           const glm::vec4& sunDirIntensity, const glm::vec3& sunColor,
                           const glm::vec3& ambientSky, float timeSeconds, uint32_t width, uint32_t height,
                           uint32_t depthId, uint32_t hdrId, uint32_t histId, uint32_t weatherPrevId,
                           uint32_t weatherCurrId, uint32_t cloudBufId) {
  CloudCBData cb{};
  cb.invViewProj = invViewProj;
  cb.viewProj = viewProj;
  cb.prevViewProj = prevViewProj;
  cb.cameraPos = glm::vec4(cameraPos, float(m_frame % 4096));
  cb.screenSize = {float(width), float(height), 1.0f / float(std::max(1u, width)),
                   1.0f / float(std::max(1u, height))};
  cb.sunDirIntensity = sunDirIntensity;
  cb.sunColor = glm::vec4(sunColor, 1.0f);
  cb.cloudShape = {m_params.coverage, m_params.density, m_params.altitude, m_params.thickness};
  cb.cloudAnim = {timeSeconds, m_params.wind.x, m_params.wind.z, m_params.storminess};
  uint32_t flags = 0;
  if (m_params.enabled) {
    flags |= 1u;
  }
  if (m_params.enableShadows) {
    flags |= 2u;
  }
  if (m_params.enableGodRays) {
    flags |= 4u;
  }
  if (m_params.enableWeatherMap) {
    flags |= 8u;
  }
  cb.cloudQuality = {m_params.shadowStrength, m_params.temporalAlpha, m_params.godRayStrength, float(flags)};
  cb.texIds0 = {depthId, hdrId, histId, weatherPrevId};
  cb.texIds1 = {weatherCurrId, cloudBufId, m_noiseBase ? m_noiseBase->bindlessIndex() : 0,
                m_noiseDetail ? m_noiseDetail->bindlessIndex() : 0};
  cb.ambientSky = glm::vec4(ambientSky, 1.0f);

  constexpr uint64_t kAlign = 512;
  if (bump + kAlign > cloudCB.size()) {
    bump = 0;
  }
  std::memcpy(static_cast<char*>(cloudCB.mapped()) + bump, &cb, sizeof(cb));
}

rhi::Texture* CloudSystem::execute(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& hdrIn,
                                   rhi::Texture& hdrTemp, rhi::Texture& depthColor, rhi::Buffer& cloudCB,
                                   rhi::Sampler& linearSamp, const glm::mat4& invViewProj,
                                   const glm::mat4& viewProj, const glm::mat4& prevViewProj,
                                   const glm::vec3& cameraPos, const glm::vec4& sunDirIntensity,
                                   const glm::vec3& sunColor, const glm::vec3& ambientSky, float timeSeconds,
                                   uint32_t width, uint32_t height, bool hasPrevCamera) {
  if (!m_ready || !m_params.enabled || !m_cloudHalf || m_params.coverage < 0.02f) {
    return nullptr;
  }

  auto& dx = static_cast<rhi::DX12Device&>(device);
  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {dxSamp(linearSamp).cpu};
  const uint32_t sampTable = dx.writeSamplerTable(sampCpu, 1);

  rhi::Texture* weatherPrev = m_weatherFlip ? m_weatherB.get() : m_weatherA.get();
  rhi::Texture* weatherCurr = m_weatherFlip ? m_weatherA.get() : m_weatherB.get();
  const glm::mat4 prevVP = hasPrevCamera ? prevViewProj : viewProj;

  const rhi::Viewport halfVp{0, 0, float(m_halfW), float(m_halfH), 0, 1};
  const rhi::Scissor halfSc{0, 0, static_cast<int>(m_halfW), static_cast<int>(m_halfH)};
  const rhi::Viewport fullVp{0, 0, float(width), float(height), 0, 1};
  const rhi::Scissor fullSc{0, 0, static_cast<int>(width), static_cast<int>(height)};
  const rhi::Viewport weatherVp{0, 0, float(kWeatherSize), float(kWeatherSize), 0, 1};
  const rhi::Scissor weatherSc{0, 0, static_cast<int>(kWeatherSize), static_cast<int>(kWeatherSize)};

  uint64_t bump = 0;
  auto drawPass = [&](rhi::PipelineState& pso, rhi::Texture& rt, const rhi::Viewport& vp, const rhi::Scissor& sc,
                      uint32_t hdrId, uint32_t histId, uint32_t cloudBufId) {
    updateCB(cloudCB, bump, invViewProj, viewProj, prevVP, cameraPos, sunDirIntensity, sunColor, ambientSky,
             timeSeconds, width, height, bindlessOf(depthColor), hdrId, histId, bindlessOf(*weatherPrev),
             bindlessOf(*weatherCurr), cloudBufId);
    const uint64_t off = bump;
    bump += 512;
    rhi::Texture* rtPtr = &rt;
    cmd.transition(rt, rhi::ResourceState::RenderTarget);
    cmd.setRenderTargets(std::span<rhi::Texture*>(&rtPtr, 1), nullptr);
    cmd.setViewport(vp);
    cmd.setScissor(sc);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(pso);
    cmd.setGraphicsRootCBV(1, cloudCB, off);
    cmd.setGraphicsRootSrvTable(3, 0);
    cmd.setGraphicsRootSamplerTable(4, sampTable);
    cmd.draw(3);
    cmd.transition(rt, rhi::ResourceState::ShaderResource);
  };

  cmd.transition(depthColor, rhi::ResourceState::ShaderResource);
  cmd.transition(hdrIn, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_noiseBase, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_noiseDetail, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_cloudHistory, rhi::ResourceState::ShaderResource);
  cmd.transition(*weatherPrev, rhi::ResourceState::ShaderResource);

  if (m_params.enableWeatherMap) {
    drawPass(*m_weatherPSO, *weatherCurr, weatherVp, weatherSc, bindlessOf(hdrIn), bindlessOf(*m_cloudHistory),
             bindlessOf(*m_cloudHalf));
    m_lastWeather = weatherCurr;
  }

  drawPass(*m_marchPSO, *m_cloudHalfTemp, halfVp, halfSc, bindlessOf(hdrIn), bindlessOf(*m_cloudHistory),
           bindlessOf(*m_cloudHalfTemp));

  drawPass(*m_temporalPSO, *m_cloudHalf, halfVp, halfSc, bindlessOf(hdrIn), bindlessOf(*m_cloudHistory),
           bindlessOf(*m_cloudHalfTemp));

  drawPass(*m_compositePSO, hdrTemp, fullVp, fullSc, bindlessOf(hdrIn), bindlessOf(*m_cloudHistory),
           bindlessOf(*m_cloudHalf));

  std::swap(m_cloudHalf, m_cloudHistory);
  m_weatherFlip = !m_weatherFlip;
  ++m_frame;
  return &hdrTemp;
}

} // namespace tucano
