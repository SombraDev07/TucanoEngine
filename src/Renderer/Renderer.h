#pragma once

#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Scene.h"
#include "Renderer/Shadows/ToroidalShadows.h"
#include "Renderer/Shadows/OctahedralShadows.h"
#include "Renderer/Shadows/VirtualShadowMaps.h"
#include "Renderer/GI/VoxelGI.h"
#include "Renderer/GI/WorldSDF.h"
#include "Renderer/GI/ReflectionProbes.h"
#include "Renderer/GI/BrunetonAtmosphere.h"
#include "Renderer/RayTracing/RayTracingScene.h"
#include "Renderer/Weather/RainSystem.h"
#include "Renderer/Weather/CloudSystem.h"
#include "Renderer/Sky/Celestial.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"
#include "AssetPipeline/ResourceFactory.h"

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
  bool enableSSR = true; // FASE 4.2 default-on
  bool enableMeshlets = true; // FASE 3.2 default path
  bool enableGpuMeshletCull = true; // CS frustum/cone → indirect args (requires enableMeshlets)
  bool enableHiZOcclusion = true;
  bool enableMeshletCompact = true;
  bool enableVisibilityBuffer = true; // VisBuffer material path
  bool enableMeshShaders = true; // gated by Device::supportsMeshShaders()
  bool enableRTReflections = false; // set true in Renderer ctor when Device::supportsRaytracing()
  bool enableRTShadows = false;     // Ray Query sun shadows (mask × CSM); auto-on with DXR
  bool enableContactShadows = true;
  bool enableAtmosphere = true;     // FASE 5.2 sky + fog
  bool useBrunetonAtmosphere = true; // Bruneton precomputed LUTs (else artistic Nishita)
  bool atmosphereDrivesSun = true;  // timeOfDay → directional light
  float timeOfDay = 0.38f;          // 0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset
  float turbidity = 2.8f;           // 1=clear … 8=hazy
  float fogDensity = 0.012f;
  float fogHeight = 40.0f;
  glm::vec3 wind = {0.2f, 0.0f, 0.05f};
  bool enableClouds = true;
  float cloudCoverage = 0.48f;
  float cloudDensity = 1.15f;
  float cloudAltitude = 1500.0f;
  float cloudThickness = 2400.0f;
  float cloudShadowStrength = 0.7f;
  float cloudGodRayStrength = 0.55f;
  float cloudStorminess = 0.35f;
  bool enableCloudShadows = true;
  bool enableCloudGodRays = true;
  bool cloudsDriveRain = true;
  bool enableVoxelGI = true;
  bool enableToroidalShadows = true;
  bool enableOctahedralPointShadows = true;
  bool enableVSM = true;
  bool enableAsyncCompute = true; // DDGI (and future CS) on compute queue
  bool enableAutoExposure = true;
  bool enableShaderHotReload = true;
  bool enableESM = false;
  bool enablePCSS = true;
  float pcssLightSize = 0.035f;
  float esmExponent = 80.0f;
  float aoIntensity = 1.0f;
  float aoRadius = 0.9f;
  float bloomStrength = 0.28f;
  float exposureMin = 0.08f;
  float exposureMax = 4.0f;
  float exposureAdapt = 0.1f;
  float exposureTarget = 0.18f;

  // ── Night sky ────────────────────────────────────
  bool enableMoon = true;
  bool enableStars = true;
  /// Peak moonlight intensity at full moon and zenith. Two orders of magnitude under the sun on
  /// purpose: real moonlight is ~400,000x weaker, but a night that dark is unplayable, so games
  /// lift it. This is the dial for how far.
  float moonIntensity = 0.045f;
  float moonDiscBrightness = 2.5f;
  /// Apparent radius in degrees. The real moon is 0.26; going bigger is the oldest cheat in
  /// landscape art and it reads well.
  float moonAngularRadiusDeg = 0.5f;
  float starIntensity = 1.0f;
  float starTwinkle = 0.35f;
  /// Angular radius of a star before the sub-pixel clamp kicks in, in degrees.
  float starSizeDeg = 0.055f;
  /// Blend toward blue in dim areas of the frame. 0 disables the Purkinje shift.
  float purkinjeStrength = 0.75f;
  /// Observer's place and date. These drive the sidereal rotation, so they decide which
  /// constellations are up and how they move.
  float latitudeDeg = -23.55f;  // Sao Paulo
  /// Day of year drives the season AND the lunar phase, since phase is elongation from the sun.
  /// 156 lands on a full moon in this model, so a fresh scene at midnight actually has a moon in
  /// the sky rather than a new moon sitting below the horizon with the sun.
  float dayOfYear = 156.0f;
  std::string starCatalogPath = "Sky/bright_stars.txt"; // under TUCANO_ENGINE_ASSETS_DIR

  GITier giTier = GITier::Low;
  uint32_t shadowMapSize = 2048;
  std::string hdriPath = "IBL/default.hdr"; // under TUCANO_ENGINE_ASSETS_DIR
};

