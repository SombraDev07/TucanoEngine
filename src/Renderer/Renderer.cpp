#include "Renderer/Renderer.h"
#include "Platform/FileSystem.h"
#include "Renderer/Deferred/GBufferPass.h"
#include "Renderer/Deferred/LightingPass.h"
#include "Renderer/GI/IBL.h"
#include "Renderer/PostFX/AOPass.h"
#include "Renderer/PostFX/BloomPass.h"
#include "Renderer/PostFX/ExposurePass.h"
#include "Renderer/Shadows/ToroidalShadows.h"
#include "Renderer/Weather/RainSystem.h"
#include "RHI/DX12/DX12Common.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

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
  glm::uvec4 textureIndices2;
  glm::vec4 fuzzColor;
};

struct DrawMaterialGPU {
  glm::vec4 baseColorFactor;
  glm::vec4 materialParams;
  glm::vec4 emissiveFactor;
  glm::uvec4 textureIndices;
  glm::vec4 materialExt;
  glm::uvec4 textureIndices2;
  glm::vec4 fuzzColor;
};

struct RootXform {
  glm::mat4 viewProj;
  glm::mat4 world;
};

struct RootOctaXform {
  glm::mat4 world;
  glm::vec4 lightPosRange;
  glm::vec4 pad0;
  glm::vec4 pad1;
  glm::vec4 pad2;
};

struct LightCBData {
  uint32_t lightCount = 0;
  uint32_t octaCount = 0;
  uint32_t spotCount = 0;
  uint32_t pad = 0;
  glm::vec4 lightPosType[16]{};
  glm::vec4 lightColorIntensity[16]{};
  glm::vec4 lightRangeParams[16]{};
  glm::vec4 lightDirection[16]{};
  glm::vec4 octaPosRange[8]{};
  glm::vec4 octaUv[8]{};
  glm::vec4 spotUv[4]{};
  glm::mat4 spotViewProj[4]{};
};

struct ShadowFrameCB {
  glm::mat4 lightViewProj;
};

std::string shaderPath(const char* name) { return joinPath(TUCANO_SHADER_DIR, name); }

void updateUploadCB(rhi::Buffer& buffer, const void* data, size_t size) {
  void* mapped = buffer.mapped();
  if (!mapped) {
    throw std::runtime_error("updateUploadCB: buffer is not CPU-mapped");
  }
  std::memcpy(mapped, data, size);
}

::tucano::rhi::DX12Texture& asDxTex(rhi::Texture& t) { return static_cast<::tucano::rhi::DX12Texture&>(t); }
::tucano::rhi::DX12Buffer& asDxBuf(rhi::Buffer& b) { return static_cast<::tucano::rhi::DX12Buffer&>(b); }
uint32_t bindlessOf(rhi::Texture& t) { return t.bindlessIndex(); }
uint32_t uavOf(rhi::Texture& t) { return asDxTex(t).uavIndex; }
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
  cb.size = 256ull * 256ull; // ring for AO/Bloom/Tonemap (must not share lighting CB)
  cb.debugName = "PostCB";
  m_postCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256ull * 4096ull;
  cb.debugName = "ObjectCB";
  m_objectCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256 * 16; // octa view-projs + light arrays
  cb.debugName = "LightCB";
  m_lightCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256;
  cb.debugName = "Phase3CB";
  m_phase3CB = m_device.createBuffer(cb, nullptr);
  cb.debugName = "ProbeCaptureCB";
  m_probeCaptureCB = m_device.createBuffer(cb, nullptr);
  cb.debugName = "ProbeConvertCB";
  m_probeConvertCB = m_device.createBuffer(cb, nullptr);
  cb.debugName = "RTCB";
  m_rtCB = m_device.createBuffer(cb, nullptr);
  {
    const uint32_t fw = ReflectionProbes::kFaceSize * ReflectionProbes::kFaces;
    const uint32_t fh = ReflectionProbes::kFaceSize;
    const uint32_t pitch = (fw * 8u + 255u) & ~255u;
    rhi::BufferDesc rb{};
    rb.size = uint64_t(pitch) * fh;
    rb.usage = rhi::BufferUsage::Readback;
    rb.debugName = "ProbeFaceReadback";
    m_probeFaceReadback = m_device.createBuffer(rb, nullptr);
  }
  cb.size = 512;
  cb.debugName = "RainCB";
  m_rainCB = m_device.createBuffer(cb, nullptr);
  cb.size = 256;
  cb.debugName = "MeshletCullCB";
  m_meshletCullCB = m_device.createBuffer(cb, nullptr);
  {
    rhi::BufferDesc args{};
    args.size = sizeof(uint32_t) * 5 * 65536; // DrawIndexedArgs × 64k
    args.usage = rhi::BufferUsage::Indirect | rhi::BufferUsage::UnorderedAccess;
    args.stride = sizeof(uint32_t) * 5;
    args.debugName = "IndirectArgs";
    m_indirectArgs = m_device.createBuffer(args, nullptr);
    args.debugName = "CompactedIndirectArgs";
    m_compactedArgs = m_device.createBuffer(args, nullptr);
    args.size = sizeof(uint32_t);
    args.stride = sizeof(uint32_t);
    args.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
    args.debugName = "IndirectCount";
    m_indirectCount = m_device.createBuffer(args, nullptr);
    args.size = sizeof(DrawMaterialGPU) * 4096ull;
    args.stride = sizeof(DrawMaterialGPU);
    args.usage = rhi::BufferUsage::Structured;
    args.debugName = "DrawMaterials";
    m_drawMaterials = m_device.createBuffer(args, nullptr);
  }

  m_rain.init(m_device);
  m_rain.resize(m_device, m_width, m_height);
  m_rain.params().enabled = false;

  m_rtScene.init(m_device);
  if (m_device.supportsRaytracing()) {
    // Keep RT opt-in until Ray Query path is stable on all scenes (page-fault TDR risk).
    m_settings.enableRTReflections = false;
    m_settings.enableRTShadows = false;
    if (!m_loggedNoDXR) {
      std::cout << "[Renderer] DXR available — enable RT via ImGui (off by default)\n";
      m_loggedNoDXR = true;
    }
  } else if (!m_loggedNoDXR) {
    std::cout << "[Renderer] DXR unavailable — using SSR + CSM (RT off)\n";
    m_loggedNoDXR = true;
  }
  if (!m_device.supportsMeshShaders() && !m_loggedNoMeshShaders) {
    std::cout << "[Renderer] Mesh shaders unavailable — using IA meshlet path\n";
    m_loggedNoMeshShaders = true;
  }

  std::cout << "[Renderer] Building IBL (HDRI cook with procedural fallback)...\n";
  {
    std::string hdri = joinPath(TUCANO_ENGINE_ASSETS_DIR, m_settings.hdriPath);
    if (!fileExists(hdri) && fileExists(m_settings.hdriPath)) {
      hdri = m_settings.hdriPath;
    }
    IBLTextures ibl = createIBLFromHDRIFile(m_device, hdri, 512);
    m_brdfLUT = ibl.brdfLUT->shared();
    m_irradiance = ibl.irradiance->shared();
    m_prefiltered = ibl.prefiltered->shared();
    m_iblMaxMip = ibl.maxMip;
  }
  m_iblExposure = 1.35f;
  std::cout << "[Renderer] IBL ready (maxMip=" << m_iblMaxMip << ")\n";

  rhi::BufferDesc expCb{};
  expCb.size = 256;
  expCb.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
  expCb.debugName = "ExposureCB";
  m_exposureCB = m_device.createBuffer(expCb, nullptr);
}

Renderer::~Renderer() {
  try {
    m_device.waitIdle();
  } catch (...) {
  }
}

