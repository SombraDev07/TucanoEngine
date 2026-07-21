#include "Renderer/GI/VoxelGI.h"

#include <algorithm>
#include <cmath>

namespace tucano {

void VoxelGI::init(rhi::Device& device) {
  // Pack 32^3 as RGBA8 atlas: width=kRes, height=kRes*kRes (one voxel per pixel, R channel).
  m_cpu.assign(kRes * kRes * kRes * 4, 0);
  rhi::TextureDesc d{};
  d.width = kRes;
  d.height = kRes * kRes;
  d.format = rhi::Format::R8G8B8A8_UNORM;
  d.usage = rhi::TextureUsage::ShaderResource;
  d.debugName = "VoxelOccupancyAtlas";
  m_occupancy = device.createTexture(d, m_cpu.data(), kRes * 4);
  m_ready = true;
}

void VoxelGI::update(rhi::Device& device, const Scene& scene, float amortizeFraction) {
  if (!m_ready) {
    init(device);
  }
  const glm::vec3 eye = scene.camera.position();
  m_origin = eye - glm::vec3(m_extent * 0.5f);
  const uint32_t total = kRes * kRes * kRes;
  const uint32_t budget = std::max(1u, static_cast<uint32_t>(total * std::clamp(amortizeFraction, 0.05f, 1.0f)));
  m_updatedThisFrame = 0;

  auto worldToVoxel = [&](const glm::vec3& p) -> glm::ivec3 {
    const glm::vec3 local = (p - m_origin) / m_extent;
    return glm::ivec3(int(local.x * kRes), int(local.y * kRes), int(local.z * kRes));
  };

  for (uint32_t n = 0; n < budget; ++n) {
    const uint32_t idx = (m_cursor + n) % total;
    m_cpu[idx * 4 + 0] = 0;
    m_cpu[idx * 4 + 1] = 0;
    m_cpu[idx * 4 + 2] = 0;
    m_cpu[idx * 4 + 3] = 255;
  }

  for (const auto& obj : scene.objects) {
    if (!obj.mesh) {
      continue;
    }
    const glm::vec3 p = glm::vec3(obj.worldMatrix[3]);
    const glm::ivec3 v = worldToVoxel(p);
    if (v.x < 0 || v.y < 0 || v.z < 0 || v.x >= int(kRes) || v.y >= int(kRes) || v.z >= int(kRes)) {
      continue;
    }
    const uint32_t idx = uint32_t(v.x) + uint32_t(v.y) * kRes + uint32_t(v.z) * kRes * kRes;
    m_cpu[idx * 4 + 0] = 255;
    ++m_updatedThisFrame;
  }

  m_cursor = (m_cursor + budget) % total;
  device.uploadTexture(*m_occupancy, m_cpu.data(), kRes, kRes * kRes, kRes * 4, 0, 0);
}

void SkyVisibility::init() {
  m_sh0.assign(kProbes * kProbes, 1.0f);
}

void SkyVisibility::update(const Scene& scene, const glm::vec3& cameraPos, float amortizeFraction) {
  if (m_sh0.empty()) {
    init();
  }
  const float tw = m_spacing;
  m_originXZ.x = std::floor(cameraPos.x / tw) * tw - (kProbes * 0.5f) * tw;
  m_originXZ.y = std::floor(cameraPos.z / tw) * tw - (kProbes * 0.5f) * tw;

  const uint32_t total = kProbes * kProbes;
  const uint32_t budget = std::max(1u, static_cast<uint32_t>(total * std::clamp(amortizeFraction, 0.05f, 1.0f)));
  m_updated = 0;

  for (uint32_t n = 0; n < budget; ++n) {
    const uint32_t idx = (m_cursor + n) % total;
    const int px = int(idx % kProbes);
    const int pz = int(idx / kProbes);
    const glm::vec3 probePos(m_originXZ.x + (px + 0.5f) * tw, cameraPos.y, m_originXZ.y + (pz + 0.5f) * tw);
    float occ = 0.0f;
    int hits = 0;
    for (const auto& obj : scene.objects) {
      const glm::vec3 p = glm::vec3(obj.worldMatrix[3]);
      const float d = glm::length(glm::vec2(p.x - probePos.x, p.z - probePos.z));
      if (d < tw * 2.0f) {
        occ += 1.0f - d / (tw * 2.0f);
        ++hits;
      }
    }
    const float sky = hits ? std::clamp(1.0f - occ / float(hits), 0.05f, 1.0f) : 1.0f;
    m_sh0[idx] = sky;
    ++m_updated;
  }
  m_cursor = (m_cursor + budget) % total;
}

float SkyVisibility::sample(const glm::vec3& worldPos) const {
  if (m_sh0.empty()) {
    return 1.0f;
  }
  const float u = (worldPos.x - m_originXZ.x) / m_spacing - 0.5f;
  const float v = (worldPos.z - m_originXZ.y) / m_spacing - 0.5f;
  const int x = std::clamp(int(std::floor(u)), 0, kProbes - 1);
  const int z = std::clamp(int(std::floor(v)), 0, kProbes - 1);
  return m_sh0[static_cast<size_t>(x + z * kProbes)];
}

} // namespace tucano
