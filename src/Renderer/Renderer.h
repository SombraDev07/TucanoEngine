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
#include "Renderer/Weather/WaterSystem.h"
#include "Renderer/Weather/FogSystem.h"
#include "Renderer/PostFX/PostFxParams.h"
#include "Renderer/Sky/Celestial.h"
#include "Renderer/Sky/SkyParams.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"
#include "AssetPipeline/ResourceFactory.h"
#include "Core/TypeSystem/ReflectionMacros.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

enum class TUCANO_ENUM() GITier : uint32_t {
  Off = 0,
  Low = 1,
  Medium = 2,
  High = 3
};

// What the renderer does, as opposed to what the world looks like — sky, sun, moon and wind moved
// out to SkyParams (E-01) so that a scene can save its environment without saving whether meshlets
// are on.
//
// Reflected (D-01), which is what makes the Rendering panel generated rather than hand-written. The
// old Environment panel wrote a widget per field by hand and reached 46 of 67 fields; the other 21
// were unreachable from the editor at all. A field added here now appears with its range and its
// tooltip, and cannot be forgotten.
//
// `.advanced` marks the engineering keys — the ones you flip to find out why something is slow or
// wrong, not to make the game look a certain way. The grid hides them behind a toggle so the
// artist-facing surface stays readable.
struct TUCANO_TYPE() RendererSettings {
  // Deliberately not annotated. SkyParams has its own panel and will be its own block in a scene
  // file (E-02); reflecting it here as well would draw every sky field a second time, nested inside
  // Rendering, and give the environment two places to be edited from.
  SkyParams sky;

  // ── Shadows ──────────────────────────────────────────────────────────────
  TUCANO_FIELD(.label = "Enabled", .tooltip = "Master switch for every shadow pass", .category = "Shadows")
  bool enableShadows = true;
  TUCANO_FIELD(.label = "Soft shadows (PCSS)", .tooltip = "Contact-hardening penumbra that widens with distance from the caster", .category = "Shadows")
  bool enablePCSS = true;
  TUCANO_FIELD(.label = "Light size", .tooltip = "Apparent size of the sun for PCSS. Bigger means softer, blurrier penumbras", .category = "Shadows", .minValue = 0.0f, .maxValue = 0.2f, .step = 0.001f)
  float pcssLightSize = 0.035f;
  TUCANO_FIELD(.label = "Contact shadows", .tooltip = "Short screen-space rays that recover the tiny contact darkening shadow maps miss", .category = "Shadows")
  bool enableContactShadows = true;
  TUCANO_FIELD(.label = "Shadow map size", .tooltip = "Pixels per cascade face. Doubling this costs four times the memory", .category = "Shadows", .minValue = 512.0f, .maxValue = 8192.0f, .step = 512.0f)
  uint32_t shadowMapSize = 2048;
  TUCANO_FIELD(.label = "Toroidal CSM", .tooltip = "Scrolls cascades instead of redrawing them when the camera moves", .category = "Shadows", .advanced = true)
  bool enableToroidalShadows = true;
  TUCANO_FIELD(.label = "Octahedral point shadows", .tooltip = "Single-texture point-light shadows instead of a cube of six", .category = "Shadows", .advanced = true)
  bool enableOctahedralPointShadows = true;
  TUCANO_FIELD(.label = "Virtual shadow maps", .category = "Shadows", .advanced = true)
  bool enableVSM = true;
  TUCANO_FIELD(.label = "Exponential (ESM)", .tooltip = "Alternative filtering; leaks light through thin geometry", .category = "Shadows", .advanced = true)
  bool enableESM = false;
  TUCANO_FIELD(.label = "ESM exponent", .category = "Shadows", .minValue = 1.0f, .maxValue = 200.0f, .step = 1.0f, .advanced = true)
  float esmExponent = 80.0f;

  // Deliberately not annotated, exactly like `sky` above: PostFxParams has its own panel and its
  // own block in a scene file (E-04). Reflecting it here as well would draw every grading field a
  // second time, nested inside Rendering, and give the look of the game two places to be edited
  // from — which is the bug E-05 had to undo for the clouds.
  PostFxParams postFx;

  // ── Global illumination and reflections ──────────────────────────────────
  //
  // These stay. Every one of them is a property of the machine drawing the scene rather than of the
  // scene: RT is "auto-enabled when the device has it", and `giTier` is how much budget to spend.
  // Writing them into a `.tuscene` would make a scene authored on a DXR machine ask for ray tracing
  // on one that has none.
  TUCANO_FIELD(.label = "Image-based lighting", .tooltip = "Environment light from the sky or an HDRI", .category = "Global illumination")
  bool enableIBL = true;
  TUCANO_FIELD(.label = "Screen-space reflections", .category = "Global illumination")
  bool enableSSR = true;
  TUCANO_FIELD(.label = "Voxel GI", .tooltip = "Bounced light from a voxelised scene", .category = "Global illumination")
  bool enableVoxelGI = true;
  TUCANO_FIELD(.label = "RT reflections", .tooltip = "Hardware ray-traced reflections. Needs DXR; auto-enabled when the device has it", .category = "Global illumination")
  bool enableRTReflections = false;
  TUCANO_FIELD(.label = "RT shadows", .tooltip = "Ray-query sun shadows masked against the cascades", .category = "Global illumination")
  bool enableRTShadows = false;

