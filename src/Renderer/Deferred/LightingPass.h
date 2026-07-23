#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>

namespace tucano {

struct LightingPassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& rootFS;
  rhi::PipelineState& lightingPSO;
  rhi::Buffer& frameCB;
  rhi::Buffer& lightCB;
  rhi::Texture& hdr;
  rhi::Texture& albedo;
  rhi::Texture& normal;
  rhi::Texture& orm;
  rhi::Texture& emissive;
  rhi::Texture& depthColor;
  rhi::Texture& shadowMap;
  rhi::Texture& brdfLUT;
  rhi::Texture& irradiance;
  rhi::Texture& prefiltered;
  rhi::Texture& ao;
  rhi::Texture* octaAtlas = nullptr;
  rhi::Texture* vsmPhysical = nullptr;
  rhi::Texture* vsmPageTable = nullptr;
  rhi::Texture* rtShadowMask = nullptr;
  rhi::Sampler& linearSamp;
  rhi::Sampler& shadowSamp;
  glm::mat4 invViewProj{1.0f};
  glm::vec4 cameraPos{};
  glm::vec4 sunDirectionIntensity{};
  glm::vec4 sunColor{};
  glm::vec4 ambientColor{};
  glm::mat4 lightViewProj[4]{};
  glm::vec4 cascadeSplits{};
  glm::vec4 flags{}; // shadows, ibl, ao, esm
  glm::vec4 screenSize{};
  float iblMaxMip = 0.0f;
  float iblExposure = 1.0f;
  glm::vec4 shadowParams{}; // pcssLightSize, esmK, octaEnable, pcssEnable
  glm::vec4 vsmMeta{};      // enable, pagesPerAxis, physicalGrid, _
  glm::vec4 atmParams{};    // turbidity, fogDensity, fogHeight, enable
  glm::vec4 brunetonParams{}; // bottomKm, topKm, mieG, exposure
  glm::uvec4 brunetonTexIds{}; // trans, scat, irr, enable
  rhi::Texture* brunetonTransmittance = nullptr;
  rhi::Texture* brunetonScattering = nullptr;
  rhi::Texture* brunetonIrradiance = nullptr;

  rhi::Viewport viewport{};
  rhi::Scissor scissor{};
  uint32_t* drawCalls = nullptr;

  // ── Celestial bodies (see Renderer/Sky/Celestial.h) ──
  // Deliberately last: the renderer builds this struct with positional aggregate initialisation,
  // so anything added ahead of drawCalls would silently shift every argument after it. These are
  // assigned by name instead.
  glm::vec4 moonDirPhase{0.0f, -1.0f, 0.0f, 0.5f};       ///< xyz = moon->scene, w = phase
  glm::vec4 moonColorIntensity{0.0f};                    ///< rgb = moonlight colour, w = intensity
  glm::vec4 moonDiscParams{0.0045f, 1.0f, 1.0f, 0.0f};   ///< angRadius, illum, discBrightness, enable
  glm::vec4 starParams{0.0f, 0.006f, 0.0009f, 0.35f};    ///< intensity, procDensity, sigma, twinkle
  glm::vec4 celestialParams{0.001f, 0.0f, 0.0f, 256.0f}; ///< pixelAngle, time, starFade, dataWidth
  rhi::Texture* starCellTex = nullptr;
  rhi::Texture* starDataTex = nullptr;
  uint32_t catalogStarCount = 0;
  glm::mat3 worldToEquatorial{1.0f};
};

void executeLightingPass(LightingPassContext& ctx);

} // namespace tucano
