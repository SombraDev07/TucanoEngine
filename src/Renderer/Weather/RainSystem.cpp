#include "Renderer/Weather/RainSystem.h"
#include "AssetPipeline/DdsLoader.h"
#include "AssetPipeline/ImageLoader.h"
#include "Platform/FileSystem.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"
#include "Renderer/Mesh.h"

#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <vector>

#ifndef TUCANO_ENGINE_ASSETS_DIR
#define TUCANO_ENGINE_ASSETS_DIR "EngineAssets"
#endif

namespace tucano {
namespace {

std::string shaderPath(const char* file) { return std::string(TUCANO_SHADER_DIR) + "/" + file; }

std::string assetPath(const char* rel) {
  return joinPath(TUCANO_ENGINE_ASSETS_DIR, rel);
}

struct RainCBData {
  glm::mat4 invViewProj;
  glm::mat4 viewProj;
  glm::mat4 viewMtx;
  glm::vec4 cameraPos;
  glm::vec4 screenSize;
  glm::vec4 rainAmount;
  glm::vec4 rainWet;
  glm::vec4 rainAnim;
  glm::vec4 rainColor;
  glm::vec4 rainVolume;
  glm::vec4 sceneRain0;
  glm::vec4 sceneRain1;
};
static_assert(sizeof(RainCBData) <= 512, "RainCB too large");

rhi::DX12Texture& dxTex(rhi::Texture& t) { return static_cast<rhi::DX12Texture&>(t); }
rhi::DX12Sampler& dxSamp(rhi::Sampler& s) { return static_cast<rhi::DX12Sampler&>(s); }

} // namespace

void RainSystem::loadTextures(rhi::Device& device) {
  auto tryLoad = [&](const char* rel, bool srgb) -> std::shared_ptr<Texture> {
    const std::string path = assetPath(rel);
    try {
      return Texture::loadFromFile(device, path, srgb);
    } catch (const std::exception& e) {
      std::cerr << "[Rain] missing texture " << path << " (" << e.what() << ")\n";
      return nullptr;
    }
  };

  m_puddleMask = tryLoad("Textures/Rain/puddle_mask.dds", false);
  m_rainSpatter = tryLoad("Textures/Rain/rain_spatter.dds", false);
  m_surfaceFlow = tryLoad("Textures/Rain/surface_flow_ddn.dds", false);
  m_rainfall = tryLoad("Textures/Rain/rainfall.dds", false);
  m_rainfallN = tryLoad("Textures/Rain/rainfall_ddn.dds", false);
  m_moisture = tryLoad("Textures/water_droplets.dds", false);

  // Fallback 1x1 white if any missing so PSO still runs
  auto ensure = [&](std::shared_ptr<Texture>& t, const char* name) {
    if (t)
      return;
    rhi::TextureDesc d{};
    d.width = d.height = 1;
    d.format = rhi::Format::R8G8B8A8_UNORM;
    d.usage = rhi::TextureUsage::ShaderResource;
    d.debugName = name;
    const uint8_t white[4] = {255, 255, 255, 255};
    t = Texture::create(device, d, white, 4);
  };
  ensure(m_puddleMask, "RainPuddleFallback");
  ensure(m_rainSpatter, "RainSpatterFallback");
  ensure(m_surfaceFlow, "RainFlowFallback");
  ensure(m_rainfall, "RainfallFallback");
  ensure(m_rainfallN, "RainfallNFallback");
  ensure(m_moisture, "MoistureFallback");

  // 24-frame ripple array (DDS BC5)
  ImageData first = loadDdsRGBA8(assetPath("Textures/Rain/Ripple/ripple1_ddn.dds"));
  rhi::TextureDesc arr{};
  arr.width = first.width;
  arr.height = first.height;
  arr.arraySize = kRippleCount;
  arr.format = rhi::Format::R8G8B8A8_UNORM;
  arr.usage = rhi::TextureUsage::ShaderResource;
  arr.debugName = "RainRipples";
  m_ripples = device.createTexture(arr, nullptr, 0);
  for (int i = 0; i < kRippleCount; ++i) {
    const std::string rel = "Textures/Rain/Ripple/ripple" + std::to_string(i + 1) + "_ddn.dds";
    const std::string path = assetPath(rel.c_str());
    ImageData img = (i == 0) ? first : loadDdsRGBA8(path);
    device.uploadTexture(*m_ripples, img.pixels.data(), img.width, img.height, img.width * 4, 0,
                         static_cast<uint32_t>(i));
  }
}

void RainSystem::buildRainMesh(rhi::Device& device) {
  // Cry cone/cylinder triangle strip converted to triangle list (Y stored as Cry Z in position.z)
  const int nSlices = kSlices;
  const float step = 6.28318530718f / float(nSlices);
  std::vector<Vertex> verts;
  auto pushV = [&](float x, float y, float z) {
    Vertex v{};
    v.position = {x, y, z};
    v.normal = {0, 1, 0};
    v.uv = {0, 0};
    verts.push_back(v);
  };

  std::vector<uint32_t> indices;
  auto emitStripQuad = [&](uint32_t base, int slices) {
    for (int h = 0; h < slices; ++h) {
      uint32_t i0 = base + uint32_t(h * 2);
      uint32_t i1 = base + uint32_t(h * 2 + 1);
      uint32_t i2 = base + uint32_t(h * 2 + 2);
      uint32_t i3 = base + uint32_t(h * 2 + 3);
      indices.push_back(i0);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i3);
    }
  };