void Renderer::recreateAfterDeviceLost() {
  const RendererSettings settings = m_settings;
  const RainParams rainParams = m_rain.params();
  const uint32_t w = m_width;
  const uint32_t h = m_height;
  rhi::Device& device = m_device;
  this->~Renderer();
  new (this) Renderer(device, w, h);
  m_settings = settings;
  m_rain.params() = rainParams;
}

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
  const auto rtSrvUav =
      rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
  const auto uavSrv = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
  m_albedo = makeRT(rhi::Format::R8G8B8A8_UNORM_SRGB, rtSrv, "GBufferAlbedo");
  m_normal = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferNormal");
  m_orm = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferORM");
  m_emissive = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "GBufferEmissive");
  m_depthColor = makeRT(rhi::Format::R32_FLOAT, rtSrv, "DepthColor");
  m_depth = makeRT(rhi::Format::D32_FLOAT, rhi::TextureUsage::DepthStencil, "MainDepth");
  m_hdr = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrvUav, "HDR");
  m_hdrCompose = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrvUav, "HDRCompose");
  m_ao = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "AO");
  m_aoTemp = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "AOTemp");
  m_shadowMap = makeRT(rhi::Format::R32_FLOAT, rtSrv, "ShadowMap", m_settings.shadowMapSize, m_settings.shadowMapSize);
  m_shadowScrollTemp =
      makeRT(rhi::Format::R32_FLOAT, rtSrv, "ShadowScrollTemp", m_settings.shadowMapSize / 2, m_settings.shadowMapSize / 2);
  {
    const float ends[4] = {5.f, 20.f, 60.f, 200.f};
    m_shadowAtlas.configure(m_settings.shadowMapSize, ends);
  }
  m_octaShadows.init(m_device, 2048, 512);
  m_vsm.init(m_device);
  m_ssgi = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "SSGI");
  m_ssgiHistory = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "SSGIHistory");
  m_depthHistory = makeRT(rhi::Format::R32_FLOAT, rtSrv, "DepthHistory");
  m_ssr = makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrvUav, "SSR");
  m_rtShadowMask = makeRT(rhi::Format::R32_FLOAT, uavSrv, "RTShadowMask", std::max(1u, m_width / 2),
                          std::max(1u, m_height / 2));
  m_visId = makeRT(rhi::Format::R32_UINT, rtSrv, "VisId");
  m_visUv = makeRT(rhi::Format::R32G32_FLOAT, rtSrv, "VisUV");
  m_visNormal = makeRT(rhi::Format::R8G8B8A8_UNORM, rtSrv, "VisNormal");
  m_visDepth = makeRT(rhi::Format::R32_FLOAT, rtSrv, "VisDepth");
  for (uint32_t i = 0; i < m_hizMips.size(); ++i) {
    m_hizMips[i] = makeRT(rhi::Format::R32_FLOAT, rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess,
                          "HiZMip", std::max(1u, m_width >> i), std::max(1u, m_height >> i));
  }
  m_ddgiAtlas = makeRT(rhi::Format::R16G16B16A16_FLOAT, uavSrv, "DDGIAtlas", 128, 128);
  m_ddgiAtlasPrev = makeRT(rhi::Format::R16G16B16A16_FLOAT, uavSrv, "DDGIAtlasPrev", 128, 128);

  for (uint32_t i = 0; i < m_bloomMips.size(); ++i) {
    m_bloomMips[i] =
        makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "BloomMip", std::max(1u, m_width >> (i + 1)),
               std::max(1u, m_height >> (i + 1)));
  }
  m_bloomScratch =
      makeRT(rhi::Format::R16G16B16A16_FLOAT, rtSrv, "BloomScratch", std::max(1u, m_width / 2),
             std::max(1u, m_height / 2));

  {
    rhi::TextureDesc hd{};
    hd.width = 256;
    hd.height = 1;
    hd.format = rhi::Format::R32_UINT;
    hd.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
    hd.debugName = "LumaHistogram";
    m_histogram = m_device.createTexture(hd, nullptr, 0);
  }
  {
    float one = 1.0f;
    rhi::TextureDesc ed{};
    ed.width = 1;
    ed.height = 1;
    ed.format = rhi::Format::R32_FLOAT;
    ed.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
    ed.debugName = "AutoExposure";
    m_exposureTex = m_device.createTexture(ed, &one, sizeof(float));
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
    d.rootSignature = m_root;
    d.vs = load("ShadowOcta_VSMain.cso");
    d.ps = load("ShadowOcta_PSMain.cso");
    d.rtvFormats = {rhi::Format::R32_FLOAT};
    d.dsvFormat = rhi::Format::Unknown;
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    m_shadowOctaPSO = m_device.createGraphicsPipeline(d);
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
    d.ps = load("PostFX_PSGTAO.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_aoPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("PostFX_VSMain.cso");
    d.ps = load("PostFX_PSGTAOBlur.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_aoBlurPSO = m_device.createGraphicsPipeline(d);
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
    d.rtvFormats = {rhi::Format::R32_UINT, rhi::Format::R32G32_FLOAT, rhi::Format::R8G8B8A8_UNORM,
                    rhi::Format::R32_FLOAT};
    d.dsvFormat = rhi::Format::D32_FLOAT;
    d.cullMode = rhi::CullMode::None;
    m_visBufferPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_root;
    d.vs = load("GBufferMeshlet_VSMain.cso");
    d.ps = load("GBufferMeshlet_PSMain.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB, rhi::Format::R8G8B8A8_UNORM, rhi::Format::R8G8B8A8_UNORM,
                    rhi::Format::R8G8B8A8_UNORM, rhi::Format::R32_FLOAT};
    d.dsvFormat = rhi::Format::D32_FLOAT;
    d.cullMode = rhi::CullMode::None;
    m_gbufferMeshletPSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_rootFS;
    d.vs = load("VisResolve_VSMain.cso");
    d.ps = load("VisResolve_PSResolve.cso");
    d.rtvFormats = {rhi::Format::R8G8B8A8_UNORM_SRGB, rhi::Format::R8G8B8A8_UNORM, rhi::Format::R8G8B8A8_UNORM,
                    rhi::Format::R8G8B8A8_UNORM, rhi::Format::R32_FLOAT};
    d.depthEnable = false;
    d.cullMode = rhi::CullMode::None;
    d.useInputLayout = false;
    m_visResolvePSO = m_device.createGraphicsPipeline(d);
  }
  if (m_device.supportsMeshShaders()) {
    try {
      rhi::GraphicsPipelineDesc d{};
      d.rootSignature = m_root;
      d.as = load("MeshletMesh_ASMain.cso");
      d.ms = load("MeshletMesh_MSMain.cso");
      d.ps = load("MeshletMesh_PSMain.cso");
      d.useMeshPipeline = true;
      d.rtvFormats = {rhi::Format::R32_UINT, rhi::Format::R32G32_FLOAT, rhi::Format::R8G8B8A8_UNORM,
                      rhi::Format::R32_FLOAT};
      d.dsvFormat = rhi::Format::D32_FLOAT;
      d.cullMode = rhi::CullMode::None;
      m_meshletMeshPSO = m_device.createGraphicsPipeline(d);
    } catch (const std::exception& e) {
      std::cerr << "[Renderer] Mesh PSO failed (" << e.what() << ") — using IA meshlet path\n";
      m_meshletMeshPSO = nullptr;
      m_settings.enableMeshShaders = false;
    }
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("DDGIUpdate_CSMain.cso");
    m_ddgiUpdatePSO = m_device.createComputePipeline(d);
  }
  {
    rhi::GraphicsPipelineDesc d{};
    d.rootSignature = m_root;
    d.vs = load("ProbeCapture_VSMain.cso");
    d.ps = load("ProbeCapture_PSMain.cso");
    d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
    d.dsvFormat = rhi::Format::D32_FLOAT;
    d.depthEnable = true;
    d.cullMode = rhi::CullMode::Back;
    m_probeCapturePSO = m_device.createGraphicsPipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("ProbeCubeToLatLong_CSMain.cso");
    m_probeConvertPSO = m_device.createComputePipeline(d);
  }
  if (m_device.supportsRaytracing()) {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("RTShadows_CSMain.cso");
    m_rtShadowsPSO = m_device.createComputePipeline(d);
    d.cs = load("RTContact_CSMain.cso");
    m_rtContactPSO = m_device.createComputePipeline(d);
    d.cs = load("RTReflections_CSMain.cso");
    m_rtReflectionsPSO = m_device.createComputePipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("MeshletCull_CSMain.cso");
    m_meshletCullPSO = m_device.createComputePipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("MeshletCompact_CSMain.cso");
    m_meshletCompactPSO = m_device.createComputePipeline(d);
    d.cs = load("HiZPyramid_CSCopy.cso");
    m_hizCopyPSO = m_device.createComputePipeline(d);
    d.cs = load("HiZPyramid_CSReduce.cso");
    m_hizReducePSO = m_device.createComputePipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("Exposure_CSClearHistogram.cso");
    m_exposureClearPSO = m_device.createComputePipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("Exposure_CSBuildHistogram.cso");
    m_exposureBuildPSO = m_device.createComputePipeline(d);
  }
  {
    rhi::ComputePipelineDesc d{};
    d.rootSignature = m_rootCS;
    d.cs = load("Exposure_CSReduceExposure.cso");
    m_exposureReducePSO = m_device.createComputePipeline(d);
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

void Renderer::render(rhi::CommandList*& cmd, rhi::Texture& swapChainRT, Scene& scene) {
  const auto t0 = std::chrono::steady_clock::now();
  m_drawCalls = 0;
  auto& dxDevice = static_cast<::tucano::rhi::DX12Device&>(m_device);

  // Deferred execute hooks — filled after Frame/Light CBs, invoked by m_graph.execute.
  std::function<void(rhi::CommandList&)> rgShadow;
  std::function<void(rhi::CommandList&)> rgGBuffer;
  std::function<void(rhi::CommandList&)> rgAO;
  std::function<void(rhi::CommandList&)> rgLighting;
  std::function<void(rhi::CommandList&)> rgSSGI;
  std::function<void(rhi::CommandList&)> rgDDGI;
  std::function<void(rhi::CommandList&)> rgDDGISample;
  std::function<void(rhi::CommandList&)> rgSSR;
  std::function<void(rhi::CommandList&)> rgCompose;
  std::function<void(rhi::CommandList&)> rgContact;
  std::function<void(rhi::CommandList&)> rgRain;
  std::function<void(rhi::CommandList&)> rgBloom;
  std::function<void(rhi::CommandList&)> rgExposure;
  std::function<void(rhi::CommandList&)> rgHiZ;
  std::function<void(rhi::CommandList&)> rgTonemap;
  rhi::Texture* postHdr = m_hdr.get();

  m_graph.reset();
  const RGHandle hAlbedo = m_graph.importTexture("albedo", m_albedo.get());
  const RGHandle hNormal = m_graph.importTexture("normal", m_normal.get());
  const RGHandle hOrm = m_graph.importTexture("orm", m_orm.get());
  const RGHandle hEmissive = m_graph.importTexture("emissive", m_emissive.get());
  const RGHandle hDepthColor = m_graph.importTexture("depthColor", m_depthColor.get());
  const RGHandle hDepth = m_graph.importTexture("depth", m_depth.get());
  const RGHandle hHdr = m_graph.importTexture("hdr", m_hdr.get());
  const RGHandle hShadow = m_graph.importTexture("shadow", m_shadowMap.get());
  const RGHandle hAo = m_graph.importTexture("ao", m_ao.get());
  const RGHandle hSsgi = m_graph.importTexture("ssgi", m_ssgi.get());
  const RGHandle hSsgiHist = m_graph.importTexture("ssgiHistory", m_ssgiHistory.get());
  const RGHandle hSsr = m_graph.importTexture("ssr", m_ssr.get());
  const RGHandle hCompose = m_graph.importTexture("hdrCompose", m_hdrCompose.get());
  const RGHandle hDdgi = m_graph.importTexture("ddgiAtlas", m_ddgiAtlas.get());
  const RGHandle hDdgiPrev = m_graph.importTexture("ddgiAtlasPrev", m_ddgiAtlasPrev.get());
  const RGHandle hBloom0 = m_graph.importTexture("bloom0", m_bloomMips[0].get());
  const RGHandle hHist = m_graph.importTexture("histogram", m_histogram.get());
  const RGHandle hExposure = m_graph.importTexture("exposure", m_exposureTex.get());
  const RGHandle hSwap = m_graph.importTexture("swapchain", &swapChainRT);
  const RGHandle hHiz0 = m_graph.importTexture("hiz0", m_hizMips[0].get());

  const bool wantGi = m_settings.giTier != GITier::Off;
  const bool wantSsr = m_settings.enableSSR;
  const bool wantPhase3 = wantGi || wantSsr;

  m_graph.addPass(
      "Shadow",
      [&](RGPassBuilder& b) {
        b.write(hShadow, RGUsage::RenderTarget);
        b.enabled = m_settings.enableShadows;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgShadow) {
          rgShadow(c);
        }
      });
  m_graph.addPass(
      "GBuffer",
      [&](RGPassBuilder& b) {
        b.write(hAlbedo, RGUsage::RenderTarget);
        b.write(hNormal, RGUsage::RenderTarget);
        b.write(hOrm, RGUsage::RenderTarget);
        b.write(hEmissive, RGUsage::RenderTarget);
        b.write(hDepthColor, RGUsage::RenderTarget);
        b.write(hDepth, RGUsage::DepthWrite);
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgGBuffer) {
          rgGBuffer(c);
        }
      });
  m_graph.addPass(
      "AO",
      [&](RGPassBuilder& b) {
        b.read(hDepthColor);
        b.read(hNormal);
        b.write(hAo, RGUsage::RenderTarget);
        b.enabled = m_settings.enableAO;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgAO) {
          rgAO(c);
        }
      });
  m_graph.addPass(
      "Lighting",
      [&](RGPassBuilder& b) {
        b.read(hAlbedo);
        b.read(hNormal);
        b.read(hOrm);
        b.read(hEmissive);
        b.read(hDepthColor);
        b.read(hShadow);
        b.read(hAo);
        b.write(hHdr, RGUsage::RenderTarget);
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgLighting) {
          rgLighting(c);
        }
      });
  m_graph.addPass(
      "SSGI",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hDepthColor);
        b.read(hNormal);
        b.read(hSsgiHist);
        b.write(hSsgi, RGUsage::RenderTarget);
        b.enabled = wantPhase3;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgSSGI) {
          rgSSGI(c);
        }
      });
  m_graph.addPass(
      "DDGIUpdate",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hDepthColor);
        b.read(hDdgiPrev);
        b.write(hDdgi, RGUsage::UnorderedAccess);
        b.enabled = wantGi && m_settings.giTier >= GITier::Medium;
        b.asyncHint = m_settings.enableAsyncCompute;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgDDGI) {
          rgDDGI(c);
        }
      });
  m_graph.addPass(
      "DDGISample",
      [&](RGPassBuilder& b) {
        b.read(hDdgi);
        b.read(hSsgi);
        b.write(hSsgiHist, RGUsage::RenderTarget);
        b.enabled = wantGi && m_settings.giTier >= GITier::Medium;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgDDGISample) {
          rgDDGISample(c);
        }
      });
  m_graph.addPass(
      "SSR",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hDepthColor);
        b.read(hNormal);
        b.write(hSsr, RGUsage::RenderTarget);
        b.enabled = wantPhase3;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgSSR) {
          rgSSR(c);
        }
      });
  m_graph.addPass(
      "Compose",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hSsgi);
        b.read(hSsgiHist);
        b.read(hSsr);
        b.write(hCompose, RGUsage::RenderTarget);
        b.enabled = wantPhase3;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgCompose) {
          rgCompose(c);
        }
      });
  m_graph.addPass(
      "ContactShadows",
      [&](RGPassBuilder& b) {
        b.read(hDepthColor);
        b.read(hHdr);
        b.read(hCompose);
        b.write(hHdr, RGUsage::RenderTarget);
        b.write(hCompose, RGUsage::RenderTarget);
        b.enabled = m_settings.enableContactShadows && (m_contactShadowPSO != nullptr || m_rtContactPSO != nullptr);
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgContact) {
          rgContact(c);
        }
      });
  m_graph.addPass(
      "RainPost",
      [&](RGPassBuilder& b) {
        b.read(hDepthColor);
        b.read(hNormal);
        b.read(hHdr);
        b.read(hCompose);
        b.write(hHdr, RGUsage::RenderTarget);
        b.write(hCompose, RGUsage::RenderTarget);
        b.enabled = m_rain.params().enabled;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgRain) {
          rgRain(c);
        }
      });
  m_graph.addPass(
      "Bloom",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hCompose);
        b.write(hBloom0, RGUsage::RenderTarget);
        b.enabled = m_settings.enableBloom;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgBloom) {
          rgBloom(c);
        }
      });
  m_graph.addPass(
      "Exposure",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hCompose);
        b.write(hHist, RGUsage::UnorderedAccess);
        b.write(hExposure, RGUsage::UnorderedAccess);
        b.enabled = m_settings.enableAutoExposure;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgExposure) {
          rgExposure(c);
        }
      });
  m_graph.addPass(
      "HiZPyramid",
      [&](RGPassBuilder& b) {
        b.read(hDepthColor);
        b.write(hHiz0, RGUsage::UnorderedAccess);
        b.enabled = m_settings.enableHiZOcclusion && m_hizCopyPSO != nullptr && m_hizReducePSO != nullptr;
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgHiZ) {
          rgHiZ(c);
        }
      });
  m_graph.addPass(
      "Tonemap",
      [&](RGPassBuilder& b) {
        b.read(hHdr);
        b.read(hCompose);
        b.read(hBloom0);
        b.read(hExposure);
        b.write(hSwap, RGUsage::RenderTarget);
      },
      [&](rhi::CommandList& c, RenderGraph&) {
        if (rgTonemap) {
          rgTonemap(c);
        }
      });
  // Alias packer smoke (non-visual) — keeps aliasedBytes stats honest.
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
  frame.proj = glm::perspectiveLH_ZO(glm::radians(60.0f), float(m_width) / float(std::max(1u, m_height)),
                                     scene.camera.nearPlane(), scene.camera.farPlane());
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
                          m_settings.enableAO ? 1.f : 0.f, m_settings.enableESM ? 1.f : 0.f);

  if (m_settings.enableVoxelGI) {
    m_voxelGI.update(m_device, scene, 0.25f);
    const glm::vec3 sunDirN = glm::normalize(glm::vec3(frame.sunDirectionIntensity));
    const float sunI = frame.sunDirectionIntensity.w;
    m_worldSdf.update(m_device, scene, eye, sunDirN, sunI, 0.25f);
    if (!m_reflectionProbes.ready()) {
      m_reflectionProbes.init(m_device);
    }
    m_reflectionProbes.placeProbes(scene, eye);
    m_skyVis.update(scene, eye, 0.2f);
    m_skyOcclusionAvg = m_skyVis.sample(eye);
    // Modulate ambient by sky visibility so interiors don't get full outdoor IBL.
    frame.ambientColor *= glm::vec4(m_skyOcclusionAvg, m_skyOcclusionAvg, m_skyOcclusionAvg, 1.0f);
  } else if (m_settings.enableSSR) {
    if (!m_reflectionProbes.ready()) {
      m_reflectionProbes.init(m_device);
    }
    m_reflectionProbes.placeProbes(scene, eye);
  }

  // Lighting FrameCB is written by executeLightingPass; post passes use m_postCB ring.
  m_postCBBump = 0;
  updateUploadCB(*m_frameCB, &frame, sizeof(frame));

  // Octa / spot slots first so lighting can map lights → atlas tiles
  uint32_t octaN = 0;
  uint32_t spotN = 0;
  if (m_settings.enableShadows && m_settings.enableOctahedralPointShadows) {
    octaN = m_octaShadows.updateLights(scene);
    spotN = m_octaShadows.spotCount();
  }

  LightCBData lights{};
  lights.lightCount = 0;
  lights.octaCount = octaN;
  lights.spotCount = spotN;
  const float atlasW = float(m_octaShadows.atlas() ? m_octaShadows.atlas()->width() : 2048);
  for (uint32_t s = 0; s < octaN; ++s) {
    lights.octaPosRange[s] = glm::vec4(m_octaShadows.lightPos(s), m_octaShadows.lightRange(s));
    const glm::vec2 off = m_octaShadows.tileUvOffset(s);
    lights.octaUv[s] = glm::vec4(off.x, off.y, m_octaShadows.tileUvScale(), atlasW);
  }
  for (uint32_t s = 0; s < spotN; ++s) {
    const uint32_t atlasSlot = m_octaShadows.spotAtlasSlot(s);
    const glm::vec2 off = m_octaShadows.tileUvOffset(atlasSlot);
    lights.spotUv[s] = glm::vec4(off.x, off.y, m_octaShadows.tileUvScale(), atlasW);
    lights.spotViewProj[s] = m_octaShadows.spotViewProj(s);
  }

  auto findOctaSlot = [&](const glm::vec3& pos) -> float {
    for (uint32_t s = 0; s < octaN; ++s) {
      if (glm::length(m_octaShadows.lightPos(s) - pos) < 1e-3f) {
        return float(s + 1);
      }
    }
    return 0.0f;
  };
  auto findSpotSlot = [&](const glm::vec3& pos, const glm::vec3& dir) -> float {
    for (uint32_t s = 0; s < spotN; ++s) {
      if (glm::length(m_octaShadows.spotPos(s) - pos) < 1e-3f &&
          glm::dot(m_octaShadows.spotDir(s), glm::normalize(dir)) > 0.99f) {
        return float(s + 1);
      }
    }
    return 0.0f;
  };

  for (const auto& l : scene.lights) {
    if ((l.type != LightType::Point && l.type != LightType::Spot) || lights.lightCount >= 16) {
      continue;
    }
    const uint32_t i = lights.lightCount++;
    const float typeW = (l.type == LightType::Spot) ? 2.0f : 1.0f;
    lights.lightPosType[i] = glm::vec4(l.position, typeW);
    lights.lightColorIntensity[i] = glm::vec4(l.color, l.intensity);
    float shadowSlot = 0.0f;
    if (l.castShadows) {
      if (l.type == LightType::Point) {
        shadowSlot = findOctaSlot(l.position);
      } else {
        shadowSlot = findSpotSlot(l.position, l.direction);
      }
    }
    lights.lightRangeParams[i] = glm::vec4(l.range, l.innerCone, l.outerCone, shadowSlot);
    lights.lightDirection[i] = glm::vec4(l.direction, 0.0f);
  }
  updateUploadCB(*m_lightCB, &lights, sizeof(lights));

  rhi::Viewport vp{0, 0, float(m_width), float(m_height), 0, 1};
  rhi::Scissor sc{0, 0, int(m_width), int(m_height)};
  cmd->setDescriptorHeap();

  uint32_t objectSlot = 0;
  auto pushObjectCB = [&](const ObjectCBData& ocb) -> uint64_t {
    if (objectSlot >= 4096u) {
      throw std::runtime_error("ObjectCB slots exhausted (need >4096 draws)");
    }
    const uint64_t offset = static_cast<uint64_t>(objectSlot++) * 256ull;
    void* mapped = m_objectCB->mapped();
    if (!mapped) {
      throw std::runtime_error("ObjectCB is not CPU-mapped");
    }
    std::memcpy(static_cast<uint8_t*>(mapped) + offset, &ocb, sizeof(ocb));
    return offset;
  };

  D3D12_CPU_DESCRIPTOR_HANDLE sampCpu[] = {asDxSamp(*m_samplerLinear).cpu};
  const uint32_t sampTable = dxDevice.writeSamplerTable(sampCpu, 1);
  // VisResolve indexes a single frame-global material buffer.  Index zero remains
  // background, so IDs written by the visibility pass are never ambiguous.
  std::vector<DrawMaterialGPU> drawMaterials(1);
  std::vector<uint32_t> objectMaterialBase(scene.objects.size(), 0);
  auto makeDrawMaterial = [&](const std::shared_ptr<Material>& mat) {
    DrawMaterialGPU out{};
    auto* albedo = mat && mat->albedo ? &mat->albedo->resource() : &m_defaultAlbedo->resource();
    auto* normal = mat && mat->normal ? &mat->normal->resource() : &m_defaultNormal->resource();
    auto* orm = mat && mat->metallicRoughness ? &mat->metallicRoughness->resource() : &m_defaultORM->resource();
    auto* emissive = mat && mat->emissive ? &mat->emissive->resource() : &m_defaultBlack->resource();
    auto* detailAlb = mat && mat->detailAlbedo ? &mat->detailAlbedo->resource() : &m_defaultAlbedo->resource();
    auto* detailNrm = mat && mat->detailNormal ? &mat->detailNormal->resource() : &m_defaultNormal->resource();
    out.baseColorFactor = mat ? mat->baseColorFactor : glm::vec4(1);
    out.materialParams = mat ? glm::vec4(mat->metallicFactor, mat->roughnessFactor, mat->aoFactor,
                                         mat->alphaMask ? mat->alphaCutoff : 0.0f)
                             : glm::vec4(0, 1, 1, 0);
    const float f0 = mat ? 0.16f * mat->reflectance * mat->reflectance : 0.04f;
    out.emissiveFactor = mat ? glm::vec4(mat->emissiveFactor, f0) : glm::vec4(0, 0, 0, f0);
    out.textureIndices = {bindlessOf(*albedo), bindlessOf(*normal), bindlessOf(*orm), bindlessOf(*emissive)};
    out.materialExt = mat ? glm::vec4(mat->clearcoat, mat->clearcoatRoughness, mat->fuzz, mat->detailScale) : glm::vec4(0);
    out.textureIndices2 = {bindlessOf(*detailAlb), bindlessOf(*detailNrm), 0, 0};
    out.fuzzColor = mat ? glm::vec4(mat->fuzzColor, 0) : glm::vec4(1);
    return out;
  };
  for (uint32_t oi = 0; oi < scene.objects.size(); ++oi) {
    objectMaterialBase[oi] = static_cast<uint32_t>(drawMaterials.size());
    for (const auto& mat : scene.objects[oi].materials) {
      if (drawMaterials.size() >= 4096) break;
      drawMaterials.push_back(makeDrawMaterial(mat));
    }
    if (objectMaterialBase[oi] == drawMaterials.size() && drawMaterials.size() < 4096) {
      drawMaterials.push_back(makeDrawMaterial(nullptr));
    }
  }
  m_device.uploadBuffer(*m_drawMaterials, drawMaterials.data(), drawMaterials.size() * sizeof(DrawMaterialGPU));
  m_meshletsTotal = 0;
  m_meshletsDrawn = 0;
  uint32_t indirectSlot = 0;

  // ---- Shadow pass (2x2 cascade atlas + toroidal GPU scroll) ----
  rgShadow = [&](rhi::CommandList& c) {
    cmd = &c;
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
          const auto& casc = m_shadowAtlas.cascade(cascade);
          const int sx = casc.lastScrollX;
          const int sy = casc.lastScrollY;
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

          cmd->transition(*m_shadowMap, rhi::ResourceState::CopySrc);
          cmd->transition(*m_shadowScrollTemp, rhi::ResourceState::CopyDst);
          cmd->copyTextureRegion(*m_shadowScrollTemp, dstX, dstY, *m_shadowMap, srcX, srcY, keepW, keepH);
          cmd->transition(*m_shadowScrollTemp, rhi::ResourceState::CopySrc);
          cmd->transition(*m_shadowMap, rhi::ResourceState::CopyDst);
          cmd->copyTextureRegion(*m_shadowMap, ox + dstX, oy + dstY, *m_shadowScrollTemp, dstX, dstY, keepW, keepH);
        }
      }

      cmd->transition(*m_shadowMap, rhi::ResourceState::RenderTarget);
      rhi::Texture* rt = m_shadowMap.get();
      cmd->setRenderTargets(std::span<rhi::Texture*>(&rt, 1), nullptr);
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
        cmd->clearRenderTarget(*m_shadowMap, clear);
      }
      cmd->setRootSignature(*m_root);
      cmd->setPipeline(*m_shadowPSO);
      cmd->setGraphicsRootSrvTable(3, 0);
      cmd->setGraphicsRootSamplerTable(4, sampTable);
      for (int cascade = 0; cascade < 4; ++cascade) {
        if (m_settings.enableToroidalShadows && !m_shadowAtlas.isDirty(cascade)) {
          continue;
        }
        const uint32_t ox = (cascade % 2) * tile;
        const uint32_t oy = (cascade / 2) * tile;
        cmd->setViewport({float(ox), float(oy), float(tile), float(tile), 0, 1});
        cmd->setScissor({int(ox), int(oy), int(ox + tile), int(oy + tile)});
        for (auto& obj : scene.objects) {
          if (!obj.mesh) {
            continue;
          }
          RootXform xform{frame.lightViewProj[cascade], obj.worldMatrix};
          cmd->setGraphicsRootConstants(0, &xform, 32);
          cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
          cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
          cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
          for (const auto& sub : obj.mesh->submeshes()) {
            cmd->drawIndexed(sub.indexCount, sub.indexOffset);
            ++m_drawCalls;
          }
        }
        if (m_settings.enableToroidalShadows) {
          m_shadowAtlas.clearDirty(cascade);
        }
      }
    }
    cmd->transition(*m_shadowMap, rhi::ResourceState::ShaderResource);
    updateUploadCB(*m_frameCB, &frame, sizeof(frame));
  } else {
    cmd->transition(*m_shadowMap, rhi::ResourceState::ShaderResource);
  }

  // ---- Octahedral point + spot shadow atlas ----
  if (m_settings.enableShadows && m_settings.enableOctahedralPointShadows && m_octaShadows.atlas() &&
      (octaN > 0 || spotN > 0) && m_shadowOctaPSO) {
    cmd->transition(*m_octaShadows.atlas(), rhi::ResourceState::RenderTarget);
    rhi::Texture* ort = m_octaShadows.atlas();
    cmd->setRenderTargets(std::span<rhi::Texture*>(&ort, 1), nullptr);
    const float clear[4] = {1, 1, 1, 1};
    cmd->clearRenderTarget(*m_octaShadows.atlas(), clear);
    cmd->setRootSignature(*m_root);
    const uint32_t ts = m_octaShadows.tileSize();

    // True octahedral points
    cmd->setPipeline(*m_shadowOctaPSO);
    for (uint32_t slot = 0; slot < octaN; ++slot) {
      const glm::vec2 off = m_octaShadows.tileUvOffset(slot) * float(m_octaShadows.atlas()->width());
      cmd->setViewport({off.x, off.y, float(ts), float(ts), 0, 1});
      cmd->setScissor({int(off.x), int(off.y), int(off.x) + int(ts), int(off.y) + int(ts)});
      for (auto& obj : scene.objects) {
        if (!obj.mesh) {
          continue;
        }
        RootOctaXform xform{};
        xform.world = obj.worldMatrix;
        xform.lightPosRange = glm::vec4(m_octaShadows.lightPos(slot), m_octaShadows.lightRange(slot));
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
        for (const auto& sub : obj.mesh->submeshes()) {
          cmd->drawIndexed(sub.indexCount, sub.indexOffset);
          ++m_drawCalls;
        }
      }
    }

    // Spot perspective tiles (packed after points)
    cmd->setPipeline(*m_shadowPSO);
    for (uint32_t s = 0; s < spotN; ++s) {
      const uint32_t atlasSlot = m_octaShadows.spotAtlasSlot(s);
      const glm::vec2 off = m_octaShadows.tileUvOffset(atlasSlot) * float(m_octaShadows.atlas()->width());
      cmd->setViewport({off.x, off.y, float(ts), float(ts), 0, 1});
      cmd->setScissor({int(off.x), int(off.y), int(off.x) + int(ts), int(off.y) + int(ts)});
      const glm::mat4 vpL = m_octaShadows.spotViewProj(s);
      for (auto& obj : scene.objects) {
        if (!obj.mesh) {
          continue;
        }
        RootXform xform{vpL, obj.worldMatrix};
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
        for (const auto& sub : obj.mesh->submeshes()) {
          cmd->drawIndexed(sub.indexCount, sub.indexOffset);
          ++m_drawCalls;
        }
      }
    }
    cmd->transition(*m_octaShadows.atlas(), rhi::ResourceState::ShaderResource);
  }

  // ---- Virtual shadow map pages (cascade-0 near field) ----
  if (m_settings.enableShadows && m_settings.enableVSM && m_vsm.physicalAtlas() && m_vsm.pageTable()) {
    m_vsm.beginFrame();
    const float c0Extent = m_settings.enableToroidalShadows ? m_shadowAtlas.split(0) : 5.0f;
    m_vsm.allocateForLightView(frame.lightViewProj[0], sunDir, c0Extent * 2.0f);
    m_vsm.uploadPageTable(m_device);

    cmd->transition(*m_vsm.physicalAtlas(), rhi::ResourceState::RenderTarget);
    rhi::Texture* vrt = m_vsm.physicalAtlas();
    cmd->setRenderTargets(std::span<rhi::Texture*>(&vrt, 1), nullptr);
    const float clearV[4] = {1, 1, 1, 1};
    cmd->clearRenderTarget(*m_vsm.physicalAtlas(), clearV);
    cmd->setRootSignature(*m_root);
    cmd->setPipeline(*m_shadowPSO);
    for (const auto& page : m_vsm.mappedPages()) {
      uint32_t ox = 0, oy = 0;
      m_vsm.physicalViewport(page.physical, ox, oy);
      const float ps = float(m_vsm.pageSize());
      cmd->setViewport({float(ox), float(oy), ps, ps, 0, 1});
      cmd->setScissor({int(ox), int(oy), int(ox + m_vsm.pageSize()), int(oy + m_vsm.pageSize())});
      const glm::mat4 pageVP = m_vsm.pageLightViewProj(page.vx, page.vy);
      for (auto& obj : scene.objects) {
        if (!obj.mesh) {
          continue;
        }
        RootXform xform{pageVP, obj.worldMatrix};
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
        for (const auto& sub : obj.mesh->submeshes()) {
          cmd->drawIndexed(sub.indexCount, sub.indexOffset);
          ++m_drawCalls;
        }
      }
    }
    cmd->transition(*m_vsm.physicalAtlas(), rhi::ResourceState::ShaderResource);
    cmd->transition(*m_vsm.pageTable(), rhi::ResourceState::ShaderResource);
  }
  }; // rgShadow

  // ---- GBuffer / VisBuffer ----
  rgGBuffer = [&](rhi::CommandList& c) {
    cmd = &c;
  const bool gpuCull = m_settings.enableMeshlets && m_settings.enableGpuMeshletCull && m_meshletCullPSO &&
                       m_meshletCompactPSO && m_indirectArgs && m_compactedArgs && m_indirectCount && m_rootCS;
  if (m_settings.enableVisibilityBuffer) {
    cmd->transition(*m_visId, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_visUv, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_visNormal, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_visDepth, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_depth, rhi::ResourceState::DepthWrite);
    rhi::Texture* visRTs[] = {m_visId.get(), m_visUv.get(), m_visNormal.get(), m_visDepth.get()};
    cmd->setRenderTargets(visRTs, m_depth.get());
    const float zc[4] = {0, 0, 0, 0};
    cmd->clearRenderTarget(*m_visId, zc);
    cmd->clearRenderTarget(*m_visUv, zc);
    cmd->clearRenderTarget(*m_visNormal, zc);
    cmd->clearRenderTarget(*m_visDepth, zc);
    cmd->clearDepth(*m_depth, 1.0f);
    cmd->setViewport(vp);
    cmd->setScissor(sc);
    cmd->setRootSignature(*m_root);
    cmd->setPipeline(*m_visBufferPSO);
    for (uint32_t oi = 0; oi < scene.objects.size(); ++oi) {
      auto& obj = scene.objects[oi];
      if (!obj.mesh) {
        continue;
      }
      const bool meshletObject = m_settings.enableMeshlets && obj.mesh->meshletGpuBuffer() &&
                                 obj.mesh->meshletIndexBuffer() && obj.mesh->meshletCount() != 0;
      if (meshletObject && m_settings.enableMeshShaders && m_device.supportsMeshShaders() && m_meshletMeshPSO) {
        ObjectCBData ocb{};
        ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
        ocb.textureIndices = {objectMaterialBase[oi], 0, 0, 0};
        const uint64_t off = pushObjectCB(ocb);
        D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {
            asDxBuf(*obj.mesh->meshletGpuBuffer()).srvCpu, asDxBuf(*obj.mesh->meshPackedIndices()).srvCpu,
            asDxBuf(*obj.mesh->meshPositions()).srvCpu, asDxBuf(*obj.mesh->meshUVs()).srvCpu,
            asDxBuf(*obj.mesh->meshNormals()).srvCpu};
        RootXform xform{frame.viewProj, obj.worldMatrix};
        cmd->setPipeline(*m_meshletMeshPSO);
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setGraphicsRootCBV(2, *m_objectCB, off);
        cmd->setGraphicsRootSrvTable(5, dxDevice.writeSrvTable(srvs, 5));
        cmd->dispatchMesh(obj.mesh->meshletCount(), 1, 1);
        ++m_drawCalls;
        m_meshletsTotal += obj.mesh->meshletCount();
        m_meshletsDrawn += obj.mesh->meshletCount();
        cmd->setPipeline(*m_visBufferPSO);
        continue;
      }
      if (meshletObject && gpuCull) {
        struct CullCBData {
          glm::mat4 viewProj;
          glm::mat4 world;
          glm::vec4 camPos;
          uint32_t meshletCount, argsOffset, enableHiZ, hizMipCount;
          glm::vec2 screenSize;
          glm::vec2 pad;
        } ccb{frame.viewProj, obj.worldMatrix, frame.cameraPos, obj.mesh->meshletCount(), 0,
              m_settings.enableHiZOcclusion ? 1u : 0u, static_cast<uint32_t>(m_hizMips.size()),
              glm::vec2(m_width, m_height), {}};
        D3D12_CPU_DESCRIPTOR_HANDLE cullSrvs[] = {
            asDxBuf(*obj.mesh->meshletGpuBuffer()).srvCpu, asDxTex(*m_hizMips[2]).srvCpu};
        D3D12_CPU_DESCRIPTOR_HANDLE cullUavs[] = {asDxBuf(*m_indirectArgs).uavCpu};
        updateUploadCB(*m_meshletCullCB, &ccb, sizeof(ccb));
        cmd->setRootSignature(*m_rootCS);
        cmd->setPipeline(*m_meshletCullPSO);
        cmd->transition(*m_indirectArgs, rhi::ResourceState::UnorderedAccess);
        cmd->setComputeRootCBV(1, *m_meshletCullCB);
        cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(cullSrvs, 2));
        cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(cullUavs, 1));
        cmd->dispatch((obj.mesh->meshletCount() + 63u) / 64u, 1, 1);
        struct CompactCBData { uint32_t srcCount, srcOffset, clearOnly, pad; } compact{obj.mesh->meshletCount(), 0, 1, 0};
        D3D12_CPU_DESCRIPTOR_HANDLE compactSrvs[] = {asDxBuf(*m_indirectArgs).srvCpu};
        D3D12_CPU_DESCRIPTOR_HANDLE compactUavs[] = {asDxBuf(*m_compactedArgs).uavCpu, asDxBuf(*m_indirectCount).uavCpu};
        cmd->setPipeline(*m_meshletCompactPSO);
        updateUploadCB(*m_meshletCullCB, &compact, sizeof(compact));
        cmd->setComputeRootCBV(1, *m_meshletCullCB);
        cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(compactSrvs, 1));
        cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(compactUavs, 2));
        cmd->dispatch(1, 1, 1);
        compact.clearOnly = 0;
        updateUploadCB(*m_meshletCullCB, &compact, sizeof(compact));
        cmd->dispatch((compact.srcCount + 63u) / 64u, 1, 1);
        cmd->transition(*m_compactedArgs, rhi::ResourceState::IndirectArgument);
        cmd->transition(*m_indirectCount, rhi::ResourceState::IndirectArgument);
        ObjectCBData ocb{};
        ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
        ocb.textureIndices = {objectMaterialBase[oi], 1, 0, 0};
        const uint64_t off = pushObjectCB(ocb);
        RootXform xform{frame.viewProj, obj.worldMatrix};
        D3D12_CPU_DESCRIPTOR_HANDLE meshletSrv[] = {asDxBuf(*obj.mesh->meshletGpuBuffer()).srvCpu};
        cmd->setRootSignature(*m_root);
        cmd->setPipeline(*m_visBufferPSO);
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setGraphicsRootCBV(2, *m_objectCB, off);
        cmd->setGraphicsRootSrvTable(5, dxDevice.writeSrvTable(meshletSrv, 1));
        cmd->setGraphicsRootSamplerTable(4, sampTable);
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(*obj.mesh->meshletIndexBuffer(), true);
        cmd->drawIndexedIndirectCount(*m_compactedArgs, 0, *m_indirectCount, 0, obj.mesh->meshletCount());
        ++m_drawCalls;
        m_meshletsTotal += obj.mesh->meshletCount();
        continue;
      }
      for (const auto& sub : obj.mesh->submeshes()) {
        ObjectCBData ocb{};
        ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
        ocb.baseColorFactor = glm::vec4(1);
        ocb.materialParams = {0, 1, 1, 0};
        ocb.emissiveFactor = glm::vec4(0);
        ocb.textureIndices = glm::uvec4(objectMaterialBase[oi] + sub.materialIndex, 0, 0, 0);
        const uint64_t off = pushObjectCB(ocb);
        RootXform xform{frame.viewProj, obj.worldMatrix};
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setGraphicsRootCBV(2, *m_objectCB, off);
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
        cmd->drawIndexed(sub.indexCount, sub.indexOffset);
        ++m_drawCalls;
      }
    }
    cmd->transition(*m_visId, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visUv, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visNormal, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visDepth, rhi::ResourceState::ShaderResource);
  }

  cmd->transition(*m_albedo, rhi::ResourceState::RenderTarget);
  cmd->transition(*m_normal, rhi::ResourceState::RenderTarget);
  cmd->transition(*m_orm, rhi::ResourceState::RenderTarget);
  cmd->transition(*m_emissive, rhi::ResourceState::RenderTarget);
  cmd->transition(*m_depthColor, rhi::ResourceState::RenderTarget);
  cmd->transition(*m_depth, rhi::ResourceState::DepthWrite);

  if (!m_settings.enableVisibilityBuffer) {
    bool drewGpuGBuffer = false;
    if (gpuCull && m_gbufferMeshletPSO && m_settings.enableMeshletCompact) {
      cmd->setDescriptorHeap();
      bool cleared = false;
      for (uint32_t oi = 0; oi < scene.objects.size(); ++oi) {
        auto& obj = scene.objects[oi];
        if (!obj.mesh || !obj.mesh->meshletGpuBuffer() || obj.mesh->meshletCount() == 0) {
          continue;
        }
        struct CullCBData {
          glm::mat4 viewProj;
          glm::mat4 world;
          glm::vec4 camPos;
          uint32_t meshletCount, argsOffset, enableHiZ, hizMipCount;
          glm::vec2 screenSize;
          glm::vec2 pad;
        } ccb{frame.viewProj,
              obj.worldMatrix,
              frame.cameraPos,
              obj.mesh->meshletCount(),
              0,
              m_settings.enableHiZOcclusion ? 1u : 0u,
              static_cast<uint32_t>(m_hizMips.size()),
              glm::vec2(float(m_width), float(m_height)),
              {}};
        D3D12_CPU_DESCRIPTOR_HANDLE cullSrvs[] = {asDxBuf(*obj.mesh->meshletGpuBuffer()).srvCpu,
                                                   asDxTex(*m_hizMips[2]).srvCpu};
        D3D12_CPU_DESCRIPTOR_HANDLE cullUavs[] = {asDxBuf(*m_indirectArgs).uavCpu};
        updateUploadCB(*m_meshletCullCB, &ccb, sizeof(ccb));
        cmd->setRootSignature(*m_rootCS);
        cmd->setPipeline(*m_meshletCullPSO);
        cmd->transition(*m_indirectArgs, rhi::ResourceState::UnorderedAccess);
        cmd->setComputeRootCBV(1, *m_meshletCullCB);
        cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(cullSrvs, 2));
        cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(cullUavs, 1));
        cmd->dispatch((obj.mesh->meshletCount() + 63u) / 64u, 1, 1);

        struct CompactCBData {
          uint32_t srcCount, srcOffset, clearOnly, pad;
        } compact{obj.mesh->meshletCount(), 0, 1, 0};
        D3D12_CPU_DESCRIPTOR_HANDLE compactSrvs[] = {asDxBuf(*m_indirectArgs).srvCpu};
        D3D12_CPU_DESCRIPTOR_HANDLE compactUavs[] = {asDxBuf(*m_compactedArgs).uavCpu,
                                                     asDxBuf(*m_indirectCount).uavCpu};
        cmd->setPipeline(*m_meshletCompactPSO);
        updateUploadCB(*m_meshletCullCB, &compact, sizeof(compact));
        cmd->setComputeRootCBV(1, *m_meshletCullCB);
        cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(compactSrvs, 1));
        cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(compactUavs, 2));
        cmd->dispatch(1, 1, 1);
        compact.clearOnly = 0;
        updateUploadCB(*m_meshletCullCB, &compact, sizeof(compact));
        cmd->dispatch((compact.srcCount + 63u) / 64u, 1, 1);
        cmd->transition(*m_compactedArgs, rhi::ResourceState::IndirectArgument);
        cmd->transition(*m_indirectCount, rhi::ResourceState::IndirectArgument);

        std::vector<DrawMaterialGPU> localMats;
        for (const auto& mat : obj.materials) {
          localMats.push_back(makeDrawMaterial(mat));
        }
        if (localMats.empty()) {
          localMats.push_back(makeDrawMaterial(nullptr));
        }
        m_device.uploadBuffer(*m_drawMaterials, localMats.data(), localMats.size() * sizeof(DrawMaterialGPU));

        ObjectCBData ocb{};
        ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
        const uint64_t off = pushObjectCB(ocb);
        RootXform xform{frame.viewProj, obj.worldMatrix};
        D3D12_CPU_DESCRIPTOR_HANDLE space1[] = {asDxBuf(*m_drawMaterials).srvCpu,
                                                asDxBuf(*obj.mesh->meshletGpuBuffer()).srvCpu};
        cmd->setRootSignature(*m_root);
        cmd->setPipeline(*m_gbufferMeshletPSO);
        cmd->setGraphicsRootConstants(0, &xform, 32);
        cmd->setGraphicsRootCBV(2, *m_objectCB, off);
        cmd->setGraphicsRootSrvTable(3, 0);
        cmd->setGraphicsRootSamplerTable(4, sampTable);
        cmd->setGraphicsRootSrvTable(5, dxDevice.writeSrvTable(space1, 2));
        cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
        cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
        cmd->setIndexBuffer(*obj.mesh->meshletIndexBuffer(), true);
        rhi::Texture* gbufferRTs[] = {m_albedo.get(), m_normal.get(), m_orm.get(), m_emissive.get(),
                                      m_depthColor.get()};
        cmd->setRenderTargets(gbufferRTs, m_depth.get());
        cmd->setViewport(vp);
        cmd->setScissor(sc);
        if (!cleared) {
          const float zclear[4] = {0, 0, 0, 0};
          cmd->clearRenderTarget(*m_albedo, zclear);
          cmd->clearRenderTarget(*m_normal, zclear);
          cmd->clearRenderTarget(*m_orm, zclear);
          cmd->clearRenderTarget(*m_emissive, zclear);
          cmd->clearRenderTarget(*m_depthColor, zclear);
          cmd->clearDepth(*m_depth, 1.0f);
          cleared = true;
        }
        cmd->drawIndexedIndirectCount(*m_compactedArgs, 0, *m_indirectCount, 0, obj.mesh->meshletCount());
        ++m_drawCalls;
        m_meshletsTotal += obj.mesh->meshletCount();
        m_meshletsDrawn += obj.mesh->meshletCount();
        drewGpuGBuffer = true;
      }
    }
    if (!drewGpuGBuffer) {
      GBufferPassContext gb{
          m_device,
          *cmd,
          *m_root,
          *m_gbufferPSO,
          *m_objectCB,
          *m_albedo,
          *m_normal,
          *m_orm,
          *m_emissive,
          *m_depthColor,
          *m_depth,
          m_defaultAlbedo->resource(),
          m_defaultNormal->resource(),
          m_defaultORM->resource(),
          m_defaultBlack->resource(),
          *m_samplerLinear,
          scene,
          frame.viewProj,
          vp,
          sc,
          true,
          sampTable,
          [&](const void* data, size_t size) -> uint64_t {
            if (objectSlot >= 4096u) {
              throw std::runtime_error("ObjectCB slots exhausted (need >4096 draws)");
            }
            const uint64_t offset = static_cast<uint64_t>(objectSlot++) * 256ull;
            void* mapped = m_objectCB->mapped();
            if (!mapped) {
              throw std::runtime_error("ObjectCB is not CPU-mapped");
            }
            std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
            return offset;
          },
          &m_drawCalls,
          &m_meshletsTotal,
          &m_meshletsDrawn,
          &m_visibleMeshlets,
          m_settings.enableMeshlets,
          false,
          nullptr,
          nullptr};
      executeGBufferPass(gb);
    }
  }

  if (m_settings.enableVisibilityBuffer) {
    cmd->transition(*m_visId, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visUv, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visNormal, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_visDepth, rhi::ResourceState::ShaderResource);
    struct Phase3CB {
      glm::mat4 invViewProj;
      glm::vec4 cameraPos;
      glm::vec4 screenSize;
      glm::vec4 params;
      glm::vec4 volumeOriginExtent;
      glm::vec4 iblParams;
      glm::uvec4 texIds0;
      glm::uvec4 texIds1;
      glm::uvec4 texIds2;
    } p3{};
    p3.invViewProj = frame.invViewProj;
    p3.cameraPos = frame.cameraPos;
    p3.screenSize = frame.screenSize;
    p3.texIds0 = {bindlessOf(*m_visId), bindlessOf(*m_visUv), bindlessOf(*m_visNormal), bindlessOf(*m_visDepth)};
    updateUploadCB(*m_phase3CB, &p3, sizeof(p3));
    cmd->setRootSignature(*m_rootFS);
    cmd->transition(*m_albedo, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_normal, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_orm, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_emissive, rhi::ResourceState::RenderTarget);
    cmd->transition(*m_depthColor, rhi::ResourceState::RenderTarget);
    rhi::Texture* gbufferRTs[] = {m_albedo.get(), m_normal.get(), m_orm.get(), m_emissive.get(), m_depthColor.get()};
    cmd->setRenderTargets(gbufferRTs, nullptr);
    cmd->setPipeline(*m_visResolvePSO);
    D3D12_CPU_DESCRIPTOR_HANDLE mats[] = {asDxBuf(*m_drawMaterials).srvCpu};
    cmd->setGraphicsRootCBV(1, *m_phase3CB);
    cmd->setGraphicsRootSrvTable(3, 0);
    cmd->setGraphicsRootSrvTable(5, dxDevice.writeSrvTable(mats, 1));
    cmd->setGraphicsRootSamplerTable(4, sampTable);
    cmd->draw(3);
    ++m_drawCalls;
  }

  cmd->transition(*m_albedo, rhi::ResourceState::ShaderResource);
  cmd->transition(*m_normal, rhi::ResourceState::ShaderResource);
  cmd->transition(*m_orm, rhi::ResourceState::ShaderResource);
  cmd->transition(*m_emissive, rhi::ResourceState::ShaderResource);
  cmd->transition(*m_depthColor, rhi::ResourceState::ShaderResource);

  // ---- Deferred rain GBuffer (Cry DeferredRainGBuffer) ----
  m_timeSeconds += std::max(0.001f, m_lastFrameMs) * 0.001f;
  m_rain.executeDeferredGBuffer(*cmd, m_device, scene, *m_albedo, *m_normal, *m_orm, *m_depthColor, *m_rainCB,
                                *m_samplerLinear, frame.invViewProj, frame.viewProj, frame.view, eye,
                                m_timeSeconds, m_width, m_height);
  }; // rgGBuffer

  // ---- AO (GTAO + bilateral) ----
  rgAO = [&](rhi::CommandList& c) {
    cmd = &c;
  if (m_settings.enableAO) {
    AOPassContext aoCtx{
        m_device, *cmd, *m_rootFS, *m_aoPSO, *m_aoBlurPSO, *m_postCB, m_postCBBump, *m_ao, *m_aoTemp, *m_depthColor,
        *m_normal, *m_samplerLinear, m_width, m_height, m_settings.aoIntensity, m_settings.aoRadius, 0.25f, 1.4f};
    executeAOPass(aoCtx);
  }
  }; // rgAO

  // ---- Lighting ----
  rgLighting = [&](rhi::CommandList& c) {
    cmd = &c;

    // Ray Query sun shadow mask (half-res) when DXR ready.
    const bool useRtShadows = m_settings.enableRTShadows && m_rtScene.ready() && m_rtShadowsPSO && m_rtShadowMask &&
                              m_rtCB && m_rtScene.tlas();
    if (useRtShadows) {
      struct RTCB {
        glm::mat4 invViewProj;
        glm::vec4 cameraPos;
        glm::vec4 screenSize;
        glm::vec4 sunDirIntensity;
        glm::vec4 params;
        glm::uvec4 texIds;
      } rcb{frame.invViewProj,
            frame.cameraPos,
            frame.screenSize,
            frame.sunDirectionIntensity,
            glm::vec4(0.02f, 80.0f, 2.0f, 0.0f),
            {bindlessOf(*m_depthColor), bindlessOf(*m_normal), 0, 0}};
      updateUploadCB(*m_rtCB, &rcb, sizeof(rcb));
      cmd->transition(*m_rtShadowMask, rhi::ResourceState::UnorderedAccess);
      cmd->setDescriptorHeap();
      cmd->setRootSignature(*m_rootCS);
      cmd->setPipeline(*m_rtShadowsPSO);
      cmd->setComputeRootCBV(1, *m_rtCB);
      cmd->setComputeRootSrvTable(2, 0);
      cmd->setComputeRootUavTable(3, uavOf(*m_rtShadowMask));
      cmd->setComputeRootSamplerTable(4, sampTable);
      cmd->setComputeRootAccelerationStructure(5, *m_rtScene.tlas());
      const uint32_t mw = m_rtShadowMask->width();
      const uint32_t mh = m_rtShadowMask->height();
      cmd->dispatch((mw + 7) / 8, (mh + 7) / 8, 1);
      cmd->uavBarrier(m_rtShadowMask.get());
      cmd->transition(*m_rtShadowMask, rhi::ResourceState::ShaderResource);
    }

  {
    const bool useOcta = m_settings.enableOctahedralPointShadows && m_octaShadows.atlas() && (octaN > 0 || spotN > 0);
    const bool useVsm = m_settings.enableVSM && m_vsm.physicalAtlas() && m_vsm.pageTable() && m_vsm.mappedCount() > 0;
    LightingPassContext lctx{m_device,
                             *cmd,
                             *m_rootFS,
                             *m_lightingPSO,
                             *m_frameCB,
                             *m_lightCB,
                             *m_hdr,
                             *m_albedo,
                             *m_normal,
                             *m_orm,
                             *m_emissive,
                             *m_depthColor,
                             *m_shadowMap,
                             *m_brdfLUT,
                             *m_irradiance,
                             *m_prefiltered,
                             *m_ao,
                             useOcta ? m_octaShadows.atlas() : nullptr,
                             useVsm ? m_vsm.physicalAtlas() : nullptr,
                             useVsm ? m_vsm.pageTable() : nullptr,
                             useRtShadows ? m_rtShadowMask.get() : nullptr,
                             *m_samplerLinear,
                             *m_samplerShadow,
                             frame.invViewProj,
                             frame.cameraPos,
                             frame.sunDirectionIntensity,
                             frame.sunColor,
                             frame.ambientColor,
                             {},
                             frame.cascadeSplits,
                             frame.flags,
                             frame.screenSize,
                             m_iblMaxMip,
                             m_iblExposure,
                             glm::vec4(m_settings.pcssLightSize, m_settings.esmExponent,
                                       m_settings.enableOctahedralPointShadows ? 1.f : 0.f,
                                       m_settings.enablePCSS ? 1.f : 0.f),
                             glm::vec4(useVsm ? 1.f : 0.f, float(m_vsm.pagesPerAxis()), float(m_vsm.physicalGrid()),
                                       0.f),
                             vp,
                             sc,
                             &m_drawCalls};
    for (int i = 0; i < 4; ++i) {
      lctx.lightViewProj[i] = frame.lightViewProj[i];
    }
    executeLightingPass(lctx);
  }
  }; // rgLighting

  // ---- Phase 3 + Post (owned by RG; lambdas below) ----
  struct Phase3CBData {
    glm::mat4 invViewProj;
    glm::mat4 viewProj;
    glm::vec4 cameraPos;
    glm::vec4 screenSize;
    glm::vec4 params;
    glm::vec4 volumeOriginExtent;
    glm::vec4 iblParams;
    glm::uvec4 texIds0;
    glm::uvec4 texIds1;
    glm::uvec4 texIds2;
    glm::vec4 sdfC0;
    glm::vec4 sdfC1;
    glm::vec4 sdfC2;
    glm::vec4 sdfC3;
    glm::mat4 prevViewProj;
    glm::uvec4 texIds3;
    glm::vec4 probePosIntensity[4];
    glm::vec4 probeBoxMin[4];
    glm::vec4 probeBoxMax[4];
  } p3cb{};
  p3cb.invViewProj = frame.invViewProj;
  p3cb.viewProj = frame.viewProj;
  p3cb.cameraPos = frame.cameraPos;
  p3cb.screenSize = frame.screenSize;
  p3cb.params = glm::vec4(0.55f, 0.0025f, 32.0f, static_cast<float>(m_settings.giTier));
  p3cb.volumeOriginExtent = glm::vec4(frame.cameraPos.x - 12.f, 0.0f, frame.cameraPos.z - 12.f, 24.0f);
  // z=probeY (normalized in volume), w=temporal alpha (base; shader scales by motion)
  p3cb.iblParams = glm::vec4(m_iblMaxMip, m_iblExposure, 0.35f, 0.12f);
  p3cb.prevViewProj = m_hasPrevCamera ? m_prevViewProj : frame.viewProj;
  {
    glm::vec4 cascades[4];
    m_worldSdf.fillCascadeCB(cascades);
    p3cb.sdfC0 = cascades[0];
    p3cb.sdfC1 = cascades[1];
    p3cb.sdfC2 = cascades[2];
    p3cb.sdfC3 = cascades[3];
  }
  {
    ReflectionProbes::ProbeGPU probes[ReflectionProbes::kMaxProbes];
    m_reflectionProbes.fillProbeCB(probes);
    for (uint32_t i = 0; i < 4; ++i) {
      p3cb.probePosIntensity[i] = probes[i].posIntensity;
      p3cb.probeBoxMin[i] = probes[i].boxMin;
      p3cb.probeBoxMax[i] = probes[i].boxMax;
    }
  }

  auto fillPhase3Ids = [&](rhi::Texture& historyOrBlack, rhi::Texture& ssrOrAtlas) {
    p3cb.texIds0 = {bindlessOf(*m_depthColor), bindlessOf(*m_normal), bindlessOf(*m_hdr), bindlessOf(*m_orm)};
    p3cb.texIds1 = {bindlessOf(*m_albedo), bindlessOf(historyOrBlack), bindlessOf(ssrOrAtlas),
                    bindlessOf(*m_brdfLUT)};
    const uint32_t sdfId = (m_worldSdf.sdfAtlas() && m_settings.enableVoxelGI) ? bindlessOf(*m_worldSdf.sdfAtlas()) : 0;
    const uint32_t shId = (m_worldSdf.shAtlas() && m_settings.enableVoxelGI) ? bindlessOf(*m_worldSdf.shAtlas()) : 0;
    p3cb.texIds2 = {bindlessOf(*m_prefiltered), bindlessOf(*m_visId), sdfId, shId};
    const uint32_t prevDepthId =
        (m_depthHistory && m_hasPrevCamera) ? bindlessOf(*m_depthHistory) : bindlessOf(*m_depthColor);
    const uint32_t hiz0 = m_hizMips[0] ? bindlessOf(*m_hizMips[0]) : 0;
    const uint32_t hiz2 = m_hizMips[2] ? bindlessOf(*m_hizMips[2]) : hiz0;
    const uint32_t probeId = m_reflectionProbes.atlas() ? bindlessOf(*m_reflectionProbes.atlas()) : 0;
    p3cb.texIds3 = {prevDepthId, hiz0, hiz2, probeId};
  };
  auto bindPhase3 = [&](rhi::CommandList& c, rhi::Texture& historyOrBlack, rhi::Texture& ssrOrAtlas) {
    fillPhase3Ids(historyOrBlack, ssrOrAtlas);
    updateUploadCB(*m_phase3CB, &p3cb, sizeof(p3cb));
    c.setGraphicsRootCBV(1, *m_phase3CB);
    c.setGraphicsRootSrvTable(3, 0);
    c.setGraphicsRootSamplerTable(4, sampTable);
  };

  bool ddgiSampled = false;

  rgSSGI = [&](rhi::CommandList& c) {
    cmd = &c;
    if (wantGi) {
      rhi::Texture* ssgiRT = m_ssgi.get();
      cmd->setRenderTargets(std::span<rhi::Texture*>(&ssgiRT, 1), nullptr);
      const float clearGi[4] = {0, 0, 0, 0};
      cmd->clearRenderTarget(*m_ssgi, clearGi);
      cmd->setRootSignature(*m_rootFS);
      cmd->setPipeline(*m_ssgiPSO);
      bindPhase3(*cmd, *m_ssgiHistory, *m_ssr);
      cmd->draw(3);
      ++m_drawCalls;
    } else {
      rhi::Texture* ssgiRT = m_ssgi.get();
      cmd->setRenderTargets(std::span<rhi::Texture*>(&ssgiRT, 1), nullptr);
      const float z[4] = {0, 0, 0, 0};
      cmd->clearRenderTarget(*m_ssgi, z);
    }
  };

  rgDDGI = [&](rhi::CommandList& c) {
    // Runs on async compute when asyncHint is set (RG batches + fence).
    fillPhase3Ids(*m_hdr, *m_ddgiAtlasPrev);
    p3cb.texIds0 = {bindlessOf(*m_hdr), bindlessOf(*m_depthColor), bindlessOf(*m_normal),
                    bindlessOf(*m_ddgiAtlasPrev)};
    updateUploadCB(*m_phase3CB, &p3cb, sizeof(p3cb));
    c.setDescriptorHeap();
    c.setRootSignature(*m_rootCS);
    c.setPipeline(*m_ddgiUpdatePSO);
    c.setComputeRootCBV(1, *m_phase3CB);
    c.setComputeRootSrvTable(2, 0);
    c.setComputeRootUavTable(3, uavOf(*m_ddgiAtlas));
    c.setComputeRootSamplerTable(4, sampTable);
    c.dispatch((128 + 7) / 8, (128 + 7) / 8, 1);
    ++m_drawCalls;
    if (m_settings.giTier < GITier::Medium) {
      std::swap(m_ddgiAtlas, m_ddgiAtlasPrev);
    }
  };

  rgDDGISample = [&](rhi::CommandList& c) {
    cmd = &c;
    // Blend octahedral DDGI on top of current SSGI into history RT (avoid read/write hazard).
    rhi::Texture* outRT = m_ssgiHistory.get();
    cmd->setRenderTargets(std::span<rhi::Texture*>(&outRT, 1), nullptr);
    cmd->setRootSignature(*m_rootFS);
    cmd->setPipeline(*m_ddgiSamplePSO);
    bindPhase3(*cmd, *m_ssgi, *m_ddgiAtlas);
    cmd->draw(3);
    ++m_drawCalls;
    std::swap(m_ddgiAtlas, m_ddgiAtlasPrev);
    ddgiSampled = true;
  };

  rgSSR = [&](rhi::CommandList& c) {
    cmd = &c;
    if (wantSsr) {
      rhi::Texture* ssrRT = m_ssr.get();
      cmd->setRenderTargets(std::span<rhi::Texture*>(&ssrRT, 1), nullptr);
      const float clearSsr[4] = {0, 0, 0, 0};
      cmd->clearRenderTarget(*m_ssr, clearSsr);
      cmd->setRootSignature(*m_rootFS);
      cmd->setPipeline(*m_ssrPSO);
      bindPhase3(*cmd, m_defaultBlack->resource(), *m_ssr);
      cmd->draw(3);
      ++m_drawCalls;
    } else if (wantPhase3) {
      rhi::Texture* ssrRT = m_ssr.get();
      cmd->setRenderTargets(std::span<rhi::Texture*>(&ssrRT, 1), nullptr);
      const float z[4] = {0, 0, 0, 0};
      cmd->clearRenderTarget(*m_ssr, z);
    }

    // HW RT reflections overwrite SSR for glossy/metal (miss keeps alpha≈0 → probes/IBL).
    if (m_settings.enableRTReflections && m_rtScene.ready() && m_rtReflectionsPSO && m_rtCB && m_rtScene.tlas()) {
      struct RTCB {
        glm::mat4 invViewProj;
        glm::vec4 cameraPos;
        glm::vec4 screenSize;
        glm::vec4 sunDirIntensity;
        glm::vec4 params;
        glm::uvec4 texIds;
        glm::vec4 skyTint;
      } rcb{frame.invViewProj,
            frame.cameraPos,
            frame.screenSize,
            frame.sunDirectionIntensity,
            glm::vec4(0.015f, 60.0f, 0.45f, 0.35f),
            {bindlessOf(*m_depthColor), bindlessOf(*m_normal), bindlessOf(*m_orm), bindlessOf(*m_albedo)},
            glm::vec4(0.15f, 0.22f, 0.35f, 1.0f)};
      updateUploadCB(*m_rtCB, &rcb, sizeof(rcb));
      cmd->transition(*m_ssr, rhi::ResourceState::UnorderedAccess);
      cmd->setDescriptorHeap();
      cmd->setRootSignature(*m_rootCS);
      cmd->setPipeline(*m_rtReflectionsPSO);
      cmd->setComputeRootCBV(1, *m_rtCB);
      cmd->setComputeRootSrvTable(2, 0);
      cmd->setComputeRootUavTable(3, uavOf(*m_ssr));
      cmd->setComputeRootSamplerTable(4, sampTable);
      cmd->setComputeRootAccelerationStructure(5, *m_rtScene.tlas());
      cmd->dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
      cmd->uavBarrier(m_ssr.get());
      cmd->transition(*m_ssr, rhi::ResourceState::ShaderResource);
    }
  };

  rgCompose = [&](rhi::CommandList& c) {
    cmd = &c;
    if (!wantPhase3) {
      postHdr = m_hdr.get();
      return;
    }
    rhi::Texture* giSrc = ddgiSampled ? m_ssgiHistory.get() : m_ssgi.get();
    rhi::Texture* compRT = m_hdrCompose.get();
    cmd->setRenderTargets(std::span<rhi::Texture*>(&compRT, 1), nullptr);
    cmd->setRootSignature(*m_rootFS);
    cmd->setPipeline(*m_composePSO);
    bindPhase3(*cmd, *giSrc, *m_ssr);
    cmd->draw(3);
    ++m_drawCalls;
    postHdr = m_hdrCompose.get();
    // Keep history for next-frame temporal SSGI.
    if (!ddgiSampled) {
      std::swap(m_ssgi, m_ssgiHistory);
    }
    // If ddgiSampled, m_ssgiHistory already holds final GI for next SSGI read.
  };

  rgContact = [&](rhi::CommandList& c) {
    cmd = &c;
    if (!m_settings.enableContactShadows) {
      return;
    }

    const bool useRtContact = m_settings.enableRTShadows && m_rtScene.ready() && m_rtContactPSO && m_rtCB &&
                              m_rtScene.tlas();
    if (useRtContact) {
      rhi::Texture* contactSrc = postHdr;
      rhi::Texture* contactDst = (postHdr == m_hdr.get()) ? m_hdrCompose.get() : m_hdr.get();
      struct RTCB {
        glm::mat4 invViewProj;
        glm::vec4 cameraPos;
        glm::vec4 screenSize;
        glm::vec4 sunDirIntensity;
        glm::vec4 params;
        glm::uvec4 texIds;
      } rcb{frame.invViewProj,
            frame.cameraPos,
            frame.screenSize,
            frame.sunDirectionIntensity,
            glm::vec4(1.5f, 0.45f, 0.02f, 0.0f),
            {bindlessOf(*m_depthColor), bindlessOf(*contactSrc), 0, 0}};
      updateUploadCB(*m_rtCB, &rcb, sizeof(rcb));
      cmd->transition(*contactSrc, rhi::ResourceState::ShaderResource);
      cmd->transition(*contactDst, rhi::ResourceState::UnorderedAccess);
      cmd->setDescriptorHeap();
      cmd->setRootSignature(*m_rootCS);
      cmd->setPipeline(*m_rtContactPSO);
      cmd->setComputeRootCBV(1, *m_rtCB);
      cmd->setComputeRootSrvTable(2, 0);
      cmd->setComputeRootUavTable(3, uavOf(*contactDst));
      cmd->setComputeRootSamplerTable(4, sampTable);
      cmd->setComputeRootAccelerationStructure(5, *m_rtScene.tlas());
      cmd->dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
      cmd->uavBarrier(contactDst);
      cmd->transition(*contactDst, rhi::ResourceState::ShaderResource);
      postHdr = contactDst;
      return;
    }

    if (!m_contactShadowPSO) {
      return;
    }
    struct ContactCB {
      glm::mat4 invViewProj;
      glm::vec4 cameraPos;
      glm::vec4 screenSize;
      glm::vec4 params;
      glm::vec4 sunDir;
      glm::uvec4 texIds;
    } ccb{frame.invViewProj,
          frame.cameraPos,
          frame.screenSize,
          glm::vec4(1.5f, 8.0f, 0.08f, 0.45f),
          frame.sunDirectionIntensity,
          {}};
    rhi::Texture* contactSrc = postHdr;
    rhi::Texture* contactDst = (postHdr == m_hdr.get()) ? m_hdrCompose.get() : m_hdr.get();
    ccb.texIds = {bindlessOf(*m_depthColor), bindlessOf(*contactSrc), 0, 0};
    const uint64_t ccbAligned = (sizeof(ccb) + 255ull) & ~255ull;
    if (m_postCBBump + ccbAligned > m_postCB->size()) {
      m_postCBBump = 0;
    }
    std::memcpy(static_cast<char*>(m_postCB->mapped()) + m_postCBBump, &ccb, sizeof(ccb));
    const uint64_t ccbOff = m_postCBBump;
    m_postCBBump += ccbAligned;
    cmd->setRenderTargets(std::span<rhi::Texture*>(&contactDst, 1), nullptr);
    cmd->setRootSignature(*m_rootFS);
    cmd->setPipeline(*m_contactShadowPSO);
    cmd->setGraphicsRootCBV(1, *m_postCB, ccbOff);
    cmd->setGraphicsRootSrvTable(3, 0);
    cmd->setGraphicsRootSamplerTable(4, sampTable);
    cmd->setViewport(vp);
    cmd->setScissor(sc);
    cmd->draw(3);
    ++m_drawCalls;
    postHdr = contactDst;
  };

  rgRain = [&](rhi::CommandList& c) {
    cmd = &c;
    rhi::Texture* rainSrc = postHdr;
    rhi::Texture* rainTmp = (postHdr == m_hdr.get()) ? m_hdrCompose.get() : m_hdr.get();
    rhi::Texture* rainOut = m_rain.executePost(
        *cmd, m_device, *rainSrc, *rainTmp, *m_depthColor, *m_normal, *rainSrc, *m_rainCB, *m_samplerLinear,
        frame.invViewProj, frame.viewProj, frame.view, eye, m_timeSeconds, m_width, m_height, m_ssr.get());
    if (rainOut) {
      postHdr = rainOut;
    }
  };

  rgBloom = [&](rhi::CommandList& c) {
    cmd = &c;
    rhi::Texture& bloomSrcTex = (postHdr == m_hdrCompose.get()) ? *m_hdrCompose : *m_hdr;
    BloomPassContext bctx{m_device,
                          *cmd,
                          *m_rootFS,
                          *m_bloomDownPSO,
                          *m_bloomUpPSO,
                          *m_postCB,
                          m_postCBBump,
                          bloomSrcTex,
                          m_bloomMips,
                          m_bloomScratch.get(),
                          *m_samplerLinear,
                          vp,
                          sc,
                          4};
    executeBloomPass(bctx);
  };

  rgExposure = [&](rhi::CommandList& c) {
    cmd = &c;
    ExposurePassContext ectx{m_device,
                             *cmd,
                             *m_rootCS,
                             *m_exposureClearPSO,
                             *m_exposureBuildPSO,
                             *m_exposureReducePSO,
                             *m_exposureCB,
                             *postHdr,
                             *m_histogram,
                             *m_exposureTex,
                             *m_samplerLinear,
                             m_width,
                             m_height,
                             m_settings.exposureMin,
                             m_settings.exposureMax,
                             m_settings.exposureAdapt,
                             m_settings.exposureTarget};
    executeExposurePass(ectx);
  };

  rgHiZ = [&](rhi::CommandList& c) {
    cmd = &c;
    struct HiZCBData {
      glm::uvec2 srcSize;
      glm::uvec2 dstSize;
      uint32_t mip;
      uint32_t pad[3]{};
    } hiz{};
    cmd->setDescriptorHeap();
    cmd->setRootSignature(*m_rootCS);
    hiz.srcSize = {m_width, m_height};
    hiz.dstSize = hiz.srcSize;
    D3D12_CPU_DESCRIPTOR_HANDLE src[] = {asDxTex(*m_depthColor).srvCpu};
    D3D12_CPU_DESCRIPTOR_HANDLE dst[] = {asDxTex(*m_hizMips[0]).uavCpu};
    updateUploadCB(*m_meshletCullCB, &hiz, sizeof(hiz));
    cmd->setPipeline(*m_hizCopyPSO);
    cmd->setComputeRootCBV(1, *m_meshletCullCB);
    cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(src, 1));
    cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(dst, 1));
    cmd->dispatch((m_width + 7u) / 8u, (m_height + 7u) / 8u, 1);
    for (uint32_t mip = 1; mip < m_hizMips.size(); ++mip) {
      cmd->transition(*m_hizMips[mip - 1], rhi::ResourceState::ShaderResource);
      cmd->transition(*m_hizMips[mip], rhi::ResourceState::UnorderedAccess);
      hiz.srcSize = {m_hizMips[mip - 1]->width(), m_hizMips[mip - 1]->height()};
      hiz.dstSize = {m_hizMips[mip]->width(), m_hizMips[mip]->height()};
      hiz.mip = mip;
      D3D12_CPU_DESCRIPTOR_HANDLE mipSrc[] = {asDxTex(*m_hizMips[mip - 1]).srvCpu};
      D3D12_CPU_DESCRIPTOR_HANDLE mipDst[] = {asDxTex(*m_hizMips[mip]).uavCpu};
      updateUploadCB(*m_meshletCullCB, &hiz, sizeof(hiz));
      cmd->setPipeline(*m_hizReducePSO);
      cmd->setComputeRootSrvTable(2, dxDevice.writeSrvTable(mipSrc, 1));
      cmd->setComputeRootUavTable(3, dxDevice.writeUavTable(mipDst, 1));
      cmd->dispatch((hiz.dstSize.x + 7u) / 8u, (hiz.dstSize.y + 7u) / 8u, 1);
    }
  };

  rgTonemap = [&](rhi::CommandList& c) {
    cmd = &c;
    TonemapPassContext tctx{m_device,
                            *cmd,
                            *m_rootFS,
                            *m_tonemapPSO,
                            *m_postCB,
                            m_postCBBump,
                            *postHdr,
                            m_settings.enableBloom ? *m_bloomMips[0] : *postHdr,
                            m_settings.enableAutoExposure ? m_exposureTex.get() : nullptr,
                            swapChainRT,
                            *m_samplerLinear,
                            m_settings.enableBloom ? m_settings.bloomStrength : 0.0f,
                            1.0f,
                            vp,
                            sc};
    executeTonemapPass(tctx);
    ++m_drawCalls;
  };

  // ---- Reflection probe cube-face RT capture (amortized 1 probe / frame) ----
  if ((m_settings.enableSSR || m_settings.enableVoxelGI) && m_probeCapturePSO && m_reflectionProbes.ready() &&
      m_reflectionProbes.faceAtlas() && m_reflectionProbes.faceDepth()) {
    const uint32_t probeIdx = m_probeBakeCursor % ReflectionProbes::kMaxProbes;
    const glm::vec3 sunDirN = glm::normalize(glm::vec3(frame.sunDirectionIntensity));
    const float sunI = std::max(frame.sunDirectionIntensity.w, 0.1f);

    // Seed GGX×5 once from WorldSDF before GPU owns mip0 (avoid stomping cube CS).
    if (!m_probeMipsSeeded && m_settings.enableVoxelGI) {
      for (uint32_t i = 0; i < ReflectionProbes::kMaxProbes; ++i) {
        m_reflectionProbes.bakeProbeCpu(i, m_worldSdf, sunDirN, sunI);
      }
      m_reflectionProbes.buildMipsAndUpload(m_device);
      m_probeMipsSeeded = true;
    }

    struct ProbeCapCB {
      glm::vec4 sunDirIntensity;
      glm::vec4 ambientColor;
    } pcb{glm::vec4(sunDirN, sunI), frame.ambientColor};
    updateUploadCB(*m_probeCaptureCB, &pcb, sizeof(pcb));

    rhi::Texture* faces = m_reflectionProbes.faceAtlas();
    rhi::Texture* faceDepth = m_reflectionProbes.faceDepth();
    cmd->transition(*faces, rhi::ResourceState::RenderTarget);
    cmd->transition(*faceDepth, rhi::ResourceState::DepthWrite);
    rhi::Texture* faceRT = faces;
    cmd->setRenderTargets(std::span<rhi::Texture*>(&faceRT, 1), faceDepth);
    const float skyClear[4] = {0.15f, 0.22f, 0.35f, 1.0f};
    cmd->clearRenderTarget(*faces, skyClear);
    cmd->setRootSignature(*m_root);
    cmd->setPipeline(*m_probeCapturePSO);
    cmd->setGraphicsRootCBV(1, *m_probeCaptureCB);
    cmd->setGraphicsRootSrvTable(3, 0);
    cmd->setGraphicsRootSamplerTable(4, sampTable);

    const uint32_t fs = ReflectionProbes::kFaceSize;
    for (uint32_t face = 0; face < ReflectionProbes::kFaces; ++face) {
      cmd->clearDepth(*faceDepth, 1.0f);
      const float ox = float(face * fs);
      cmd->setViewport({ox, 0.f, float(fs), float(fs), 0.f, 1.f});
      cmd->setScissor({int(ox), 0, int(ox + fs), int(fs)});
      const glm::mat4 vpFace = m_reflectionProbes.faceViewProj(probeIdx, face);
      for (auto& obj : scene.objects) {
        if (!obj.mesh) {
          continue;
        }
        for (const auto& sub : obj.mesh->submeshes()) {
          auto mat = (sub.materialIndex < obj.materials.size()) ? obj.materials[sub.materialIndex] : nullptr;
          auto* albedo = mat && mat->albedo ? &mat->albedo->resource() : &m_defaultAlbedo->resource();
          ObjectCBData ocb{};
          ocb.worldInvTranspose = glm::transpose(glm::inverse(obj.worldMatrix));
          if (mat) {
            ocb.baseColorFactor = mat->baseColorFactor;
            ocb.materialParams = {mat->metallicFactor, mat->roughnessFactor, mat->aoFactor,
                                  mat->alphaMask ? mat->alphaCutoff : 0.0f};
            ocb.emissiveFactor = glm::vec4(mat->emissiveFactor, 0.04f);
          } else {
            ocb.baseColorFactor = glm::vec4(1);
            ocb.materialParams = {0, 1, 1, 0};
            ocb.emissiveFactor = glm::vec4(0, 0, 0, 0.04f);
          }
          ocb.textureIndices = {bindlessOf(*albedo), bindlessOf(m_defaultNormal->resource()),
                                bindlessOf(m_defaultORM->resource()), bindlessOf(m_defaultBlack->resource())};
          const uint64_t off = pushObjectCB(ocb);
          RootXform xform{vpFace, obj.worldMatrix};
          cmd->setGraphicsRootConstants(0, &xform, 32);
          cmd->setGraphicsRootCBV(2, *m_objectCB, off);
          cmd->setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
          cmd->setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
          cmd->setIndexBuffer(obj.mesh->indexBuffer(), true);
          cmd->drawIndexed(sub.indexCount, sub.indexOffset);
          ++m_drawCalls;
        }
      }
    }

    // CS: cube faces → lat-long strip in probe atlas (mip0 live)
    cmd->transition(*faces, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_reflectionProbes.atlas(), rhi::ResourceState::UnorderedAccess);
    struct ProbeConvCB {
      uint32_t probeIndex;
      uint32_t faceSize;
      uint32_t latWidth;
      uint32_t latHeight;
      uint32_t maxProbes;
      uint32_t faceAtlasId;
      uint32_t pad0;
      uint32_t pad1;
      glm::vec4 skyTint;
    } conv{probeIdx,
           fs,
           ReflectionProbes::kWidth,
           ReflectionProbes::kHeight,
           ReflectionProbes::kMaxProbes,
           bindlessOf(*faces),
           0,
           0,
           glm::vec4(0.15f, 0.22f, 0.35f, 1.0f)};
    updateUploadCB(*m_probeConvertCB, &conv, sizeof(conv));
    cmd->setDescriptorHeap();
    cmd->setRootSignature(*m_rootCS);
    cmd->setPipeline(*m_probeConvertPSO);
    cmd->setComputeRootCBV(1, *m_probeConvertCB);
    cmd->setComputeRootSrvTable(2, 0);
    cmd->setComputeRootUavTable(3, uavOf(*m_reflectionProbes.atlas()));
    cmd->setComputeRootSamplerTable(4, sampTable);
    cmd->dispatch((ReflectionProbes::kWidth + 7) / 8, (ReflectionProbes::kHeight + 7) / 8, 1);
    cmd->uavBarrier(m_reflectionProbes.atlas());
    cmd->transition(*m_reflectionProbes.atlas(), rhi::ResourceState::ShaderResource);
    m_probeBakeCursor++;
  }

  // Build / refresh BLAS+TLAS only when a Ray Query pass is enabled.
  if (m_rtScene.supported() &&
      (m_settings.enableRTReflections || m_settings.enableRTShadows)) {
    m_rtScene.update(m_device, *cmd, scene);
  }

  // Full frame: Shadow → … → Tonemap (Phase3/Post + async DDGI tips).
  m_graph.execute(cmd, m_device);

  // Preserve depth + viewProj for next-frame temporal GI reprojection (camera MVs).
  if (cmd && m_depthHistory && m_depthColor) {
    cmd->transition(*m_depthColor, rhi::ResourceState::CopySrc);
    cmd->transition(*m_depthHistory, rhi::ResourceState::CopyDst);
    cmd->copyTextureRegion(*m_depthHistory, 0, 0, *m_depthColor, 0, 0, m_width, m_height);
    cmd->transition(*m_depthHistory, rhi::ResourceState::ShaderResource);
    cmd->transition(*m_depthColor, rhi::ResourceState::ShaderResource);
  }
  m_prevViewProj = frame.viewProj;
  m_hasPrevCamera = true;

  // Shader hot-reload: recreate PSOs when compiled .cso changes
  if (m_settings.enableShaderHotReload) {
    if (!m_shaderWatchReady) {
      const char* watchList[] = {"PostFX_PSGTAO.cso",         "PostFX_PSGTAOBlur.cso", "PostFX_PSTonemap.cso",
                                 "PostFX_PSBloomDown.cso",    "PostFX_PSBloomUp.cso",  "Exposure_CSBuildHistogram.cso",
                                 "DeferredLighting_PSMain.cso"};
      for (const char* f : watchList) {
        m_shaderWatcher.watch(shaderPath(f));
      }
      m_shaderWatchReady = true;
    }
    const auto changed = m_shaderWatcher.poll();
    if (!changed.empty()) {
      try {
        m_device.waitIdle();
        createPipelines();
        createPhase3Pipelines();
        std::cout << "[Renderer] Hot-reloaded shaders (" << changed.size() << " files)\n";
      } catch (const std::exception& ex) {
        std::cerr << "[Renderer] Hot-reload failed: " << ex.what() << "\n";
      }
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  m_lastFrameMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}
} // namespace tucano


