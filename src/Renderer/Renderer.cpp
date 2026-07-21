#include "Renderer/Renderer.h"
#include "Platform/FileSystem.h"
#include "Renderer/GI/IBL.h"
#include "Renderer/Shadows/ToroidalShadows.h"
#include "Renderer/Weather/RainSystem.h"
#include "RHI/DX12/DX12Common.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace tucano {
namespace {

struct FrameCBData {
  glm::mat4 viewProj;
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 invViewProj;
  glm::vec4 cameraPos;
  glm::vec4 screenSize;
  glm::vec4 sunDirectionIntensity;
  glm::vec4 sunColor;
  glm::vec4 ambientColor;
  glm::mat4 lightViewProj[4];
  glm::vec4 cascadeSplits;
  glm::vec4 flags;
};

struct ObjectCBData {
  glm::mat4 worldInvTranspose;
  glm::vec4 baseColorFactor;
  glm::vec4 materialParams;
  glm::vec4 emissiveFactor;
  glm::uvec4 textureIndices;
  glm::vec4 materialExt;
};

struct RootXform {
  glm::mat4 viewProj;
  glm::mat4 world;
};

struct LightCBData {
  uint32_t lightCount;
  uint32_t pad[3];
  glm::vec4 lightPosType[16];
  glm::vec4 lightColorIntensity[16];
  glm::vec4 lightRangeParams[16];
  glm::vec4 lightDirection[16];
};

struct ShadowFrameCB {
  glm::mat4 lightViewProj;
};

std::string shaderPath(const char* name) { return joinPath(TUCANO_SHADER_DIR, name); }

void updateUploadCB(rhi::Buffer& buffer, const void* data, size_t size) {
  std::memcpy(buffer.mapped(), data, size);
}

::tucano::rhi::DX12Texture& asDxTex(rhi::Texture& t) { return static_cast<::tucano::rhi::DX12Texture&>(t); }
::tucano::rhi::DX12Sampler& asDxSamp(rhi::Sampler& s) { return static_cast<::tucano::rhi::DX12Sampler&>(s); }

glm::mat4 computeCascadeVP(const Camera& cam, const glm::vec3& lightDir, float nearD, float farD, float& outSplit) {
  outSplit = farD;
  const float aspect = 16.0f / 9.0f;
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), aspect, nearD, farD);
  const glm::mat4 view = cam.view();
  const glm::mat4 inv = glm::inverse(proj * view);

  glm::vec3 center(0);
  int count = 0;
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < 2; ++z) {
        glm::vec4 ndc(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 1.0f, 1.0f);
        glm::vec4 world = inv * ndc;
        world /= world.w;
        center += glm::vec3(world);
        ++count;
      }
    }
  }
  center /= static_cast<float>(count);
  const float radius = farD * 0.5f;
  const glm::vec3 lightPos = center - lightDir * radius * 2.0f;
  const glm::mat4 lightView = glm::lookAtLH(lightPos, center, glm::vec3(0, 1, 0));
  const glm::mat4 lightProj = glm::orthoLH_ZO(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
  return lightProj * lightView;
}

} // namespace

Renderer::Renderer(rhi::Device& device, uint32_t width, uint32_t height)
    : m_device(device), m_width(width), m_height(height) {
  createDefaultTextures();
  createTargets();
  createPipelines();
  createPhase3Pipelines();

  rhi::BufferDesc cb{};
  cb.size = 256 * 8;
  cb.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
  cb.debugName = "FrameCB";
  m_frameCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256ull * 4096ull;
  cb.debugName = "ObjectCB";
  m_objectCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256 * 8;
  cb.debugName = "LightCB";
  m_lightCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256;
  cb.debugName = "Phase3CB";
  m_phase3CB = m_device.createBuffer(cb, nullptr);
  cb.size = 512;
  cb.debugName = "RainCB";
  m_rainCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256 * 1024;
  cb.usage = rhi::BufferUsage::Upload | rhi::BufferUsage::Indirect;
  cb.debugName = "IndirectArgs";
  m_indirectArgs = m_device.createBuffer(cb, nullptr);

  m_rain.init(m_device);
  m_rain.resize(m_device, m_width, m_height);
  m_rain.params().enabled = false;

  if (!m_settings.enableRTReflections && !m_loggedNoDXR) {
    std::cout << "[Renderer] DXR reflections unavailable — using SSR + IBL (refl_low)\n";
    m_loggedNoDXR = true;
  }

  std::cout << "[Renderer] Building procedural IBL (BRDF LUT + irradiance + prefiltered)...\n";
  IBLTextures ibl = createProceduralIBL(m_device, 256, 128);
  m_brdfLUT = ibl.brdfLUT->shared();
  m_irradiance = ibl.irradiance->shared();
  m_prefiltered = ibl.prefiltered->shared();
  m_iblMaxMip = ibl.maxMip;
  m_iblExposure = 1.35f;
  std::cout << "[Renderer] IBL ready (maxMip=" << m_iblMaxMip << ")\n";
}

Renderer::~Renderer() { m_device.waitIdle(); }

void Renderer::createDefaultTextures() {
  const uint8_t white[4] = {255, 255, 255, 255};
  const uint8_t flatN[4] = {128, 128, 255, 255};
  const uint8_t orm[4] = {255, 255, 255, 255}; // ao=1, rough=1, metal=1 (factors multiply)
  const uint8_t black[4] = {0, 0, 0, 255};
  rhi::TextureDesc d{};
  d.width = d.height = 1;
  d.usage = rhi::TextureUsage::ShaderResource;
  d.format = rhi::Format::R8G8B8A8_UNORM;
  d.debugName = "DefaultAlbedo";
  m_defaultAlbedo = Texture::create(m_device, d, white, 4);
  d.debugName = "DefaultNormal";
  m_defaultNormal = Texture::create(m_device, d, flatN, 4);
  d.debugName = "DefaultORM";
  m_defaultORM = Texture::create(m_device, d, orm, 4);
  d.debugName = "DefaultBlack";
  m_defaultBlack = Texture::create(m_device, d, black, 4);

  rhi::SamplerDesc sd{};
  m_samplerLinear = m_device.createSampler(sd);
  sd.filter = rhi::Filter::Linear;
  sd.addressU = sd.addressV = sd.addressW = rhi::AddressMode::Clamp;
  m_samplerShadow = m_device.createSampler(sd);
}