  // Top cone
  uint32_t baseTop = 0;
  for (int h = 0; h < nSlices + 1; ++h) {
    float x = std::cos(float(h) * step);
    float y = std::sin(float(h) * step);
    pushV(x * 0.01f, y * 0.01f, 1.0f);
    pushV(x, y, 0.33f);
  }
  emitStripQuad(baseTop, nSlices);

  // Cylinder
  uint32_t baseCyl = static_cast<uint32_t>(verts.size());
  for (int h = 0; h < nSlices + 1; ++h) {
    float x = std::cos(float(h) * step);
    float y = std::sin(float(h) * step);
    pushV(x, y, 0.33f);
    pushV(x, y, -0.33f);
  }
  emitStripQuad(baseCyl, nSlices);

  // Bottom cone
  uint32_t baseBot = static_cast<uint32_t>(verts.size());
  for (int h = 0; h < nSlices + 1; ++h) {
    float x = std::cos(float(h) * step);
    float y = std::sin(float(h) * step);
    pushV(x, y, -0.33f);
    pushV(x * 0.01f, y * 0.01f, -1.0f);
  }
  emitStripQuad(baseBot, nSlices);

  // Expand to non-indexed draw list for simple draw()
  std::vector<Vertex> expanded;
  expanded.reserve(indices.size());
  for (uint32_t idx : indices) {
    expanded.push_back(verts[idx]);
  }
  m_rainVertexCount = static_cast<uint32_t>(expanded.size());

  rhi::BufferDesc vb{};
  vb.size = expanded.size() * sizeof(Vertex);
  vb.usage = rhi::BufferUsage::Vertex;
  vb.debugName = "RainConeVB";
  m_rainVB = device.createBuffer(vb, expanded.data());
}

void RainSystem::init(rhi::Device& device) {
  m_rootFS = device.createRootSignature(false);
  m_rootMesh = device.createRootSignature(true);

  rhi::SamplerDesc wrap{};
  wrap.addressU = wrap.addressV = wrap.addressW = rhi::AddressMode::Wrap;
  wrap.filter = rhi::Filter::Linear;
  m_wrapSamp = device.createSampler(wrap);
  rhi::SamplerDesc point{};
  point.addressU = point.addressV = point.addressW = rhi::AddressMode::Clamp;
  point.filter = rhi::Filter::Point;
  m_pointSamp = device.createSampler(point);

  auto load = [](const char* file) { return rhi::ShaderBytecode::loadFromFile(shaderPath(file)); };

  auto makeFS = [&](const char* ps, const std::vector<rhi::Format>& fmts, rhi::BlendMode blend) {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("Rain_VSMain.cso");
    d.ps = load(ps);
    d.rtvFormats = fmts;
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    d.blend = blend;
    return device.createGraphicsPipeline(d);
  };

  m_occPSO = makeFS("Rain_PSOcclusion.cso", {rhi::Format::R8G8B8A8_UNORM}, rhi::BlendMode::Opaque);

  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("Rain_VSMain.cso");
    d.ps = load("Rain_PSDeferredGBuffer.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB, rhi::Format::R8G8B8A8_UNORM, rhi::Format::R8G8B8A8_UNORM};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_gbufferPSO = device.createGraphicsPipeline(d);
  }

  m_streakPSO = makeFS("Rain_PSStreaks.cso", {rhi::Format::R16G16B16A16_FLOAT}, rhi::BlendMode::Opaque);
  m_puddleSpecPSO = makeFS("Rain_PSPuddleSpec.cso", {rhi::Format::R16G16B16A16_FLOAT}, rhi::BlendMode::Opaque);
  m_mistPSO = makeFS("Rain_PSMist.cso", {rhi::Format::R16G16B16A16_FLOAT}, rhi::BlendMode::Opaque);
  m_dropsPSO = makeFS("Rain_PSDrops.cso", {rhi::Format::R16G16B16A16_FLOAT}, rhi::BlendMode::Opaque);

  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("Rain_VSMain.cso");
    d.ps = load("Rain_PSCopy.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_copySrgbPSO = device.createGraphicsPipeline(d);

    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM};
    m_copyUnormPSO = device.createGraphicsPipeline(d);
  }

  loadTextures(device);
  buildRainMesh(device);
  m_ready = true;
  std::cout << "[Rain] Cry-parity rain ready (GBuffer wet + SceneRain + drops)\n";
}