class Renderer {
public:
  Renderer(rhi::Device& device, uint32_t width, uint32_t height);
  ~Renderer();

  void resize(uint32_t width, uint32_t height);
  // Rebuild all GPU resources after Device::recoverFromDeviceLost (device already healthy).
  void recreateAfterDeviceLost();

  /// Re-cooks the image-based lighting from a different HDRI (Phase I-1 skybox).
  /// Falls back to the procedural sky when the file is missing, exactly as startup does.
  /// Returns false if the path could not be used, in which case the previous IBL is kept.
  bool reloadIBL(const std::string& hdriPath);

  /// (Re)builds the GPU star catalogue from settings().starCatalogPath. Safe to call at any time;
  /// on failure it clears the textures and the renderer falls back to the procedural star field.
  void buildStarCatalogTextures();

  /// Where the sun and moon currently are, and the moon's phase. Recomputed every frame from
  /// timeOfDay/dayOfYear/latitude; useful to the editor for aiming a camera or a gizmo.
  const CelestialState& celestial() const { return m_celestial; }

  /// Overall brightness applied to the environment lighting.
  float iblExposure() const { return m_iblExposure; }
  void setIblExposure(float e) { m_iblExposure = e; }
  // May reassign `cmd` after a graphics checkpoint (async compute handoff).
  void render(rhi::CommandList*& cmd, rhi::Texture& swapChainRT, Scene& scene);
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
  std::shared_ptr<rhi::PipelineState> m_shadowOctaPSO;
  std::shared_ptr<rhi::PipelineState> m_lightingPSO;
  std::shared_ptr<rhi::PipelineState> m_tonemapPSO;
  std::shared_ptr<rhi::PipelineState> m_bloomDownPSO;
  std::shared_ptr<rhi::PipelineState> m_bloomUpPSO;
  std::shared_ptr<rhi::PipelineState> m_aoPSO;
  std::shared_ptr<rhi::PipelineState> m_aoBlurPSO;
  std::shared_ptr<rhi::PipelineState> m_exposureClearPSO;
  std::shared_ptr<rhi::PipelineState> m_exposureBuildPSO;
  std::shared_ptr<rhi::PipelineState> m_exposureReducePSO;
  std::shared_ptr<rhi::PipelineState> m_ssgiPSO;
  std::shared_ptr<rhi::PipelineState> m_ssrPSO;
  std::shared_ptr<rhi::PipelineState> m_composePSO;
  std::shared_ptr<rhi::PipelineState> m_ddgiSamplePSO;
  std::shared_ptr<rhi::PipelineState> m_ddgiUpdatePSO;
  std::shared_ptr<rhi::PipelineState> m_meshletCullPSO;
  std::shared_ptr<rhi::PipelineState> m_hizCopyPSO;
  std::shared_ptr<rhi::PipelineState> m_hizReducePSO;
  std::shared_ptr<rhi::PipelineState> m_meshletCompactPSO;
  std::shared_ptr<rhi::PipelineState> m_gbufferMeshletPSO;
  std::shared_ptr<rhi::PipelineState> m_visResolvePSO;
  std::shared_ptr<rhi::PipelineState> m_meshletMeshPSO;
  std::shared_ptr<rhi::PipelineState> m_visBufferPSO;
  std::shared_ptr<rhi::PipelineState> m_visAlbedoPSO;
  std::shared_ptr<rhi::PipelineState> m_visNormalPSO;
  std::shared_ptr<rhi::PipelineState> m_visOrmPSO;
  std::shared_ptr<rhi::PipelineState> m_visEmissivePSO;
  std::shared_ptr<rhi::PipelineState> m_contactShadowPSO;
  std::shared_ptr<rhi::PipelineState> m_probeCapturePSO;
  std::shared_ptr<rhi::PipelineState> m_probeConvertPSO;
  std::shared_ptr<rhi::PipelineState> m_rtShadowsPSO;
  std::shared_ptr<rhi::PipelineState> m_rtContactPSO;
  std::shared_ptr<rhi::PipelineState> m_rtReflectionsPSO;