void Renderer::createTargets() {
  auto makeRT = [&](rhi::Format fmt, rhi::TextureUsage usage, const char* name, uint32_t w = 0, uint32_t h = 0) {
    rhi::TextureDesc d{};
    d.width = w ? w : m_width;
    d.height = h ? h : m_height;
    d.format = fmt;
    d.usage = usage;
    d.debugName = name;
    return m_device.createTexture(d, nullptr, 0);
  };

  const auto rtSrv = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  m_albedo = makeRT(rhi::Format::R8G8B8A8_UNORM_SRGB, rtSrv, "GBufferAlbedo");
  m_normal = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferNormal");
  m_orm = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferORM");
  m_emissive = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferEmissive");
  m_depthColor = makeRT(rhi::Format::R32_FLOAT, rtSrv, "DepthColor");
  m_depth = makeRT(rhi::Format::D32_FLOAT, rhi::TextureUsage::DepthStencil, "MainDepth");
  m_hdr = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "HDR");
  m_hdrCompose = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "HDRCompose");
  m_ao = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "AO");
  m_shadowMap = makeRT(rhi::Format::R32_FLOAT, rtSrv, "ShadowMap", m_settings.shadowMapSize, m_settings.shadowMapSize);
  m_shadowScrollTemp =
      makeRT(rhi::Format::R32_FLOAT, rtSrv, "ShadowScrollTemp", m_settings.shadowMapSize / 2, m_settings.shadowMapSize / 2);
  {
    const float ends[4] = {5.f, 20.f, 60.f, 200.f};
    m_shadowAtlas.configure(m_settings.shadowMapSize, ends);
  }
  m_octaShadows.init(m_device, 2048, 512);
  m_ssgi = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "SSGI");
  m_ssr = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "SSR");
  m_visId = makeRT(rhi::Format::R32_UINT, rtSrv, "VisId");
  const auto uavSrv = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
  m_ddgiAtlas = makeRT(rhi::Format::R16G16B16A16_FLOAT, uavSrv, "DDGIAtlas", 64, 64);
  m_ddgiAtlasPrev = makeRT(rhi::Format::R16G16B16A16_FLOAT, uavSrv, "DDGIAtlasPrev", 64, 64);

  for (uint32_t i = 0; i < m_bloomMips.size(); ++i) {
    m_bloomMips[i] =
        makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "BloomMip", std::max(1u, m_width >> (i + 1)),
               std::max(1u, m_height >> (i + 1)));
  }
}

void Renderer::createPipelines() {
  m_root = m_device.createRootSignature(true);
  m_rootFS = m_device.createRootSignature(false);

  auto load = [](const char* file) { return rhi::ShaderBytecode::loadFromFile(shaderPath(file)); };

  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_root;
    d.vs = load("GBuffer_VSMain.cso");
    d.ps = load("GBuffer_PSMain.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB, rhi::Format::R8G8B8A8_UNORM, rhi::Format::R8G8B8A8_UNORM,
                    rhi::Format::R8G8B8A8_UNORM, rhi::Format::R32_FLOAT};
    d.dsvFormat = rhi::Format::D32_FLOAT;
    d.cullMode = rhi::CullMode::None;
    m_gbufferPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_root;
    d.vs = load("Shadow_VSMain.cso");
    d.ps = load("Shadow_PSMain.cso");
    d.rtvFormats = {rhi::Format::R32_FLOAT};
    d.dsvFormat = rhi::Format::Unknown;
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    m_shadowPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("DeferredLighting_VSMain.cso");
    d.ps = load("DeferredLighting_PSMain.cso");
    d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_lightingPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("PostFX_VSMain.cso");
    d.ps = load("PostFX_PSTonemap.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_tonemapPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("PostFX_VSMain.cso");
    d.ps = load("PostFX_PSBloomDown.cso");
    d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_bloomDownPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("PostFX_VSMain.cso");
    d.ps = load("PostFX_PSBloomUp.cso");
    d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_bloomUpPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("PostFX_VSMain.cso");
    d.ps = load("PostFX_PSAO.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_aoPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("ContactShadows_VSMain.cso");
    d.ps = load("ContactShadows_PSMain.cso");
    d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_contactShadowPSO = m_device.createGraphicsPipeline(d);
  }
}

void Renderer::createPhase3Pipelines() {
  m_rootCS = m_device.createComputeRootSignature();
  auto load = [](const char* file) { return rhi::ShaderBytecode::loadFromFile(shaderPath(file)); };

  auto makeFS = [&](const char* ps, rhi::Format fmt) {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("Phase3_VSMain.cso");
    d.ps = load(ps);
    d.rtvFormats = {fmt};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    return m_device.createGraphicsPipeline(d);
  };

  m_ssgiPSO = makeFS("Phase3_PSSSGI.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_ssrPSO = makeFS("Phase3_PSSSR.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_composePSO = makeFS("Phase3_PSCompose.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_ddgiSamplePSO = makeFS("Phase3_PSDDGI.cso", rhi::Format::R16G16B16A16_FLOAT);
  m_visAlbedoPSO = makeFS("Phase3_PSVisAlbedo.cso", rhi::Format::R8G8B8A8_UNORM_SRGB);
  m_visNormalPSO = makeFS("Phase3_PSVisNormal.cso", rhi::Format::R8G8B8A8_UNORM);
  m_visOrmPSO = makeFS("Phase3_PSVisORM.cso", rhi::Format::R8G8B8A8_UNORM);
  m_visEmissivePSO = makeFS("Phase3_PSVisEmissive.cso", rhi::Format::R8G8B8A8_UNORM);

  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_root;
    d.vs = load("VisBuffer_VSMain.cso");
    d.ps = load("VisBuffer_PSMain.cso");
    d.rtvFormats = {rhi::Format::R32_UINT};
    d.dsvFormat = rhi::Format::D32_FLOAT;
    d.cullMode = rhi::CullMode::None;
    m_visBufferPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("DDGIUpdate_CSMain.cso");
    m_ddgiUpdatePSO = m_device.createComputePipeline(d);
  }
}

void Renderer::resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || (width == m_width && height == m_height)) {
    return;
  }
  m_device.waitIdle();
  m_width = width;
  m_height = height;
  createTargets();
  m_rain.resize(m_device, m_width, m_height);
}