void RainSystem::resize(rhi::Device& device, uint32_t width, uint32_t height) {
  if (!m_ready || width == 0 || height == 0) {
    return;
  }
  const auto rtSrv = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  auto make = [&](rhi::Format fmt, const char* name) {
    rhi::TextureDesc d{};
    d.width = width;
    d.height = height;
    d.format = fmt;
    d.usage = rtSrv;
    d.debugName = name;
    return device.createTexture(d, nullptr, 0);
  };
  m_albedoCopy = make(rhi::Format::R8G8B8A8_UNORM_SRGB, "RainAlbedoCopy");
  m_normalCopy = make(rhi::Format::R8G8B8A8_UNORM, "RainNormalCopy");
  m_ormCopy = make(rhi::Format::R8G8B8A8_UNORM, "RainOrmCopy");
  m_occlusion = make(rhi::Format::R8G8B8A8_UNORM, "RainOcclusion");
}

void RainSystem::updateCB(rhi::Buffer& rainCB, const glm::mat4& invViewProj, const glm::mat4& viewProj,
                          const glm::mat4& view, const glm::vec3& cameraPos, float timeSeconds, uint32_t width,
                          uint32_t height, int /*layer*/, float amountScale) {
  RainCBData cb{};
  cb.invViewProj = invViewProj;
  cb.viewProj = viewProj;
  cb.viewMtx = view;
  cb.cameraPos = glm::vec4(cameraPos, timeSeconds);
  cb.screenSize = {float(width), float(height), 1.0f / float(width), 1.0f / float(height)};
  cb.rainAmount = {m_params.amount * amountScale, m_params.rainDropsAmount, m_params.rainDropsSpeed,
                   m_params.rainDropsLighting};
  cb.rainWet = {m_params.diffuseDarkening, m_params.puddlesAmount, m_params.glossBoost, m_params.splashesAmount};
  const float rippleFrame = std::fmod(timeSeconds * 12.0f, float(kRippleCount));
  const glm::vec2 wind = glm::clamp(glm::vec2(m_params.wind.x, m_params.wind.z), -1.0f, 1.0f);
  cb.rainAnim = {wind.x, wind.y, rippleFrame, m_params.mistAmount};
  cb.rainColor = {m_params.color.r, m_params.color.g, m_params.color.b, m_params.streakLayers};
  cb.rainVolume = {cameraPos.x, cameraPos.y, cameraPos.z, m_params.radius};
  cb.sceneRain0 = {m_params.streakSpeed, 1.0f, 0.0f, 1.0f};
  cb.sceneRain1 = {m_params.rainDropsLighting, m_params.streakIntensity, 0.0f, m_params.maxViewDist};
  std::memcpy(rainCB.mapped(), &cb, sizeof(cb));
}

