#pragma once

#include "RHI/RHI.h"
#include "Renderer/GI/WorldSDF.h"
#include "Renderer/Scene.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

// Local reflection probes: GPU cube-face RT capture → lat-long atlas + GGX mips.
class ReflectionProbes {
public:
  static constexpr uint32_t kMaxProbes = 4;
  static constexpr uint32_t kWidth = 128;
  static constexpr uint32_t kHeight = 64;
  static constexpr uint32_t kMips = 5;
  static constexpr uint32_t kFaceSize = 64;
  static constexpr uint32_t kFaces = 6;

  struct ProbeGPU {
    glm::vec4 posIntensity;
    glm::vec4 boxMin;
    glm::vec4 boxMax; // .w = maxMip
  };

  void init(rhi::Device& device);
  void placeProbes(const Scene& scene, const glm::vec3& cameraPos);

  // View-projection for cube face (D3D +X,-X,+Y,-Y,+Z,-Z).
  glm::mat4 faceViewProj(uint32_t probeIndex, uint32_t face) const;
  glm::vec3 probePosition(uint32_t probeIndex) const;

  rhi::Texture* atlas() const { return m_atlas.get(); }
  rhi::Texture* faceAtlas() const { return m_faceAtlas.get(); }
  rhi::Texture* faceDepth() const { return m_faceDepth.get(); }
  uint32_t probeCount() const { return m_count; }
  float maxMip() const { return float(kMips - 1); }
  void fillProbeCB(ProbeGPU out[kMaxProbes]) const;
  bool ready() const { return m_ready; }

  // After GPU has rendered all 6 faces into faceAtlas, convert strip into m_cpu (half RGBA face data).
  void convertFacesToLatLongCpu(uint32_t probeIndex, const uint16_t* faceRgba16, uint32_t rowPitchBytes,
                                const glm::vec3& sunDir, float sunIntensity);

  // CPU fallback (WorldSDF) when GPU capture skipped.
  void bakeProbeCpu(uint32_t index, const WorldSDF& sdf, const glm::vec3& sunDir, float sunIntensity);
  void buildMipsAndUpload(rhi::Device& device);
  void uploadMip0(rhi::Device& device);

  std::vector<float>& cpuAtlas() { return m_cpu; }
  const std::vector<float>& cpuAtlas() const { return m_cpu; }

private:
  std::shared_ptr<rhi::Texture> m_atlas;
  std::shared_ptr<rhi::Texture> m_faceAtlas; // 6*faceSize x faceSize
  std::shared_ptr<rhi::Texture> m_faceDepth;
  std::vector<float> m_cpu;
  std::array<ProbeGPU, kMaxProbes> m_probes{};
  uint32_t m_count = 0;
  bool m_ready = false;
};

} // namespace tucano
