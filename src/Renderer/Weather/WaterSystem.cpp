#include "Renderer/Weather/WaterSystem.h"
#include "Platform/FileSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifndef TUCANO_SHADER_DIR
#define TUCANO_SHADER_DIR "Shaders"
#endif

namespace tucano {
namespace {

std::string shaderPath(const char* file) { return std::string(TUCANO_SHADER_DIR) + "/" + file; }

struct WaterCBData {
  glm::mat4 invViewProj;
  glm::mat4 viewProj;          // SSR projects each world-space ray sample back to the screen
  glm::vec4 cameraPos;         // xyz, time
  glm::vec4 screenSize;        // w, h, 1/w, 1/h
  glm::vec4 waveParams0;       // amplitude, frequency, speed, choppy
  glm::vec4 waveParams1;       // waveCount, foamAmount, sssIntensity, shoreHardness
  glm::vec4 waterColor;        // rgb scattering tint, fogDensity
  glm::vec4 windLevel;         // wind.x, wind.z, storminess, waterLevel
  glm::vec4 sunParams;         // sunToScene.xyz, turbidity
  glm::vec4 sunColorIntensity; // sunColor.rgb, sunIntensity
  glm::vec4 waterMisc;         // normalStrength, reflectionStrength, roughness, detailFadeDistance
  glm::vec4 ssrParams;         // steps, maxDistance, edgeFade, refractionStrength
  glm::vec4 absorptionParams;  // absorption.rgb, _
  glm::vec4 rainParams;        // intensity, rippleStrength, rippleScale, rippleSpeed
  glm::vec4 rainParams2;       // rainRoughness, rainFoam, enableSSR, _
  glm::vec4 iblParams;         // prefilteredMaxMip, iblExposure, _, _
  glm::uvec4 texIndices;       // depthIdx, hdrIdx, weatherIdx, prefilteredIdx
};
static_assert(sizeof(WaterCBData) <= 512, "WaterCB too large");

} // namespace

void WaterSystem::init(rhi::Device& device, std::shared_ptr<rhi::RootSignature> sharedRootFS) {
  rhi::GraphicsPipelineDesc d{};
  d.rootSignature = sharedRootFS;
  d.vs = rhi::ShaderBytecode::loadFromFile(shaderPath("Water_VSMain.cso"));
  d.ps = rhi::ShaderBytecode::loadFromFile(shaderPath("Water_PSMain.cso"));
  d.rtvFormats = {rhi::Format::R16G16B16A16_FLOAT};
  d.dsvFormat = rhi::Format::Unknown;
  d.depthEnable = false;
  d.cullMode = rhi::CullMode::None;
  d.useInputLayout = false;
  d.blend = rhi::BlendMode::Opaque;
  m_pso = device.createGraphicsPipeline(d);
  m_ready = true;
}

void WaterSystem::resize(uint32_t width, uint32_t height) {
  m_ready = true;
}

void WaterSystem::execute(rhi::CommandList& cmd, rhi::Texture& hdr, rhi::Texture& depthColor,
                          rhi::Texture* weatherMap, rhi::Buffer& waterCB, rhi::Sampler& linearSamp,
                          const glm::mat4& invViewProj, const glm::mat4& viewProj, const glm::mat4& view,
                          const glm::vec3& cameraPos, const glm::vec3& sunDir, const glm::vec3& sunColor,
                          float turbidity, float timeSeconds, uint32_t width, uint32_t height,
                          uint32_t depthBindless, uint32_t hdrBindless, uint32_t weatherBindless,
                          uint32_t prefilteredBindless, float iblMaxMip, float iblExposure) {
  if (!m_ready || !m_params.enabled) {
    return;
  }

  WaterCBData cb{};
  cb.invViewProj = invViewProj;
  cb.viewProj = viewProj;
  cb.cameraPos = glm::vec4(cameraPos, timeSeconds);
  cb.screenSize = glm::vec4(float(width), float(height), 1.0f / float(width), 1.0f / float(height));
  cb.waveParams0 = glm::vec4(m_params.waveAmplitude, m_params.waveFrequency, m_params.waveSpeed, m_params.waveChoppy);
  cb.waveParams1 = glm::vec4(m_params.waveCount, m_params.foamAmount, m_params.sssIntensity, m_params.shoreHardness);
  cb.waterColor = glm::vec4(m_params.waterColorR, m_params.waterColorG, m_params.waterColorB, m_params.fogDensity);
  // Rain whips up the sea state, so it also feeds the wave-direction spread (storminess).
  const float rain = std::clamp(m_params.rainIntensity, 0.0f, 1.0f);
  cb.windLevel = glm::vec4(m_params.windDirection.x, m_params.windDirection.y, rain, m_params.waterLevel);
  cb.sunParams = glm::vec4(sunDir, turbidity);
  float sunIntensity = glm::length(sunColor);
  float maxChannel = std::max({sunColor.r, sunColor.g, sunColor.b});
  glm::vec3 sunColNorm = (maxChannel > 1e-5f) ? sunColor / maxChannel : sunColor;
  cb.sunColorIntensity = glm::vec4(sunColNorm, sunIntensity * 0.1f);
  cb.waterMisc = glm::vec4(m_params.normalStrength, m_params.reflectionStrength, m_params.roughness,
                           m_params.detailFadeDistance);
  cb.ssrParams = glm::vec4(std::clamp(m_params.ssrSteps, 4.0f, 128.0f), m_params.ssrMaxDistance,
                           m_params.ssrEdgeFade, m_params.refractionStrength);
  cb.absorptionParams = glm::vec4(m_params.absorption, 0.0f);
  cb.rainParams = glm::vec4(rain, m_params.rainRippleStrength, m_params.rainRippleScale,
                            m_params.rainRippleSpeed);
  cb.rainParams2 = glm::vec4(m_params.rainRoughness, m_params.rainFoam,
                             m_params.enableSSR ? 1.0f : 0.0f, 0.0f);
  cb.iblParams = glm::vec4(iblMaxMip, iblExposure, 0.0f, 0.0f);
  cb.texIndices = glm::uvec4(depthBindless, hdrBindless, weatherBindless, prefilteredBindless);

  uint64_t cbOff = 0;
  void* mapped = waterCB.mapped();
  if (mapped) {
    std::memcpy(static_cast<uint8_t*>(mapped) + cbOff, &cb, sizeof(cb));
  }

  cmd.setPipeline(*m_pso);
  cmd.setGraphicsRootCBV(1, waterCB, cbOff);

  rhi::Viewport vp{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
  rhi::Scissor sc{0, 0, int32_t(width), int32_t(height)};
  cmd.setViewport(vp);
  cmd.setScissor(sc);

  cmd.draw(3);
}

} // namespace tucano