void RainSystem::runOcclusion(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& depthColor,
                              rhi::Buffer& rainCB, uint32_t sampTable, uint32_t width, uint32_t height) {
  auto& dx = static_cast<rhi::DX12Device&>(device);
  cmd.transition(*m_occlusion, rhi::ResourceState::RenderTarget);
  cmd.transition(depthColor, rhi::ResourceState::ShaderResource);
  rhi::Texture* rt = m_occlusion.get();
  cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
  cmd.setRootSignature(*m_rootFS);
  cmd.setPipeline(*m_occPSO);
  cmd.setGraphicsRootCBV(1, rainCB);
  D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {dxTex(depthColor).srvCpu};
  cmd.setGraphicsRootSrvTable(3, dx.writeSrvTable(srv, 1));
  cmd.setGraphicsRootSamplerTable(4, sampTable);
  cmd.setViewport({0, 0, float(width), float(height), 0, 1});
  cmd.setScissor({0, 0, int(width), int(height)});
  cmd.draw(3);
  cmd.transition(*m_occlusion, rhi::ResourceState::ShaderResource);
}

void RainSystem::executeDeferredGBuffer(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& albedo,
                                        rhi::Texture& normal, rhi::Texture& orm, rhi::Texture& depthColor,
                                        rhi::Buffer& rainCB, rhi::Sampler& linearSamp,
                                        const glm::mat4& invViewProj, const glm::mat4& viewProj,
                                        const glm::mat4& view, const glm::vec3& cameraPos, float timeSeconds,
                                        uint32_t width, uint32_t height) {
  if (!m_ready || !m_params.enabled || m_params.amount <= 0.001f || !m_albedoCopy) {
    return;
  }

  auto& dx = static_cast<rhi::DX12Device&>(device);
  updateCB(rainCB, invViewProj, viewProj, view, cameraPos, timeSeconds, width, height, 0, 1.0f);

  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {dxSamp(linearSamp).cpu, dxSamp(*m_wrapSamp).cpu,
                                           dxSamp(*m_pointSamp).cpu};
  const uint32_t sampTable = dx.writeSamplerTable(sampCpu, 3);

  runOcclusion(cmd, device, depthColor, rainCB, sampTable, width, height);

  auto blit = [&](rhi::Texture& src, rhi::Texture& dst, rhi::PipelineState& pso) {
    cmd.transition(dst, rhi::ResourceState::RenderTarget);
    cmd.transition(src, rhi::ResourceState::ShaderResource);
    rhi::Texture* rt = &dst;
    cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(pso);
    cmd.setGraphicsRootCBV(1, rainCB);
    D3D12_CPU_DESCRIPTOR_HANDLE srv2[] = {dxTex(depthColor).srvCpu, dxTex(src).srvCpu};
    cmd.setGraphicsRootSrvTable(3, dx.writeSrvTable(srv2, 2));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
    cmd.draw(3);
    cmd.transition(dst, rhi::ResourceState::ShaderResource);
  };

  blit(albedo, *m_albedoCopy, *m_copySrgbPSO);
  blit(normal, *m_normalCopy, *m_copyUnormPSO);
  blit(orm, *m_ormCopy, *m_copyUnormPSO);

  cmd.transition(albedo, rhi::ResourceState::RenderTarget);
  cmd.transition(normal, rhi::ResourceState::RenderTarget);
  cmd.transition(orm, rhi::ResourceState::RenderTarget);
  cmd.transition(depthColor, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_occlusion, rhi::ResourceState::ShaderResource);

  rhi::Texture* rts[3] = {&albedo, &normal, &orm};
  cmd.setRenderTargets(std::span<rhi::Texture*>(rts, 3), nullptr);
  cmd.setRootSignature(*m_rootFS);
  cmd.setPipeline(*m_gbufferPSO);
  cmd.setGraphicsRootCBV(1, rainCB);

  D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
      dxTex(depthColor).srvCpu,
      dxTex(*m_albedoCopy).srvCpu,
      dxTex(*m_normalCopy).srvCpu,
      dxTex(*m_ormCopy).srvCpu,
      dxTex(m_puddleMask->resource()).srvCpu,
      dxTex(m_rainSpatter->resource()).srvCpu,
      dxTex(m_surfaceFlow->resource()).srvCpu,
      dxTex(*m_ripples).srvCpu,
      dxTex(*m_occlusion).srvCpu,
  };
  cmd.setGraphicsRootSrvTable(3, dx.writeSrvTable(srv, 9));
  cmd.setGraphicsRootSamplerTable(4, sampTable);
  cmd.setViewport({0, 0, float(width), float(height), 0, 1});
  cmd.setScissor({0, 0, int(width), int(height)});
  cmd.draw(3);

  cmd.transition(albedo, rhi::ResourceState::ShaderResource);
  cmd.transition(normal, rhi::ResourceState::ShaderResource);
  cmd.transition(orm, rhi::ResourceState::ShaderResource);
}

