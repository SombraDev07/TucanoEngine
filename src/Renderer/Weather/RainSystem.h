#pragma once

#include "Renderer/Weather/RainParams.h"
#include "Renderer/Scene.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"

#include <memory>

namespace tucano {

class RainSystem {
public:
  void init(rhi::Device& device);
  void resize(rhi::Device& device, uint32_t width, uint32_t height);

  RainParams& params() { return m_params; }
  const RainParams& params() const { return m_params; }

  // True while raining OR while surfaces are still wet/drying — rain passes must keep
  // running after the rain stops so puddles fade out instead of vanishing.
  bool isActive() const { return (m_params.enabled && m_params.amount > 0.001f) || m_wetLevel > 0.01f; }

  // After GBuffer, before lighting: wetness / puddles / ripples + Cry rain-space occluder.
  void executeDeferredGBuffer(rhi::CommandList& cmd, rhi::Device& device, Scene& scene, rhi::Texture& albedo,
                              rhi::Texture& normal, rhi::Texture& orm, rhi::Texture& depthColor,
                              rhi::Buffer& rainCB, rhi::Sampler& linearSamp, const glm::mat4& invViewProj,
                              const glm::mat4& viewProj, const glm::mat4& view, const glm::vec3& cameraPos,
                              float timeSeconds, uint32_t width, uint32_t height);

  // Sun + camera info for lit streaks; cloud weather map couples local rain density (all optional).
  void setLighting(const glm::vec4& sunDirIntensity, const glm::vec3& sunColor, float tanHalfFovY,
                   rhi::Texture* weatherMap, float cloudCoverage = 1.0f) {
    m_sunDirIntensity = sunDirIntensity;
    m_sunColor = sunColor;
    m_tanHalfFovY = tanHalfFovY;
    m_weatherMap = weatherMap;
    m_cloudCoverage = cloudCoverage;
  }

  // After HDR: merged composite (puddle SSR + mist + lit streaks) + SceneRain cones + world splashes + lens drops.
  rhi::Texture* executePost(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& hdrIn,
                            rhi::Texture& hdrTemp, rhi::Texture& depthColor, rhi::Texture& normals,
                            rhi::Texture& bloomOrHdr, rhi::Buffer& rainCB, rhi::Sampler& linearSamp,
                            const glm::mat4& invViewProj, const glm::mat4& viewProj, const glm::mat4& view,
                            const glm::vec3& cameraPos, float timeSeconds, uint32_t width, uint32_t height,
                            rhi::Texture* ssr = nullptr);

private:
  void loadTextures(rhi::Device& device);
  void buildRainMesh(rhi::Device& device);
  void updateCB(rhi::Buffer& rainCB, const glm::mat4& invViewProj, const glm::mat4& viewProj,
                const glm::mat4& view, const glm::mat4& rainOccVP, const glm::vec3& cameraPos,
                float timeSeconds, uint32_t width, uint32_t height, float amountScale);
  void renderOccluderMap(rhi::CommandList& cmd, rhi::Device& device, Scene& scene, const glm::vec3& cameraPos,
                         const glm::vec2& wind);
  void runOcclusion(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& depthColor, rhi::Buffer& rainCB,
                    uint32_t sampTable, uint32_t width, uint32_t height);
  glm::mat4 makeRainOccVP(const glm::vec3& cameraPos, const glm::vec2& wind) const;

  RainParams m_params;
  std::shared_ptr<rhi::RootSignature> m_rootFS;
  std::shared_ptr<rhi::RootSignature> m_rootMesh;
  std::shared_ptr<rhi::PipelineState> m_copySrgbPSO;
  std::shared_ptr<rhi::PipelineState> m_copyUnormPSO;
  std::shared_ptr<rhi::PipelineState> m_occPSO;
  std::shared_ptr<rhi::PipelineState> m_occDepthPSO;
  std::shared_ptr<rhi::PipelineState> m_gbufferPSO;
  std::shared_ptr<rhi::PipelineState> m_compositePSO; // puddle + mist + streaks merged
  std::shared_ptr<rhi::PipelineState> m_dropsPSO;
  std::shared_ptr<rhi::PipelineState> m_rainDropsPSO; // stateless GPU drop particles
  std::shared_ptr<rhi::PipelineState> m_wetnessPSO;   // wetness accumulate/dry update
  std::shared_ptr<rhi::PipelineState> m_sceneRainPSO;
  std::shared_ptr<rhi::PipelineState> m_splashPSO;

  std::shared_ptr<Texture> m_puddleMask;
  std::shared_ptr<Texture> m_rainSpatter;
  std::shared_ptr<Texture> m_surfaceFlow;
  std::shared_ptr<Texture> m_rainfall;
  std::shared_ptr<Texture> m_rainfallN;
  std::shared_ptr<Texture> m_moisture;
  std::shared_ptr<rhi::Texture> m_ripples;

  std::shared_ptr<rhi::Texture> m_albedoCopy;
  std::shared_ptr<rhi::Texture> m_normalCopy;
  std::shared_ptr<rhi::Texture> m_ormCopy;
  std::shared_ptr<rhi::Texture> m_occlusion;
  std::shared_ptr<rhi::Texture> m_rainSpaceDepth; // Cry-style rain-space occluder (R32)
  std::shared_ptr<rhi::Texture> m_wetnessA;       // accumulated wetness ping-pong (R16F, world-tiled)
  std::shared_ptr<rhi::Texture> m_wetnessB;

  std::shared_ptr<rhi::Buffer> m_rainVB;
  uint32_t m_rainVertexCount = 0;

  std::shared_ptr<rhi::Sampler> m_wrapSamp;
  std::shared_ptr<rhi::Sampler> m_pointSamp;
  std::shared_ptr<rhi::Sampler> m_occSamp; // comparison-ish point clamp

  glm::mat4 m_rainOccVP{1.0f};
  glm::vec4 m_sunDirIntensity{0.0f, -1.0f, 0.0f, 0.0f};
  glm::vec3 m_sunColor{1.0f};
  float m_tanHalfFovY = 0.0f;
  float m_cloudCoverage = 1.0f;
  rhi::Texture* m_weatherMap = nullptr;
  glm::vec3 m_occCamPos{1e9f};
  uint32_t m_occCooldown = 0;
  float m_wetLevel = 0.0f; // CPU mirror of overall wetness (pass gating only)
  float m_lastTime = -1.0f;
  float m_dt = 0.0f;
  bool m_wetFlip = false;
  bool m_ready = false;
  static constexpr int kRippleCount = 24;
  static constexpr int kSlices = 12;
  static constexpr uint32_t kOccSize = 512;
  static constexpr uint32_t kWetnessSize = 512;
  static constexpr uint32_t kRainParticles = 24576;
  static constexpr float kParticleRadius = 17.0f;
  static constexpr float kWetnessExtent = 96.0f;
};

} // namespace tucano