void Renderer::render(rhi::CommandList& cmd, rhi::Texture& swapChainRT, Scene& scene) {
  const auto t0 = std::chrono::steady_clock::now();
  m_drawCalls = 0;
  auto& dxDevice = static_cast<::tucano::rhi::DX12Device&>(m_device);

  m_graph.reset();
  const RGHandle hAlbedo = m_graph.importTexture("albedo", m_albedo.get());
  const RGHandle hNormal = m_graph.importTexture("normal", m_normal.get());
  const RGHandle hHdr = m_graph.importTexture("hdr", m_hdr.get());
  const RGHandle hShadow = m_graph.importTexture("shadow", m_shadowMap.get());
  const RGHandle hSsgi = m_graph.importTexture("ssgi", m_ssgi.get());
  const RGHandle hSsr = m_graph.importTexture("ssr", m_ssr.get());
  const RGHandle hCompose = m_graph.importTexture("hdrCompose", m_hdrCompose.get());
  m_graph.addPass(
      "Shadow",
      [&](RGPassBuilder& b) {
        b.write(hShadow, RGUsage::RenderTarget);
        b.enabled = m_settings.enableShadows;
      },
      {});
  m_graph.addPass(
      "GBuffer",
      [&](RGPassBuilder& b) {
        b.write(hAlbedo, RGUsage::RenderTarget);
        b.write(hNormal, RGUsage::RenderTarget);
      },
      {});
  m_graph.addPass(
      "Lighting",
      [&](RGPassBuilder& b) {
        b.read(hAlbedo);
        b.read(hNormal);
        b.read(hShadow);
        b.write(hHdr, RGUsage::RenderTarget);
      },
      {});
  m_graph.addPass(
      "SSGI",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.write(hSsgi, RGUsage::RenderTarget);
        b.enabled = m_settings.giTier != GITier::Off;
        b.asyncHint = true;
      },
      {});
  m_graph.addPass(
      "SSR",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.write(hSsr, RGUsage::RenderTarget);
        b.enabled = m_settings.enableSSR;
      },
      {});
  m_graph.addPass(
      "Compose",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hSsgi);
        b.read(hSsr);
        b.write(hCompose, RGUsage::RenderTarget);
      },
      {});
  RGTextureDesc tmpA{std::max(1u, m_width / 2), std::max(1u, m_height / 2), rhi::Format::R16G16B16A16_FLOAT,
                     rhi::TextureUsage::ShaderResource | rhi::TextureUsage::RenderTarget, "rg_tmp_a"};
  RGTextureDesc tmpB = tmpA;
  tmpB.name = "rg_tmp_b";
  m_graph.addPass(
      "AliasA",
      [&](RGPassBuilder& b) {
        RGHandle tA = b.create(tmpA);
        b.write(tA, RGUsage::RenderTarget);
      },
      {});
  m_graph.addPass(
      "AliasB",
      [&](RGPassBuilder& b) {
        RGHandle tB = b.create(tmpB);
        b.write(tB, RGUsage::RenderTarget);
      },
      {});
  m_graph.compile(m_device);
  static bool exportedRG = false;
  if (!exportedRG) {
    m_graph.exportGraphviz("c:/TucanoEngine/captures/render_graph.dot");
    exportedRG = true;
    std::cout << "[RenderGraph] aliasedBytes~" << m_graph.aliasedBytes() << "\n";
  }

  glm::vec3 sunDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
  glm::vec3 sunColor(1.0f, 0.96f, 0.90f);
  float sunIntensity = 4.0f;
  for (const auto& l : scene.lights) {
    if (l.type == LightType::Directional) {
      sunDir = glm::normalize(l.direction);
      sunColor = l.color;
      sunIntensity = l.intensity;
      break;
    }
  }

  FrameCBData frame{};
  // Stable camera for determinism; scene.camera still used for gameplay samples
  const glm::vec3 eye = scene.camera.position();
  const glm::vec3 target = eye + scene.camera.forward();
  frame.view = glm::lookAtLH(eye, target, glm::vec3(0, 1, 0));
  frame.proj = glm::perspectiveLH_ZO(glm::radians(60.0f), float(m_width) / float(std::max(1u, m_height)), 0.1f, 300.0f);
  frame.viewProj = frame.proj * frame.view;
  frame.invViewProj = glm::inverse(frame.viewProj);
  frame.cameraPos = glm::vec4(eye, 1.0f);
  frame.screenSize = {float(m_width), float(m_height), 1.0f / m_width, 1.0f / m_height};
  frame.sunDirectionIntensity = glm::vec4(sunDir, sunIntensity);
  frame.sunColor = glm::vec4(sunColor, 1.0f);
  frame.ambientColor = glm::vec4(0.03f, 0.04f, 0.06f, 1.0f);
  float splits[4]{};
  const float cascadeEnds[4] = {5.f, 20.f, 60.f, 200.f};
  if (m_settings.enableToroidalShadows) {
    m_shadowAtlas.update(scene.camera, sunDir);
    for (int i = 0; i < 4; ++i) {
      frame.lightViewProj[i] = m_shadowAtlas.viewProj(i, sunDir);
      splits[i] = m_shadowAtlas.split(i);
    }
  } else {
    float nearP = scene.camera.nearPlane();
    for (int i = 0; i < 4; ++i) {
      frame.lightViewProj[i] = computeCascadeVP(scene.camera, sunDir, nearP, cascadeEnds[i], splits[i]);
      nearP = cascadeEnds[i];
    }
  }
  frame.cascadeSplits = glm::vec4(splits[0], splits[1], splits[2], splits[3]);
  frame.flags = glm::vec4(m_settings.enableShadows ? 1.f : 0.f, m_settings.enableIBL ? 1.f : 0.f,
                          m_settings.enableAO ? 1.f : 0.f, 0.f);

  if (m_settings.enableVoxelGI) {
    m_voxelGI.update(m_device, scene, 0.25f);
    m_skyVis.update(scene, eye, 0.2f);
    m_skyOcclusionAvg = m_skyVis.sample(eye);
    // Modulate ambient by sky visibility so interiors don't get full outdoor IBL.
    frame.ambientColor *= glm::vec4(m_skyOcclusionAvg, m_skyOcclusionAvg, m_skyOcclusionAvg, 1.0f);
  }

  // Lighting FrameCB layout differs — pack carefully into same buffer with lighting-specific prefix later
  updateUploadCB(*m_frameCB, &frame, sizeof(frame));

  LightCBData lights{};
  lights.lightCount = 0;
  for (const auto& l : scene.lights) {
    if ((l.type != LightType::Point && l.type != LightType::Spot) || lights.lightCount >= 16) {
      continue;
    }
    const uint32_t i = lights.lightCount++;
    const float typeW = (l.type == LightType::Spot) ? 2.0f : 1.0f;
    lights.lightPosType[i] = glm::vec4(l.position, typeW);
    lights.lightColorIntensity[i] = glm::vec4(l.color, l.intensity);
    lights.lightRangeParams[i] = glm::vec4(l.range, l.innerCone, l.outerCone, 0.0f);
    lights.lightDirection[i] = glm::vec4(l.direction, 0.0f);
  }
  updateUploadCB(*m_lightCB, &lights, sizeof(lights));

  rhi::Viewport vp{0, 0, float(m_width), float(m_height), 0, 1};
  rhi::Scissor sc{0, 0, int(m_width), int(m_height)};
  cmd.setDescriptorHeap();

  uint32_t objectSlot = 0;
  auto pushObjectCB = [&](const ObjectCBData& ocb) -> uint64_t {
    const uint64_t offset = static_cast<uint64_t>(objectSlot++) * 256ull;
    std::memcpy(static_cast<uint8_t*>(m_objectCB->mapped()) + offset, &ocb, sizeof(ocb));
    return offset;
  };

  // ---- Shadow pass (2x2 cascade atlas + toroidal GPU scroll) ----
  if (m_settings.enableShadows) {
    bool anyDirty = true;
    if (m_settings.enableToroidalShadows) {
      anyDirty = false;
      for (int i = 0; i < 4; ++i) {
        anyDirty = anyDirty || m_shadowAtlas.isDirty(i);
      }
    }

    if (anyDirty) {
      const uint32_t tile = m_settings.shadowMapSize / 2;

      // GPU toroidal scroll: copy each dirty tile through a temp when camera scrolled.
      if (m_settings.enableToroidalShadows && m_shadowScrollTemp) {
        for (int cascade = 0; cascade < 4; ++cascade) {
          if (!m_shadowAtlas.isDirty(cascade)) {
            continue;
          }
          const auto& c = m_shadowAtlas.cascade(cascade);
          const int sx = c.lastScrollX;
          const int sy = c.lastScrollY;
          if (sx == 0 && sy == 0) {
            continue;
          }
          // Clamp scroll to tile; copy overlapping region via temp (no in-place overlap).
          const int absx = std::abs(sx);
          const int absy = std::abs(sy);
          if (absx >= int(tile) || absy >= int(tile)) {
            continue;
          }
          const uint32_t ox = (cascade % 2) * tile;
          const uint32_t oy = (cascade / 2) * tile;
          const uint32_t keepW = tile - uint32_t(absx);
          const uint32_t keepH = tile - uint32_t(absy);
          const uint32_t srcX = ox + (sx > 0 ? uint32_t(sx) : 0);
          const uint32_t srcY = oy + (sy > 0 ? uint32_t(sy) : 0);
          const uint32_t dstX = sx < 0 ? uint32_t(-sx) : 0;
          const uint32_t dstY = sy < 0 ? uint32_t(-sy) : 0;

          cmd.transition(*m_shadowMap, rhi::ResourceState::CopySrc);
          cmd.transition(*m_shadowScrollTemp, rhi::ResourceState::CopyDst);
          cmd.copyTextureRegion(*m_shadowScrollTemp, dstX, dstY, *m_shadowMap, srcX, srcY, keepW, keepH);
          cmd.transition(*m_shadowScrollTemp, rhi::ResourceState::CopySrc);
          cmd.transition(*m_shadowMap, rhi::ResourceState::CopyDst);
          cmd.copyTextureRegion(*m_shadowMap, ox + dstX, oy + dstY, *m_shadowScrollTemp, dstX, dstY, keepW, keepH);
        }
      }

      cmd.transition(*m_shadowMap, rhi::ResourceState::RenderTarget);
      rhi::Texture* rt = m_shadowMap.get();
      cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
      // Only full-clear when not doing partial toroidal scroll rebuild.
      bool needFullClear = !m_settings.enableToroidalShadows;
      if (m_settings.enableToroidalShadows) {
        for (int i = 0; i < 4; ++i) {
          if (m_shadowAtlas.isDirty(i) && m_shadowAtlas.cascade(i).lastScrollX == 0 &&
              m_shadowAtlas.cascade(i).lastScrollY == 0) {
            needFullClear = true;
          }
        }
      }
      if (needFullClear) {
        const float clear[4] = {1, 1, 1, 1};
        cmd.clearRenderTarget(*m_shadowMap, clear);
      }
      cmd.setRootSignature(*m_root);
      cmd.setPipeline(*m_shadowPSO);
      for (int cascade = 0; cascade < 4; ++cascade) {
        if (m_settings.enableToroidalShadows && !m_shadowAtlas.isDirty(cascade)) {
          continue;
        }
        const uint32_t ox = (cascade % 2) * tile;
        const uint32_t oy = (cascade / 2) * tile;
        cmd.setViewport({float(ox), float(oy), float(tile), float(tile), 0, 1});
        cmd.setScissor({int(ox), int(oy), int(ox + tile), int(oy + tile)});
        for (auto& obj : scene.objects) {
          if (!obj.mesh) {
            continue;
          }
          RootXform xform{frame.lightViewProj[cascade], obj.worldMatrix};
          cmd.setGraphicsRootConstants(0, &xform, 32);
          cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
          cmd.setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
          cmd.setIndexBuffer(obj.mesh->indexBuffer(), true);
          for (const auto& sub : obj.mesh->submeshes()) {
            cmd.drawIndexed(sub.indexCount, sub.indexOffset);
            ++m_drawCalls;
          }
        }
        if (m_settings.enableToroidalShadows) {
          m_shadowAtlas.clearDirty(cascade);
        }
      }
    }
    cmd.transition(*m_shadowMap, rhi::ResourceState::ShaderResource);
    updateUploadCB(*m_frameCB, &frame, sizeof(frame));
  } else {
    cmd.transition(*m_shadowMap, rhi::ResourceState::ShaderResource);
  }

  // ---- Octahedral point-light shadow atlas ----
  if (m_settings.enableShadows && m_settings.enableOctahedralPointShadows && m_octaShadows.atlas()) {
    const uint32_t n = m_octaShadows.updateLights(scene);
    if (n > 0) {
      cmd.transition(*m_octaShadows.atlas(), rhi::ResourceState::RenderTarget);
      rhi::Texture* ort = m_octaShadows.atlas();
      cmd.setRenderTargets(std::span<rhi::Texture*>(&ort, 1), nullptr);
      const float clear[4] = {1, 1, 1, 1};
      cmd.clearRenderTarget(*m_octaShadows.atlas(), clear);
      cmd.setRootSignature(*m_root);
      cmd.setPipeline(*m_shadowPSO);
      const uint32_t ts = m_octaShadows.tileSize();
      for (uint32_t slot = 0; slot < n; ++slot) {
        const glm::vec2 off = m_octaShadows.tileUvOffset(slot) * float(m_octaShadows.atlas()->width());
        // Pack both hemispheres side-by-side inside the tile.
        for (int hemi = 0; hemi < 2; ++hemi) {
          const float hx = off.x + float(hemi) * float(ts / 2);
          cmd.setViewport({hx, off.y, float(ts / 2), float(ts), 0, 1});
          cmd.setScissor({int(hx), int(off.y), int(hx) + int(ts / 2), int(off.y) + int(ts)});
          const glm::mat4 vpL = m_octaShadows.lightViewProj(slot, hemi);
          for (auto& obj : scene.objects) {
            if (!obj.mesh) {
              continue;
            }
            RootXform xform{vpL, obj.worldMatrix};
            cmd.setGraphicsRootConstants(0, &xform, 32);
            cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
            cmd.setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
            cmd.setIndexBuffer(obj.mesh->indexBuffer(), true);
            for (const auto& sub : obj.mesh->submeshes()) {
              cmd.drawIndexed(sub.indexCount, sub.indexOffset);
              ++m_drawCalls;
            }
          }
        }
      }
      cmd.transition(*m_octaShadows.atlas(), rhi::ResourceState::ShaderResource);
    }
  }

  // ---- GBuffer / VisBuffer ----
  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {asDxSamp(*m_samplerLinear).cpu};
  const uint32_t sampTable = dxDevice.writeSamplerTable(sampCpu, 1);
  m_meshletsTotal = 0;
  m_meshletsDrawn = 0;

  if (m_settings.enableVisibilityBuffer) {
    cmd.transition(*m_visId, rhi::ResourceState::RenderTarget);
    cmd.transition(*m_depth, rhi::ResourceState::DepthWrite);
    rhi::Texture* visRT = m_visId.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&visRT, 1), m_depth.get());
    const float zc[4] = {0, 0, 0, 0};
    cmd.clearRenderTarget(*m_visId, zc);
    cmd.clearDepth(*m_depth, 1.0f);
    cmd.setViewport(vp);
    cmd.setScissor(sc);
    cmd.setRootSignature(*m_root);
    cmd.setPipeline(*m_visBufferPSO);
    for (auto& obj : scene.objects) {
      if (!obj.mesh) {
        continue;
      }
      for (const auto& sub : obj.mesh->submeshes()) {
        ObjectCBData ocb{};
        ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
        ocb.baseColorFactor = glm::vec4(1);
        ocb.materialParams = {0, 1, 1, 0};
        ocb.emissiveFactor = glm::vec4(0);
        ocb.textureIndices = glm::uvec4(sub.materialIndex + 1, 0, 0, 0);
        const uint64_t off = pushObjectCB(ocb);
        RootXform xform{frame.viewProj, obj.worldMatrix};
        cmd.setGraphicsRootConstants(0, &xform, 32);
        cmd.setGraphicsRootCBV(2, *m_objectCB, off);
        cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd.setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd.setIndexBuffer(obj.mesh->indexBuffer(), true);
        cmd.drawIndexed(sub.indexCount, sub.indexOffset);
        ++m_drawCalls;
      }
    }
    cmd.transition(*m_visId, rhi::ResourceState::ShaderResource);
  }

  cmd.transition(*m_albedo, rhi::ResourceState::RenderTarget);
  cmd.transition(*m_normal, rhi::ResourceState::RenderTarget);
  cmd.transition(*m_orm, rhi::ResourceState::RenderTarget);
  cmd.transition(*m_emissive, rhi::ResourceState::RenderTarget);
  cmd.transition(*m_depthColor, rhi::ResourceState::RenderTarget);
  cmd.transition(*m_depth, rhi::ResourceState::DepthWrite);

  rhi::Texture* gbufferRTs[] = {m_albedo.get(), m_normal.get(), m_orm.get(), m_emissive.get(), m_depthColor.get()};
  cmd.setRenderTargets(gbufferRTs, m_depth.get());
  const float zclear[4] = {0, 0, 0, 0};
  cmd.clearRenderTarget(*m_albedo, zclear);
  cmd.clearRenderTarget(*m_normal, zclear);
  cmd.clearRenderTarget(*m_orm, zclear);
  cmd.clearRenderTarget(*m_emissive, zclear);
  cmd.clearRenderTarget(*m_depthColor, zclear);
  if (!m_settings.enableVisibilityBuffer) {
    cmd.clearDepth(*m_depth, 1.0f);
  }
  cmd.setViewport(vp);
  cmd.setScissor(sc);
  cmd.setRootSignature(*m_root);
  cmd.setPipeline(*m_gbufferPSO);
  // Bind entire bindless heap once — draws only update textureIndices in ObjectCB.
  cmd.setGraphicsRootSrvTable(3, 0);
  cmd.setGraphicsRootSamplerTable(4, sampTable);

  for (auto& obj : scene.objects) {
    if (!obj.mesh) {
      continue;
    }
    m_meshletsTotal += obj.mesh->meshletCount();

    auto drawSub = [&](uint32_t materialIndex, uint32_t indexCount, uint32_t indexOffset, rhi::Buffer& ib) {
      auto mat = (materialIndex < obj.materials.size()) ? obj.materials[materialIndex] : nullptr;
      auto* albedo = mat && mat->albedo ? &mat->albedo->resource() : &m_defaultAlbedo->resource();
      auto* normal = mat && mat->normal ? &mat->normal->resource() : &m_defaultNormal->resource();
      auto* orm = mat && mat->metallicRoughness ? &mat->metallicRoughness->resource() : &m_defaultORM->resource();
      auto* emissive = mat && mat->emissive ? &mat->emissive->resource() : &m_defaultBlack->resource();

      cmd.transition(*albedo, rhi::ResourceState::ShaderResource);
      cmd.transition(*normal, rhi::ResourceState::ShaderResource);
      cmd.transition(*orm, rhi::ResourceState::ShaderResource);
      cmd.transition(*emissive, rhi::ResourceState::ShaderResource);

      ObjectCBData ocb{};
      ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
      if (mat) {
        const float dielectricF0 = 0.16f * mat->reflectance * mat->reflectance;
        ocb.baseColorFactor = mat->baseColorFactor;
        ocb.materialParams = {mat->metallicFactor, mat->roughnessFactor, mat->aoFactor,
                              mat->alphaMask ? mat->alphaCutoff : 0.0f};
        ocb.emissiveFactor = glm::vec4(mat->emissiveFactor, dielectricF0);
        ocb.materialExt = {mat->clearcoat, mat->clearcoatRoughness, 0.0f, 0.0f};
      } else {
        ocb.baseColorFactor = glm::vec4(1);
        ocb.materialParams = {0, 1, 1, 0};
        ocb.emissiveFactor = glm::vec4(0, 0, 0, 0.04f);
        ocb.materialExt = glm::vec4(0);
      }
      ocb.textureIndices = glm::uvec4(albedo->bindlessIndex(), normal->bindlessIndex(), orm->bindlessIndex(),
                                      emissive->bindlessIndex());
      const uint64_t off = pushObjectCB(ocb);
      RootXform xform{frame.viewProj, obj.worldMatrix};
      cmd.setGraphicsRootConstants(0, &xform, 32);
      cmd.setGraphicsRootCBV(2, *m_objectCB, off);
      cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
      cmd.setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
      cmd.setIndexBuffer(ib, true);
      cmd.drawIndexed(indexCount, indexOffset);
      ++m_drawCalls;
    };

    if (m_settings.enableMeshlets && obj.mesh->meshletIndexBuffer() && obj.mesh->meshletCount() > 0) {
      cullMeshletsCPU(*obj.mesh, obj.worldMatrix, frame.viewProj, m_visibleMeshlets);
      m_meshletsDrawn += static_cast<uint32_t>(m_visibleMeshlets.size());
      if (!m_visibleMeshlets.empty()) {
        for (uint32_t mi : m_visibleMeshlets) {
          const auto& ml = obj.mesh->meshlets()[mi];
          const auto& sub = obj.mesh->submeshes()[ml.submeshIndex];
          drawSub(sub.materialIndex, ml.indexCount, ml.indexOffset, *obj.mesh->meshletIndexBuffer());
        }
      } else {
        for (const auto& sub : obj.mesh->submeshes()) {
          drawSub(sub.materialIndex, sub.indexCount, sub.indexOffset, obj.mesh->indexBuffer());
        }
      }
    } else {
      for (const auto& sub : obj.mesh->submeshes()) {
        drawSub(sub.materialIndex, sub.indexCount, sub.indexOffset, obj.mesh->indexBuffer());
      }
    }
  }

  if (m_settings.enableVisibilityBuffer) {
    cmd.transition(*m_visId, rhi::ResourceState::ShaderResource);
    cmd.transition(*m_depthColor, rhi::ResourceState::ShaderResource);
    struct Phase3CB {
      glm::mat4 invViewProj;
      glm::vec4 cameraPos;
      glm::vec4 screenSize;
      glm::vec4 params;
      glm::vec4 volumeOriginExtent;
      glm::vec4 iblParams;
    } p3{};
    p3.invViewProj = frame.invViewProj;
    p3.cameraPos = frame.cameraPos;
    p3.screenSize = frame.screenSize;
    updateUploadCB(*m_phase3CB, &p3, sizeof(p3));
    cmd.setRootSignature(*m_rootFS);
    auto resolveTarget = [&](rhi::PipelineState& pso, rhi::Texture& target) {
      cmd.transition(target, rhi::ResourceState::RenderTarget);
      rhi::Texture* rt = &target;
      cmd.setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
      cmd.setPipeline(pso);
      cmd.setGraphicsRootCBV(1, *m_phase3CB);
      D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
          asDxTex(*m_depthColor).srvCpu, asDxTex(*m_normal).srvCpu, asDxTex(*m_hdr).srvCpu,
          asDxTex(*m_orm).srvCpu,        asDxTex(*m_albedo).srvCpu, asDxTex(*m_ssgi).srvCpu,
          asDxTex(*m_ddgiAtlas).srvCpu,  asDxTex(*m_brdfLUT).srvCpu, asDxTex(*m_prefiltered).srvCpu,
          asDxTex(*m_visId).srvCpu};
      cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(srv, 10));
      cmd.setGraphicsRootSamplerTable(4, sampTable);
      cmd.draw(3);
      ++m_drawCalls;
    };
    // Overwrite albedo/normal for VisBuffer A/B (orm/emissive flat)
    resolveTarget(*m_visAlbedoPSO, *m_albedo);
    resolveTarget(*m_visNormalPSO, *m_normal);
    resolveTarget(*m_visOrmPSO, *m_orm);
    resolveTarget(*m_visEmissivePSO, *m_emissive);
  }

  cmd.transition(*m_albedo, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_normal, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_orm, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_emissive, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_depthColor, rhi::ResourceState::ShaderResource);

  // ---- Deferred rain GBuffer (Cry DeferredRainGBuffer) ----
  m_timeSeconds += std::max(0.001f, m_lastFrameMs) * 0.001f;
  m_rain.executeDeferredGBuffer(cmd, m_device, *m_albedo, *m_normal, *m_orm, *m_depthColor, *m_rainCB,
                                *m_samplerLinear, frame.invViewProj, frame.viewProj, frame.view, eye,
                                m_timeSeconds, m_width, m_height);

  // ---- AO ----
  if (m_settings.enableAO) {
    cmd.transition(*m_ao, rhi::ResourceState::RenderTarget);
    rhi::Texture* aoRT = m_ao.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&aoRT, 1), nullptr);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(*m_aoPSO);
    struct PostCB {
      glm::vec4 params;
      glm::vec4 texelSize;
    } pcb{{1, 0, 1.5f, 0}, {1.f / m_width, 1.f / m_height, 0, 0}};
    updateUploadCB(*m_frameCB, &pcb, sizeof(pcb));
    cmd.setGraphicsRootCBV(1, *m_frameCB);
    D3D12_CPU_DESCRIPTOR_HANDLE aoSrv2[] = {asDxTex(*m_albedo).srvCpu, asDxTex(*m_albedo).srvCpu,
                                            asDxTex(*m_depthColor).srvCpu, asDxTex(*m_normal).srvCpu};
    cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(aoSrv2, 4));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
    cmd.draw(3);
    cmd.transition(*m_ao, rhi::ResourceState::ShaderResource);
  }

  // ---- Lighting ----
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
  } lcb;
  lcb.invViewProj = frame.invViewProj;
  lcb.cameraPos = frame.cameraPos;
  lcb.sunDirectionIntensity = frame.sunDirectionIntensity;
  lcb.sunColor = frame.sunColor;
  lcb.ambientColor = frame.ambientColor;
  for (int i = 0; i < 4; ++i) {
    lcb.lightViewProj[i] = frame.lightViewProj[i];
  }
  lcb.cascadeSplits = frame.cascadeSplits;
  lcb.flags = frame.flags;
  lcb.screenSize = frame.screenSize;
  lcb.iblParams = glm::vec4(m_iblMaxMip, m_iblExposure, 0.0f, 0.0f);
  updateUploadCB(*m_frameCB, &lcb, sizeof(lcb));

  cmd.transition(*m_hdr, rhi::ResourceState::RenderTarget);
  rhi::Texture* hdrRT = m_hdr.get();
  cmd.setRenderTargets(std::span<rhi::Texture*>(&hdrRT, 1), nullptr);
  const float hdrClear[4] = {0, 0, 0, 1};
  cmd.clearRenderTarget(*m_hdr, hdrClear);
  cmd.setRootSignature(*m_rootFS);
  cmd.setPipeline(*m_lightingPSO);
  cmd.setGraphicsRootCBV(1, *m_frameCB);
  cmd.setGraphicsRootCBV(2, *m_lightCB);

  D3D12_CPU_DESCRIPTOR_HANDLE lightSrv[] = {
      asDxTex(*m_albedo).srvCpu,      asDxTex(*m_normal).srvCpu,       asDxTex(*m_orm).srvCpu,
      asDxTex(*m_emissive).srvCpu,    asDxTex(*m_depthColor).srvCpu,   asDxTex(*m_shadowMap).srvCpu,
      asDxTex(*m_brdfLUT).srvCpu,     asDxTex(*m_irradiance).srvCpu,   asDxTex(*m_prefiltered).srvCpu,
      asDxTex(*m_ao).srvCpu};
  D3D12_CPU_DESCRIPTOR_HANDLE lightSamp[] = {asDxSamp(*m_samplerLinear).cpu, asDxSamp(*m_samplerShadow).cpu};
  cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(lightSrv, 10));
  cmd.setGraphicsRootSamplerTable(4, dxDevice.writeSamplerTable(lightSamp, 2));
  cmd.draw(3);
  ++m_drawCalls;
  cmd.transition(*m_hdr, rhi::ResourceState::ShaderResource);

  // ---- Phase 3: SSGI / DDGI / SSR / Compose ----
  const bool wantGi = m_settings.giTier != GITier::Off;
  const bool wantSsr = m_settings.enableSSR;
  rhi::Texture* postHdr = m_hdr.get();

  if (wantGi || wantSsr) {
  struct Phase3CBData {
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;
    glm::vec4 screenSize;
    glm::vec4 params;
    glm::vec4 volumeOriginExtent;
    glm::vec4 iblParams;
  } p3cb;
  p3cb.invViewProj = frame.invViewProj;
  p3cb.cameraPos = frame.cameraPos;
  p3cb.screenSize = frame.screenSize;
  const float giTier = static_cast<float>(m_settings.giTier);
  p3cb.params = glm::vec4(0.55f, 0.003f, 16.0f, giTier);
  p3cb.volumeOriginExtent = glm::vec4(frame.cameraPos.x - 12.f, 0.0f, frame.cameraPos.z - 12.f, 24.0f);
  p3cb.iblParams = glm::vec4(m_iblMaxMip, m_iblExposure, 0, 0);
  updateUploadCB(*m_phase3CB, &p3cb, sizeof(p3cb));

  // Never bind the RT we're writing as an SRV (was causing flicker).
  auto bindPhase3Srv = [&](rhi::Texture& historyOrBlack) {
    D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
        asDxTex(*m_depthColor).srvCpu, asDxTex(*m_normal).srvCpu,      asDxTex(*m_hdr).srvCpu,
        asDxTex(*m_orm).srvCpu,        asDxTex(*m_albedo).srvCpu,      asDxTex(historyOrBlack).srvCpu,
        asDxTex(*m_ssr).srvCpu,        asDxTex(*m_brdfLUT).srvCpu,     asDxTex(*m_prefiltered).srvCpu,
        asDxTex(*m_visId).srvCpu};
    cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(srv, 10));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
  };

  if (wantGi) {
    cmd.transition(*m_ssgi, rhi::ResourceState::RenderTarget);
    rhi::Texture* ssgiRT = m_ssgi.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&ssgiRT, 1), nullptr);
    const float clearGi[4] = {0, 0, 0, 0};
    cmd.clearRenderTarget(*m_ssgi, clearGi);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(*m_ssgiPSO);
    cmd.setGraphicsRootCBV(1, *m_phase3CB);
    bindPhase3Srv(m_defaultBlack->resource());
    cmd.draw(3);
    ++m_drawCalls;
    cmd.transition(*m_ssgi, rhi::ResourceState::ShaderResource);

    if (m_settings.giTier >= GITier::Medium) {
      cmd.transition(*m_ddgiAtlas, rhi::ResourceState::UnorderedAccess);
      cmd.transition(*m_ddgiAtlasPrev, rhi::ResourceState::ShaderResource);
      cmd.setRootSignature(*m_rootCS);
      cmd.setPipeline(*m_ddgiUpdatePSO);
      cmd.setComputeRootCBV(1, *m_phase3CB);
      D3D12_CPU_DESCRIPTOR_HANDLE dsrv[] = {asDxTex(*m_hdr).srvCpu, asDxTex(*m_depthColor).srvCpu,
                                            asDxTex(*m_normal).srvCpu, asDxTex(*m_ddgiAtlasPrev).srvCpu};
      D3D12_CPU_DESCRIPTOR_HANDLE duav[] = {asDxTex(*m_ddgiAtlas).uavCpu};
      cmd.setComputeRootSrvTable(2, dxDevice.writeSrvTable(dsrv, 4));
      cmd.setComputeRootUavTable(3, dxDevice.writeUavTable(duav, 1));
      cmd.setComputeRootSamplerTable(4, sampTable);
      cmd.dispatch((64 + 7) / 8, (64 + 7) / 8, 1);
      ++m_drawCalls;
      cmd.transition(*m_ddgiAtlas, rhi::ResourceState::ShaderResource);
      std::swap(m_ddgiAtlas, m_ddgiAtlasPrev);

      if (m_settings.giTier >= GITier::High) {
        cmd.transition(*m_ssgi, rhi::ResourceState::RenderTarget);
        cmd.setRenderTargets(std::span<rhi::Texture*>(&ssgiRT, 1), nullptr);
        cmd.setRootSignature(*m_rootFS);
        cmd.setPipeline(*m_ddgiSamplePSO);
        cmd.setGraphicsRootCBV(1, *m_phase3CB);
        D3D12_CPU_DESCRIPTOR_HANDLE ddsrv[] = {
            asDxTex(*m_depthColor).srvCpu, asDxTex(*m_normal).srvCpu,         asDxTex(*m_hdr).srvCpu,
            asDxTex(*m_orm).srvCpu,        asDxTex(*m_albedo).srvCpu,         asDxTex(m_defaultBlack->resource()).srvCpu,
            asDxTex(*m_ddgiAtlasPrev).srvCpu, asDxTex(*m_brdfLUT).srvCpu,    asDxTex(*m_prefiltered).srvCpu,
            asDxTex(*m_visId).srvCpu};
        cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(ddsrv, 10));
        cmd.setGraphicsRootSamplerTable(4, sampTable);
        cmd.draw(3);
        ++m_drawCalls;
        cmd.transition(*m_ssgi, rhi::ResourceState::ShaderResource);
      }
    }
  } else {
    cmd.transition(*m_ssgi, rhi::ResourceState::RenderTarget);
    rhi::Texture* ssgiRT = m_ssgi.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&ssgiRT, 1), nullptr);
    const float z[4] = {0, 0, 0, 0};
    cmd.clearRenderTarget(*m_ssgi, z);
    cmd.transition(*m_ssgi, rhi::ResourceState::ShaderResource);
  }

  if (wantSsr) {
    cmd.transition(*m_ssr, rhi::ResourceState::RenderTarget);
    rhi::Texture* ssrRT = m_ssr.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&ssrRT, 1), nullptr);
    const float clearSsr[4] = {0, 0, 0, 0};
    cmd.clearRenderTarget(*m_ssr, clearSsr);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(*m_ssrPSO);
    cmd.setGraphicsRootCBV(1, *m_phase3CB);
    bindPhase3Srv(m_defaultBlack->resource());
    cmd.draw(3);
    ++m_drawCalls;
    cmd.transition(*m_ssr, rhi::ResourceState::ShaderResource);
  } else {
    cmd.transition(*m_ssr, rhi::ResourceState::RenderTarget);
    rhi::Texture* ssrRT = m_ssr.get();
    cmd.setRenderTargets(std::span<rhi::Texture*>(&ssrRT, 1), nullptr);
    const float z[4] = {0, 0, 0, 0};
    cmd.clearRenderTarget(*m_ssr, z);
    cmd.transition(*m_ssr, rhi::ResourceState::ShaderResource);
  }

  cmd.transition(*m_hdrCompose, rhi::ResourceState::RenderTarget);
  rhi::Texture* compRT = m_hdrCompose.get();
  cmd.setRenderTargets(std::span<rhi::Texture*>(&compRT, 1), nullptr);
  cmd.setRootSignature(*m_rootFS);
  cmd.setPipeline(*m_composePSO);
  cmd.setGraphicsRootCBV(1, *m_phase3CB);
  {
    D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {
        asDxTex(*m_depthColor).srvCpu, asDxTex(*m_normal).srvCpu, asDxTex(*m_hdr).srvCpu,
        asDxTex(*m_orm).srvCpu,        asDxTex(*m_albedo).srvCpu, asDxTex(*m_ssgi).srvCpu,
        asDxTex(*m_ssr).srvCpu,        asDxTex(*m_brdfLUT).srvCpu, asDxTex(*m_prefiltered).srvCpu,
        asDxTex(*m_visId).srvCpu};
    cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(srv, 10));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
  }
  cmd.draw(3);
  ++m_drawCalls;
  cmd.transition(*m_hdrCompose, rhi::ResourceState::ShaderResource);
  postHdr = m_hdrCompose.get();
  } // wantGi || wantSsr

  // ---- Contact shadows (after lighting/compose, before bloom) ----
  if (m_settings.enableContactShadows && m_contactShadowPSO) {
    struct ContactCB {
      glm::mat4 invViewProj;
      glm::vec4 cameraPos;
      glm::vec4 screenSize;
      glm::vec4 params;
    } ccb{frame.invViewProj, frame.cameraPos, frame.screenSize, glm::vec4(1.5f, 8.0f, 0.08f, 0.45f)};
    updateUploadCB(*m_frameCB, &ccb, sizeof(ccb));

    rhi::Texture* contactSrc = postHdr;
    rhi::Texture* contactDst = (postHdr == m_hdr.get()) ? m_hdrCompose.get() : m_hdr.get();
    cmd.transition(*contactDst, rhi::ResourceState::RenderTarget);
    cmd.transition(*contactSrc, rhi::ResourceState::ShaderResource);
    cmd.transition(*m_depthColor, rhi::ResourceState::ShaderResource);
    cmd.setRenderTargets(std::span<rhi::Texture*>(&contactDst, 1), nullptr);
    cmd.setRootSignature(*m_rootFS);
    cmd.setPipeline(*m_contactShadowPSO);
    cmd.setGraphicsRootCBV(1, *m_frameCB);
    D3D12_CPU_DESCRIPTOR_HANDLE csv[] = {asDxTex(*m_depthColor).srvCpu, asDxTex(*contactSrc).srvCpu};
    cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(csv, 2));
    cmd.setGraphicsRootSamplerTable(4, sampTable);
    cmd.setViewport(vp);
    cmd.setScissor(sc);
    cmd.draw(3);
    ++m_drawCalls;
    cmd.transition(*contactDst, rhi::ResourceState::ShaderResource);
    postHdr = contactDst;
  }

  // ---- Rain post (SceneRain volumes + mist + lens drops) ----
  {
    rhi::Texture* rainSrc = postHdr;
    rhi::Texture* rainTmp = (postHdr == m_hdr.get()) ? m_hdrCompose.get() : m_hdr.get();
    // Bloom not ready yet — light streaks with HDR itself (Cry uses bloom+luminance)
    rhi::Texture* rainOut = m_rain.executePost(
        cmd, m_device, *rainSrc, *rainTmp, *m_depthColor, *m_normal, *rainSrc, *m_rainCB, *m_samplerLinear,
        frame.invViewProj, frame.viewProj, frame.view, eye, m_timeSeconds, m_width, m_height);
    if (rainOut) {
      postHdr = rainOut;
    }
  }

  // ---- Bloom (from composed or raw HDR) ----
  if (m_settings.enableBloom) {
    std::shared_ptr<rhi::Texture> src = postHdr == m_hdrCompose.get() ? m_hdrCompose : m_hdr;
    for (uint32_t i = 0; i < 4; ++i) {
      cmd.transition(*m_bloomMips[i], rhi::ResourceState::RenderTarget);
      rhi::Texture* brt = m_bloomMips[i].get();
      cmd.setRenderTargets(std::span<rhi::Texture*>(&brt, 1), nullptr);
      cmd.setPipeline(*m_bloomDownPSO);
      struct PostCB {
        glm::vec4 params;
        glm::vec4 texelSize;
      } pcb{{1, 0, 0, 0},
            {1.f / float(m_bloomMips[i]->width()), 1.f / float(m_bloomMips[i]->height()), 0, 0}};
      updateUploadCB(*m_frameCB, &pcb, sizeof(pcb));
      cmd.setGraphicsRootCBV(1, *m_frameCB);
      D3D12_CPU_DESCRIPTOR_HANDLE s[] = {asDxTex(*src).srvCpu};
      cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(s, 1));
      cmd.setGraphicsRootSamplerTable(4, sampTable);
      cmd.setViewport({0, 0, float(m_bloomMips[i]->width()), float(m_bloomMips[i]->height()), 0, 1});
      cmd.setScissor({0, 0, int(m_bloomMips[i]->width()), int(m_bloomMips[i]->height())});
      cmd.draw(3);
      cmd.transition(*m_bloomMips[i], rhi::ResourceState::ShaderResource);
      src = m_bloomMips[i];
    }
    cmd.setViewport(vp);
    cmd.setScissor(sc);
  }

  // ---- Tonemap to swapchain ----
  cmd.transition(swapChainRT, rhi::ResourceState::RenderTarget);
  rhi::Texture* bb = &swapChainRT;
  cmd.setRenderTargets(std::span<rhi::Texture*>(&bb, 1), nullptr);
  cmd.setRootSignature(*m_rootFS);
  cmd.setPipeline(*m_tonemapPSO);
  struct PostCB {
    glm::vec4 params;
    glm::vec4 texelSize;
  } tcb{{1.0f, m_settings.enableBloom ? 0.25f : 0.0f, 0, 0}, {0, 0, 0, 0}};
  updateUploadCB(*m_frameCB, &tcb, sizeof(tcb));
  cmd.setGraphicsRootCBV(1, *m_frameCB);
  auto& bloomSrc = m_settings.enableBloom ? *m_bloomMips[0] : *postHdr;
  D3D12_CPU_DESCRIPTOR_HANDLE tonemapSrv[] = {asDxTex(*postHdr).srvCpu, asDxTex(bloomSrc).srvCpu};
  cmd.setGraphicsRootSrvTable(3, dxDevice.writeSrvTable(tonemapSrv, 2));
  cmd.setGraphicsRootSamplerTable(4, sampTable);
  cmd.setViewport(vp);
  cmd.setScissor(sc);
  cmd.draw(3);
  ++m_drawCalls;

  const auto t1 = std::chrono::steady_clock::now();
  m_lastFrameMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}
} // namespace tucano