rhi::Texture* RainSystem::executePost(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& hdrIn,
                                      rhi::Texture& hdrTemp, rhi::Texture& depthColor, rhi::Texture& normals,
                                      rhi::Texture& bloomOrHdr, rhi::Buffer& rainCB, rhi::Sampler& linearSamp,
                                      const glm::mat4& invViewProj, const glm::mat4& viewProj,
                                      const glm::mat4& view, const glm::vec3& cameraPos, float timeSeconds,
                                      uint32_t width, uint32_t height) {
  if (!m_ready || !m_params.enabled || m_params.amount <= 0.001f) {
    return &hdrIn;
  }

  auto& dx = static_cast<rhi::DX12Device&>(device);
  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {dxSamp(linearSamp).cpu, dxSamp(*m_wrapSamp).cpu,
                                           dxSamp(*m_pointSamp).cpu};
  const uint32_t sampTable = dx.writeSamplerTable(sampCpu, 3);

  if (!m_occlusion) {
    updateCB(rainCB, invViewProj, viewProj, view, cameraPos, timeSeconds, width, height, 0, 1.0f);
    resize(device, width, height);
    runOcclusion(cmd, device, depthColor, rainCB, sampTable, width, height);
  }

  rhi::Texture* src = &hdrIn;
  rhi::Texture* dst = &hdrTemp;

  enum class PassKind { Mist, Streaks, PuddleSpec, Drops };
  auto fullscreen = [&](rhi::PipelineState& pso, rhi::Texture& inTex, rhi::Texture& outTex, PassKind kind) {
    updateCB(rainCB, invViewProj, viewProj, view, cameraPos, timeSeconds, width, height, 0, 1.0f);
    cmd.transition(outTex, rhi::ResourceState::RenderTarget);
    cmd.transition(inTex, rhi::ResourceState::ShaderResource);
    cmd.transition(depthColor, rhi::ResourceState::ShaderResource);
    cmd.transition(normals, rhi::ResourceState::ShaderResource);
    cmd.transition(*m_occlusion, rhi::ResourceState::ShaderResource);
    rhi::Texture* rt = &outTex;
    cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(pso);
    cmd.setGraphicsRootCBV(1, rainCB);
    D3D12_CPU_DESCRIPTOR_HANDLE srv[10] = {};
    srv[0] = dxTex(depthColor).srvCpu;
    srv[1] = dxTex(inTex).srvCpu;
    if (kind == PassKind::Streaks) {
      srv[2] = dxTex(m_rainfall->resource()).srvCpu;
      srv[3] = dxTex(m_rainfallN->resource()).srvCpu;
    } else if (kind == PassKind::PuddleSpec) {
      srv[2] = dxTex(normals).srvCpu;
      srv[3] = dxTex(normals).srvCpu;
    } else {
      srv[2] = dxTex(m_moisture->resource()).srvCpu;
      srv[3] = dxTex(m_moisture->resource()).srvCpu;
    }
    srv[4] = dxTex(m_puddleMask->resource()).srvCpu;
    srv[5] = dxTex(m_rainSpatter->resource()).srvCpu;
    srv[6] = dxTex(m_surfaceFlow->resource()).srvCpu;
    srv[7] = dxTex(*m_ripples).srvCpu;
    srv[8] = dxTex(*m_occlusion).srvCpu;
    srv[9] = dxTex(bloomOrHdr).srvCpu;
    cmd.setGraphicsRootSrvTable(3, dx.writeSrvTable(srv, 10));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
    cmd.setViewport({0, 0, float(width), float(height), 0, 1});
    cmd.setScissor({0, 0, int(width), int(height)});
    cmd.draw(3);
    cmd.transition(outTex, rhi::ResourceState::ShaderResource);
  };

  fullscreen(*m_puddleSpecPSO, *src, *dst, PassKind::PuddleSpec);
  std::swap(src, dst);
  fullscreen(*m_mistPSO, *src, *dst, PassKind::Mist);
  std::swap(src, dst);
  fullscreen(*m_streakPSO, *src, *dst, PassKind::Streaks);
  std::swap(src, dst);
  fullscreen(*m_dropsPSO, *src, *dst, PassKind::Drops);
  return dst;
}

} // namespace tucano