  // ── Clouds ───────────────────────────────────────────────────────────────
  //
  // Gone, and deliberately (E-05). Every one of these eleven fields had a twin in `CloudParams`,
  // and `Renderer::render` copied this struct over that one **every frame** — so the Clouds panel,
  // which edits `CloudParams`, changed nothing, and the `CloudParams` block a `.tuscene` saves was
  // overwritten on the frame after it loaded. Two places to edit the same weather, one of which
  // silently lost.
  //
  // `CloudParams` won because it is the superset (it also carries the weather map and the temporal
  // filter) and because it is the one the scene file already stores. Same move E-01 made with the
  // sky: one owner, and the panel that edits it is the one that means something.

  // ── Geometry pipeline ────────────────────────────────────────────────────
  // Every one of these is a different way of drawing the same triangles. They belong to whoever is
  // chasing a frame budget, not to whoever is dressing a level.
  TUCANO_FIELD(.label = "Meshlets", .tooltip = "Cluster-based geometry path", .category = "Geometry pipeline", .advanced = true)
  bool enableMeshlets = true;
  TUCANO_FIELD(.label = "GPU meshlet culling", .tooltip = "Compute frustum/cone rejection feeding indirect args. Needs Meshlets", .category = "Geometry pipeline", .advanced = true)
  bool enableGpuMeshletCull = true;
  TUCANO_FIELD(.label = "Meshlet compaction", .category = "Geometry pipeline", .advanced = true)
  bool enableMeshletCompact = true;
  TUCANO_FIELD(.label = "Hi-Z occlusion", .tooltip = "Rejects geometry hidden behind last frame's depth pyramid", .category = "Geometry pipeline", .advanced = true)
  bool enableHiZOcclusion = true;
  TUCANO_FIELD(.label = "Visibility buffer", .tooltip = "Deferred material evaluation from a triangle-id buffer", .category = "Geometry pipeline", .advanced = true)
  bool enableVisibilityBuffer = true;
  TUCANO_FIELD(.label = "Mesh shaders", .tooltip = "Gated by device support; falls back automatically", .category = "Geometry pipeline", .advanced = true)
  bool enableMeshShaders = true;
  TUCANO_FIELD(.label = "Async compute", .tooltip = "Runs GI and other compute on a second queue", .category = "Geometry pipeline", .advanced = true)
  bool enableAsyncCompute = true;
  TUCANO_FIELD(.label = "Shader hot reload", .tooltip = "Watches the shader directory and recompiles on change", .category = "Geometry pipeline", .advanced = true)
  bool enableShaderHotReload = true;

  TUCANO_FIELD(.label = "GI tier", .tooltip = "How much of the global illumination budget to spend", .category = "Global illumination")
  GITier giTier = GITier::Low;

  // Not reflected: changing the path has to re-cook the IBL and roll back if the file cannot be
  // used, which a plain string edit cannot express. The Environment panel owns the picker that
  // does both.
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
  std::shared_ptr<rhi::RootSignature> sharedComputeRootSig() const { return m_rootCS; }
  /// Previous-frame Hi-Z mip used for occlusion (mip 2). Null if Hi-Z disabled / not built yet.
  rhi::Texture* hizOcclusionMip() const {
    return (m_settings.enableHiZOcclusion && m_hizMips[2]) ? m_hizMips[2].get() : nullptr;
  }
  uint32_t drawCalls() const { return m_drawCalls; }
  uint32_t meshletsTotal() const { return m_meshletsTotal; }
  uint32_t meshletsDrawn() const { return m_meshletsDrawn; }
  uint64_t rgAliasedBytes() const { return m_graph.aliasedBytes(); }
  SkyParams& sky() { return m_settings.sky; }
  const SkyParams& sky() const { return m_settings.sky; }
  PostFxParams& postFx() { return m_settings.postFx; }
  const PostFxParams& postFx() const { return m_settings.postFx; }
  RainParams& rain() { return m_rain.params(); }
  const RainParams& rain() const { return m_rain.params(); }
  WaterParams& water() { return m_water.params(); }
  const WaterParams& water() const { return m_water.params(); }
  FogParams& fog() { return m_fog.params(); }
  const FogParams& fog() const { return m_fog.params(); }
  CloudParams& clouds() { return m_clouds.params(); }
  const CloudParams& clouds() const { return m_clouds.params(); }

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
  std::shared_ptr<rhi::PipelineState> m_instanceGbufferPSO; // WM-6: instanced indirect g-buffer draw
  std::shared_ptr<rhi::PipelineState> m_instanceShadowPSO;  // Veg-P1: instanced shadow casters
  std::shared_ptr<rhi::PipelineState> m_instanceBillboardPSO; // Veg-P2: LOD2 impostors
  std::shared_ptr<rhi::PipelineState> m_clipmapTerrainPSO;  // WM-8: continuous-LOD clipmap terrain
  std::shared_ptr<rhi::PipelineState> m_clipmapFeedbackPSO; // WM-8: VT feedback pass (Phase 2b)
  std::shared_ptr<rhi::Texture> m_vtFeedbackRT;             // R32_UINT page requests, reduced res
  std::shared_ptr<rhi::Texture> m_vtFeedbackDepth;          // its own depth
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
  std::shared_ptr<rhi::Buffer> m_clipmapVtCB; // WM-8: terrain VT params (b2) for the clipmap draw
  uint64_t m_clipmapVtBump = 0;
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
  WaterSystem m_water;
  FogSystem m_fog;
  std::shared_ptr<rhi::Buffer> m_waterCB;
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