  std::shared_ptr<rhi::Texture> m_albedo;
  std::shared_ptr<rhi::Texture> m_normal;
  std::shared_ptr<rhi::Texture> m_orm;
  std::shared_ptr<rhi::Texture> m_emissive;
  std::shared_ptr<rhi::Texture> m_depth;
  std::shared_ptr<rhi::Texture> m_hdr;
  std::shared_ptr<rhi::Texture> m_hdrCompose;
  std::shared_ptr<rhi::Texture> m_ao;
  std::shared_ptr<rhi::Texture> m_aoTemp;
  std::shared_ptr<rhi::Texture> m_shadowMap;
  std::shared_ptr<rhi::Texture> m_shadowScrollTemp;
  std::shared_ptr<rhi::Texture> m_starCellTex;
  std::shared_ptr<rhi::Texture> m_starDataTex;
  uint32_t m_starCount = 0;
  uint32_t m_starDataWidth = 256;
  CelestialState m_celestial{};
  /// Environment-lighting multiplier from the time of day. See the note where it is computed.
  float m_skyLightScale = 1.0f;
  std::shared_ptr<rhi::Texture> m_brdfLUT;
  std::shared_ptr<rhi::Texture> m_irradiance;
  std::shared_ptr<rhi::Texture> m_prefiltered;
  std::shared_ptr<rhi::Texture> m_ssgi;
  std::shared_ptr<rhi::Texture> m_ssgiHistory;
  std::shared_ptr<rhi::Texture> m_depthHistory;
  std::shared_ptr<rhi::Texture> m_ssr;
  std::shared_ptr<rhi::Texture> m_rtShadowMask;
  std::shared_ptr<rhi::Texture> m_ddgiAtlas;
  std::shared_ptr<rhi::Texture> m_ddgiAtlasPrev;
  std::shared_ptr<rhi::Texture> m_visId;
  std::shared_ptr<rhi::Texture> m_visUv;
  std::shared_ptr<rhi::Texture> m_visNormal;
  std::shared_ptr<rhi::Texture> m_visDepth;
  float m_iblMaxMip = 0.0f;
  float m_iblExposure = 1.0f;
  std::array<std::shared_ptr<rhi::Texture>, 5> m_bloomMips;
  std::array<std::shared_ptr<rhi::Texture>, 8> m_hizMips;
  std::shared_ptr<rhi::Texture> m_bloomScratch;
  std::shared_ptr<rhi::Texture> m_histogram;
  std::shared_ptr<rhi::Texture> m_exposureTex;
  std::shared_ptr<rhi::Buffer> m_exposureCB;

  std::shared_ptr<rhi::Sampler> m_samplerLinear;
  std::shared_ptr<rhi::Sampler> m_samplerShadow;
  std::shared_ptr<rhi::Buffer> m_frameCB;
  std::shared_ptr<rhi::Buffer> m_postCB;
  uint64_t m_postCBBump = 0;
  std::shared_ptr<rhi::Buffer> m_objectCB;
  // Skinning palette shared across the frame; objects index into it via ObjectCB::skinInfo.
  static constexpr uint32_t kMaxSkinningMatrices = 4096;
  std::shared_ptr<rhi::Buffer> m_skinningBuffer;
  std::shared_ptr<rhi::Buffer> m_lightCB;
  std::shared_ptr<rhi::Buffer> m_phase3CB;
  uint64_t m_phase3CBBump = 0;
  std::shared_ptr<rhi::Buffer> m_probeCaptureCB;
  std::shared_ptr<rhi::Buffer> m_probeConvertCB;
  std::shared_ptr<rhi::Buffer> m_probeFaceReadback;
  std::shared_ptr<rhi::Buffer> m_rtCB;
  uint64_t m_rtCBBump = 0;
  uint32_t m_probeBakeCursor = 0;
  bool m_probeMipsSeeded = false;
  RayTracingScene m_rtScene;
  std::shared_ptr<rhi::Buffer> m_indirectArgs;
  std::shared_ptr<rhi::Buffer> m_compactedArgs;
  std::shared_ptr<rhi::Buffer> m_indirectCount;
  std::shared_ptr<rhi::Buffer> m_drawMaterials;
  std::shared_ptr<rhi::Buffer> m_meshletCullCB;
  std::shared_ptr<Texture> m_defaultAlbedo;
  std::shared_ptr<Texture> m_defaultNormal;
  std::shared_ptr<Texture> m_defaultORM;
  std::shared_ptr<Texture> m_defaultBlack;

  float m_lastFrameMs = 0.0f;
  uint32_t m_drawCalls = 0;
  uint32_t m_meshletsTotal = 0;
  uint32_t m_meshletsDrawn = 0;
  bool m_loggedNoDXR = false;
  bool m_loggedNoMeshShaders = false;
  std::vector<uint32_t> m_visibleMeshlets;

  ToroidalShadowAtlas m_shadowAtlas;
  OctahedralShadowAtlas m_octaShadows;
  VirtualShadowMapAtlas m_vsm;
  VoxelGI m_voxelGI;
  WorldSDF m_worldSdf;
  ReflectionProbes m_reflectionProbes;
  BrunetonAtmosphere m_bruneton;
  SkyVisibility m_skyVis;
  RainSystem m_rain;
  std::shared_ptr<rhi::Buffer> m_rainCB;
  CloudSystem m_clouds;
  std::shared_ptr<rhi::Buffer> m_cloudCB;
  float m_timeSeconds = 0.0f;
  float m_skyOcclusionAvg = 1.0f;
  glm::mat4 m_prevViewProj{1.0f};
  bool m_hasPrevCamera = false;
  AssetWatcher m_shaderWatcher;
  bool m_shaderWatchReady = false;
};

} // namespace tucano
