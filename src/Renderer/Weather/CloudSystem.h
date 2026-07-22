#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>

namespace tucano {

struct CloudParams {
  bool enabled = true;
  bool enableShadows = true;
  bool enableGodRays = true;
  bool enableWeatherMap = true;
  bool driveRain = true;
  float coverage = 0.48f;
  float density = 1.15f;
  float altitude = 1500.0f;
  float thickness = 2400.0f;
  float shadowStrength = 0.7f;
  float temporalAlpha = 0.88f;
  float godRayStrength = 0.55f;
  float storminess = 0.35f;
  glm::vec3 wind{0.2f, 0.0f, 0.05f};
};

// Dagor-class volumetric clouds: half-res march, temporal AA, weather map, ground shadows, god rays.
class CloudSystem {
public:
  void init(rhi::Device& device);
  void resize(rhi::Device& device, uint32_t width, uint32_t height);

  CloudParams& params() { return m_params; }
  const CloudParams& params() const { return m_params; }

  // Mean weather coverage last frame (CPU estimate from settings + storminess) for rain coupling.
  float weatherRainScale() const;

  // GPU weather map written this frame (R = coverage, camera-centered ±4 km); null until first execute.
  rhi::Texture* weatherMap() const { return m_lastWeather; }

  // After Lighting (HDR has sky). Returns composited HDR (ping-pong) or nullptr if disabled.
  rhi::Texture* execute(rhi::CommandList& cmd, rhi::Device& device, rhi::Texture& hdrIn, rhi::Texture& hdrTemp,
                        rhi::Texture& depthColor, rhi::Buffer& cloudCB, rhi::Sampler& linearSamp,
                        const glm::mat4& invViewProj, const glm::mat4& viewProj, const glm::mat4& prevViewProj,
                        const glm::vec3& cameraPos, const glm::vec4& sunDirIntensity, const glm::vec3& sunColor,
                        const glm::vec3& ambientSky, float timeSeconds, uint32_t width, uint32_t height,
                        bool hasPrevCamera);

private:
  void updateCB(rhi::Buffer& cloudCB, uint64_t& bump, const glm::mat4& invViewProj, const glm::mat4& viewProj,
                const glm::mat4& prevViewProj, const glm::vec3& cameraPos, const glm::vec4& sunDirIntensity,
                const glm::vec3& sunColor, const glm::vec3& ambientSky, float timeSeconds, uint32_t width,
                uint32_t height, uint32_t depthId, uint32_t hdrId, uint32_t histId, uint32_t weatherPrevId,
                uint32_t weatherCurrId, uint32_t cloudBufId);

  CloudParams m_params;
  std::shared_ptr<rhi::RootSignature> m_rootFS;
  std::shared_ptr<rhi::PipelineState> m_marchPSO;
  std::shared_ptr<rhi::PipelineState> m_temporalPSO;
  std::shared_ptr<rhi::PipelineState> m_weatherPSO;
  std::shared_ptr<rhi::PipelineState> m_compositePSO;

  std::shared_ptr<rhi::Texture> m_cloudHalf;
  std::shared_ptr<rhi::Texture> m_cloudHalfTemp;
  std::shared_ptr<rhi::Texture> m_cloudHistory;
  std::shared_ptr<rhi::Texture> m_weatherA;
  std::shared_ptr<rhi::Texture> m_weatherB;
  std::shared_ptr<rhi::Texture> m_noiseBase;   // 128^3 Perlin-Worley RGBA8
  std::shared_ptr<rhi::Texture> m_noiseDetail; // 32^3 Worley RGBA8
  rhi::Texture* m_lastWeather = nullptr;
  uint32_t m_frame = 0;
  bool m_weatherFlip = false;
  bool m_ready = false;
  uint32_t m_halfW = 0;
  uint32_t m_halfH = 0;

  static constexpr uint32_t kWeatherSize = 256;
};

} // namespace tucano
