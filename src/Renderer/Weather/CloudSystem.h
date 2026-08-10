#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"

#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>

namespace tucano {

struct TUCANO_TYPE() CloudParams {
  TUCANO_FIELD(.label = "Enabled", .tooltip = "Draw the volumetric cloud layer at all", .category = "Clouds")
  bool enabled = true;
  TUCANO_FIELD(.label = "Cloud shadows", .tooltip = "Clouds cast shadows onto the world below", .category = "Lighting")
  bool enableShadows = true;
  TUCANO_FIELD(.label = "God rays", .tooltip = "Light shafts through gaps in the layer", .category = "Lighting")
  bool enableGodRays = true;
  TUCANO_FIELD(.label = "Weather map", .tooltip = "Procedural map deciding where cloud forms; off gives a uniform layer", .category = "Weather")
  bool enableWeatherMap = true;
  TUCANO_FIELD(.label = "Clouds drive rain", .tooltip = "Cloud coverage feeds RainParams::amount automatically", .category = "Weather")
  bool driveRain = true;
  TUCANO_FIELD(.label = "Coverage", .tooltip = "Fraction of sky filled. 0 is clear, 1 is overcast", .category = "Clouds", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.01f)
  float coverage = 0.48f;
  TUCANO_FIELD(.label = "Density", .tooltip = "Extinction inside the cloud; higher reads as heavier and darker", .category = "Clouds", .minValue = 0.0f, .maxValue = 4.0f, .step = 0.05f)
  float density = 1.15f;
  TUCANO_FIELD(.label = "Base altitude", .tooltip = "Height of the cloud bottom, in metres", .category = "Shape", .minValue = 100.0f, .maxValue = 8000.0f, .step = 50.0f)
  float altitude = 1500.0f;
  TUCANO_FIELD(.label = "Thickness", .tooltip = "Vertical extent of the layer, in metres", .category = "Shape", .minValue = 100.0f, .maxValue = 8000.0f, .step = 50.0f)
  float thickness = 2400.0f;
  TUCANO_FIELD(.label = "Shadow strength", .tooltip = "How much cloud shadow darkens the ground", .category = "Lighting", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
  float shadowStrength = 0.7f;
  TUCANO_FIELD(.label = "Temporal blend", .tooltip = "History weight of the temporal filter. High is stable but smears when the camera moves", .category = "Quality", .minValue = 0.0f, .maxValue = 0.99f, .step = 0.01f)
  float temporalAlpha = 0.88f;
  TUCANO_FIELD(.label = "God ray strength", .tooltip = "Intensity of the light shafts", .category = "Lighting", .minValue = 0.0f, .maxValue = 2.0f, .step = 0.05f)
  float godRayStrength = 0.55f;
  TUCANO_FIELD(.label = "Storminess", .tooltip = "Pushes the shape towards towering, anvil-topped cloud", .category = "Shape", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.02f)
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
