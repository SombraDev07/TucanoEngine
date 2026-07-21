#pragma once

#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Scene.h"
#include "Renderer/Shadows/ToroidalShadows.h"
#include "Renderer/Shadows/OctahedralShadows.h"
#include "Renderer/GI/VoxelGI.h"
#include "Renderer/Weather/RainSystem.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

enum class GITier : uint32_t { Off = 0, Low = 1, Medium = 2, High = 3 };

struct RendererSettings {
  bool enableShadows = true;
  bool enableIBL = true;
  bool enableBloom = true;
  bool enableAO = true;
  bool enableTonemap = true;
  bool enableSSR = false;
  bool enableMeshlets = false;
  bool enableVisibilityBuffer = false;
  bool enableRTReflections = false; // DXR stub — always unavailable on non-RT GPUs
  bool enableContactShadows = true;
  bool enableVoxelGI = true;
  bool enableToroidalShadows = true;
  bool enableOctahedralPointShadows = true;
  GITier giTier = GITier::Off;
  uint32_t shadowMapSize = 2048;
};

class Renderer {
public:
  Renderer(rhi::Device& device, uint32_t width, uint32_t height);
  ~Renderer();

  void resize(uint32_t width, uint32_t height);
  void render(rhi::CommandList& cmd, rhi::Texture& swapChainRT, Scene& scene);
  RendererSettings& settings() { return m_settings; }
  const RendererSettings& settings() const { return m_settings; }

  float lastFrameMs() const { return m_lastFrameMs; }
  uint32_t drawCalls() const { return m_drawCalls; }
  uint32_t meshletsTotal() const { return m_meshletsTotal; }
  uint32_t meshletsDrawn() const { return m_meshletsDrawn; }
  uint64_t rgAliasedBytes() const { return m_graph.aliasedBytes(); }
  RainParams& rain() { return m_rain.params(); }
  const RainParams& rain() const { return m_rain.params(); }

private:
  void createTargets();
  void createPipelines();
  void createDefaultTextures();
  void createPhase3Pipelines();

  rhi::Device& m_device;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  RendererSettings m_settings;
  RenderGraph m_graph;

  std::shared_ptr<rhi::RootSignature> m_root;
  std::shared_ptr<rhi::RootSignature> m_rootFS;
  std::shared_ptr<rhi::RootSignature> m_rootCS;
  std::shared_ptr<rhi::Texture> m_depthColor;
  std::shared_ptr<rhi::PipelineState> m_gbufferPSO;
  std::shared_ptr<rhi::PipelineState> m_shadowPSO;
  std::shared_ptr<rhi::PipelineState> m_lightingPSO;
  std::shared_ptr<rhi::PipelineState> m_tonemapPSO;
  std::shared_ptr<rhi::PipelineState> m_bloomDownPSO;
  std::shared_ptr<rhi::PipelineState> m_bloomUpPSO;
  std::shared_ptr<rhi::PipelineState> m_aoPSO;
  std::shared_ptr<rhi::PipelineState> m_ssgiPSO;
  std::shared_ptr<rhi::PipelineState> m_ssrPSO;
  std::shared_ptr<rhi::PipelineState> m_composePSO;
  std::shared_ptr<rhi::PipelineState> m_ddgiSamplePSO;
  std::shared_ptr<rhi::PipelineState> m_ddgiUpdatePSO;
  std::shared_ptr<rhi::PipelineState> m_visBufferPSO;
  std::shared_ptr<rhi::PipelineState> m_visAlbedoPSO;
  std::shared_ptr<rhi::PipelineState> m_visNormalPSO;
  std::shared_ptr<rhi::PipelineState> m_visOrmPSO;
  std::shared_ptr<rhi::PipelineState> m_visEmissivePSO;
  std::shared_ptr<rhi::PipelineState> m_contactShadowPSO;

  std::shared_ptr<rhi::Texture> m_albedo;
  std::shared_ptr<rhi::Texture> m_normal;
  std::shared_ptr<rhi::Texture> m_orm;
  std::shared_ptr<rhi::Texture> m_emissive;
  std::shared_ptr<rhi::Texture> m_depth;
  std::shared_ptr<rhi::Texture> m_hdr;
  std::shared_ptr<rhi::Texture> m_hdrCompose;
  std::shared_ptr<rhi::Texture> m_ao;
  std::shared_ptr<rhi::Texture> m_shadowMap;
  std::shared_ptr<rhi::Texture> m_shadowScrollTemp;
  std::shared_ptr<rhi::Texture> m_brdfLUT;
  std::shared_ptr<rhi::Texture> m_irradiance;
  std::shared_ptr<rhi::Texture> m_prefiltered;
  std::shared_ptr<rhi::Texture> m_ssgi;
  std::shared_ptr<rhi::Texture> m_ssr;
  std::shared_ptr<rhi::Texture> m_ddgiAtlas;
  std::shared_ptr<rhi::Texture> m_ddgiAtlasPrev;
  std::shared_ptr<rhi::Texture> m_visId;
  float m_iblMaxMip = 0.0f;
  float m_iblExposure = 1.0f;
  std::array<std::shared_ptr<rhi::Texture>, 5> m_bloomMips;

  std::shared_ptr<rhi::Sampler> m_samplerLinear;
  std::shared_ptr<rhi::Sampler> m_samplerShadow;
  std::shared_ptr<rhi::Buffer> m_frameCB;
  std::shared_ptr<rhi::Buffer> m_objectCB;
  std::shared_ptr<rhi::Buffer> m_lightCB;
  std::shared_ptr<rhi::Buffer> m_phase3CB;
  std::shared_ptr<rhi::Buffer> m_indirectArgs;
  std::shared_ptr<Texture> m_defaultAlbedo;
  std::shared_ptr<Texture> m_defaultNormal;
  std::shared_ptr<Texture> m_defaultORM;
  std::shared_ptr<Texture> m_defaultBlack;

  float m_lastFrameMs = 0.0f;
  uint32_t m_drawCalls = 0;
  uint32_t m_meshletsTotal = 0;
  uint32_t m_meshletsDrawn = 0;
  bool m_loggedNoDXR = false;
  std::vector<uint32_t> m_visibleMeshlets;

  ToroidalShadowAtlas m_shadowAtlas;
  OctahedralShadowAtlas m_octaShadows;
  VoxelGI m_voxelGI;
  SkyVisibility m_skyVis;
  RainSystem m_rain;
  std::shared_ptr<rhi::Buffer> m_rainCB;
  float m_timeSeconds = 0.0f;
  float m_skyOcclusionAvg = 1.0f;
};

} // namespace tucano
