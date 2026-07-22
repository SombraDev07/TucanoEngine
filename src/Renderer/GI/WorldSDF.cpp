#include "Renderer/GI/WorldSDF.h"

#include "Renderer/Material.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tucano {
namespace {

constexpr float kInf = 1.0e5f;

inline uint32_t voxelIndex(uint32_t x, uint32_t y, uint32_t z) {
  return x + y * WorldSDF::kRes + z * WorldSDF::kRes * WorldSDF::kRes;
}

inline uint32_t atlasPixel(uint32_t cascade, uint32_t x, uint32_t y, uint32_t z) {
  const uint32_t px = cascade * WorldSDF::kRes + x;
  const uint32_t py = y + z * WorldSDF::kRes;
  return py * (WorldSDF::kRes * WorldSDF::kCascades) + px;
}

// Point–triangle distance (Ericson). Returns unsigned distance.
float pointTriangleDistance(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
  const glm::vec3 ab = b - a;
  const glm::vec3 ac = c - a;
  const glm::vec3 ap = p - a;
  const float d1 = glm::dot(ab, ap);
  const float d2 = glm::dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) {
    return glm::length(ap);
  }
  const glm::vec3 bp = p - b;
  const float d3 = glm::dot(ab, bp);
  const float d4 = glm::dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) {
    return glm::length(bp);
  }
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return glm::length(ap + ab * v);
  }
  const glm::vec3 cp = p - c;
  const float d5 = glm::dot(ab, cp);
  const float d6 = glm::dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) {
    return glm::length(cp);
  }
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return glm::length(ap + ac * w);
  }
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return glm::length(bp + (c - b) * w);
  }
  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return glm::length(ap + ab * v + ac * w);
}

uint16_t floatToHalf(float f) {
  union {
    float f;
    uint32_t u;
  } v{f};
  const uint32_t sign = (v.u >> 16) & 0x8000u;
  int32_t exp = int32_t((v.u >> 23) & 0xFFu) - 127 + 15;
  uint32_t mant = v.u & 0x7FFFFFu;
  if ((v.u & 0x7FFFFFFFu) == 0) {
    return static_cast<uint16_t>(sign);
  }
  if (exp <= 0) {
    return static_cast<uint16_t>(sign);
  }
  if (exp >= 31) {
    return static_cast<uint16_t>(sign | 0x7C00u);
  }
  return static_cast<uint16_t>(sign | (uint32_t(exp) << 10) | (mant >> 13));
}

} // namespace

void WorldSDF::init(rhi::Device& device) {
  const uint32_t atlasW = kRes * kCascades;
  const uint32_t atlasH = kRes * kRes;
  m_sdfCpu.assign(atlasW * atlasH, kInf);
  m_shCpu.assign(atlasW * atlasH * 4, 0.0f);

  rhi::TextureDesc sdfDesc{};
  sdfDesc.width = atlasW;
  sdfDesc.height = atlasH;
  sdfDesc.format = rhi::Format::R32_FLOAT;
  sdfDesc.usage = rhi::TextureUsage::ShaderResource;
  sdfDesc.debugName = "WorldSDF";
  m_sdf = device.createTexture(sdfDesc, m_sdfCpu.data(), atlasW * sizeof(float));

  rhi::TextureDesc shDesc{};
  shDesc.width = atlasW;
  shDesc.height = atlasH;
  shDesc.format = rhi::Format::R16G16B16A16_FLOAT;
  shDesc.usage = rhi::TextureUsage::ShaderResource;
  shDesc.debugName = "WorldSDF_SH1";
  m_sh = device.createTexture(shDesc, nullptr, 0);
  m_ready = true;
}

void WorldSDF::fillCascadeCB(glm::vec4 out[4]) const {
  for (uint32_t c = 0; c < kCascades; ++c) {
    out[c] = glm::vec4(m_origin[c], m_extent[c]);
  }
}

float WorldSDF::sampleDistanceCpu(const glm::vec3& worldPos) const {
  if (!m_ready || m_sdfCpu.empty()) {
    return 1.0e5f;
  }
  for (uint32_t c = 0; c < kCascades; ++c) {
    const glm::vec3 local = (worldPos - m_origin[c]) / m_extent[c];
    if (local.x > 0.02f && local.y > 0.02f && local.z > 0.02f && local.x < 0.98f && local.y < 0.98f &&
        local.z < 0.98f) {
      const uint32_t x = std::min(kRes - 1u, uint32_t(local.x * kRes));
      const uint32_t y = std::min(kRes - 1u, uint32_t(local.y * kRes));
      const uint32_t z = std::min(kRes - 1u, uint32_t(local.z * kRes));
      return m_sdfCpu[atlasPixel(c, x, y, z)];
    }
  }
  return 1.0e5f;
}

glm::vec3 WorldSDF::sampleShCpu(const glm::vec3& worldPos) const {
  if (!m_ready || m_shCpu.empty()) {
    return glm::vec3(0.05f);
  }
  for (uint32_t c = 0; c < kCascades; ++c) {
    const glm::vec3 local = (worldPos - m_origin[c]) / m_extent[c];
    if (local.x > 0.02f && local.y > 0.02f && local.z > 0.02f && local.x < 0.98f && local.y < 0.98f &&
        local.z < 0.98f) {
      const uint32_t x = std::min(kRes - 1u, uint32_t(local.x * kRes));
      const uint32_t y = std::min(kRes - 1u, uint32_t(local.y * kRes));
      const uint32_t z = std::min(kRes - 1u, uint32_t(local.z * kRes));
      const uint32_t pix = atlasPixel(c, x, y, z);
      const glm::vec3 L1(m_shCpu[pix * 4 + 0], m_shCpu[pix * 4 + 1], m_shCpu[pix * 4 + 2]);
      const float L0 = m_shCpu[pix * 4 + 3];
      return glm::vec3(std::max(L0, 0.0f)) + L1 * 0.25f;
    }
  }
  return glm::vec3(0.05f);
}

void WorldSDF::update(rhi::Device& device, const Scene& scene, const glm::vec3& cameraPos,
                      const glm::vec3& sunDir, float sunIntensity, float amortizeFraction) {
  if (!m_ready) {
    init(device);
  }

  for (uint32_t c = 0; c < kCascades; ++c) {
    const float ext = m_extent[c];
    const float cell = ext / float(kRes);
    const float snap = cell * 4.0f;
    auto snapAxis = [&](float v) { return std::floor(v / snap) * snap; };
    m_origin[c] = glm::vec3(snapAxis(cameraPos.x), snapAxis(cameraPos.y - ext * 0.25f), snapAxis(cameraPos.z)) -
                  glm::vec3(ext * 0.5f);
  }

  const uint32_t budget =
      std::max(1u, static_cast<uint32_t>(std::ceil(float(kCascades) * std::clamp(amortizeFraction, 0.1f, 1.0f))));
  m_updated = 0;
  for (uint32_t n = 0; n < budget; ++n) {
    const uint32_t c = (m_cascadeCursor + n) % kCascades;
    rebuildCascade(c, scene, sunDir, sunIntensity);
    uploadCascade(device, c);
    m_updated += kRes * kRes * kRes;
  }
  m_cascadeCursor = (m_cascadeCursor + budget) % kCascades;
}

void WorldSDF::rebuildCascade(uint32_t cascade, const Scene& scene, const glm::vec3& sunDir,
                              float sunIntensity) {
  const float ext = m_extent[cascade];
  const glm::vec3 origin = m_origin[cascade];
  const float voxelSize = ext / float(kRes);
  const uint32_t N = kRes * kRes * kRes;

  std::vector<float> dist(N, kInf);
  std::vector<uint8_t> inside(N, 0);
  std::vector<uint32_t> seed(N, 0xFFFFFFFFu);
  std::vector<glm::vec3> albedoAcc(N, glm::vec3(0));
  std::vector<float> albedoW(N, 0.0f);

  auto worldToVoxel = [&](const glm::vec3& p) -> glm::ivec3 {
    const glm::vec3 local = (p - origin) / ext;
    return glm::ivec3(int(std::floor(local.x * kRes)), int(std::floor(local.y * kRes)),
                      int(std::floor(local.z * kRes)));
  };
  auto voxelCenter = [&](int x, int y, int z) -> glm::vec3 {
    return origin + (glm::vec3(float(x), float(y), float(z)) + 0.5f) * voxelSize;
  };
  auto markSurface = [&](int x, int y, int z, float dSurf, const glm::vec3& albedo) {
    if (x < 0 || y < 0 || z < 0 || x >= int(kRes) || y >= int(kRes) || z >= int(kRes)) {
      return;
    }
    const uint32_t idx = voxelIndex(uint32_t(x), uint32_t(y), uint32_t(z));
    if (dSurf < dist[idx]) {
      dist[idx] = dSurf;
      seed[idx] = idx;
    }
    albedoAcc[idx] += albedo;
    albedoW[idx] += 1.0f;
  };

  // ---- 1) Mesh-raster: triangle seeds (primary surface) ----
  uint32_t triBudget = 12288;
  for (const auto& obj : scene.objects) {
    if (!obj.mesh || triBudget == 0) {
      break;
    }
    const auto& pos = obj.mesh->cpuPositions();
    const auto& idx = obj.mesh->packedIndices();
    if (pos.empty() || idx.size() < 3) {
      continue;
    }
    const glm::mat4& w = obj.worldMatrix;
    glm::vec3 matAlbedo(0.55f, 0.5f, 0.45f);
    if (!obj.materials.empty() && obj.materials[0]) {
      matAlbedo = glm::vec3(obj.materials[0]->baseColorFactor);
    }

    for (size_t t = 0; t + 2 < idx.size() && triBudget > 0; t += 3) {
      const uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
      if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) {
        continue;
      }
      const glm::vec3 a = glm::vec3(w * glm::vec4(pos[i0], 1.0f));
      const glm::vec3 b = glm::vec3(w * glm::vec4(pos[i1], 1.0f));
      const glm::vec3 c = glm::vec3(w * glm::vec4(pos[i2], 1.0f));
      const glm::vec3 mn = glm::min(a, glm::min(b, c)) - glm::vec3(voxelSize);
      const glm::vec3 mx = glm::max(a, glm::max(b, c)) + glm::vec3(voxelSize);
      // Skip tris far outside this cascade
      if (mx.x < origin.x || mx.y < origin.y || mx.z < origin.z || mn.x > origin.x + ext ||
          mn.y > origin.y + ext || mn.z > origin.z + ext) {
        continue;
      }
      --triBudget;
      const glm::ivec3 v0 = worldToVoxel(mn);
      const glm::ivec3 v1 = worldToVoxel(mx);
      const int x0 = std::max(v0.x, 0);
      const int y0 = std::max(v0.y, 0);
      const int z0 = std::max(v0.z, 0);
      const int x1 = std::min(v1.x, int(kRes) - 1);
      const int y1 = std::min(v1.y, int(kRes) - 1);
      const int z1 = std::min(v1.z, int(kRes) - 1);
      // Cap per-tri voxel visits
      if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 512) {
        // Coarse: seed AABB corners + center only
        const glm::vec3 mid = (a + b + c) * (1.0f / 3.0f);
        const glm::ivec3 vc = worldToVoxel(mid);
        markSurface(vc.x, vc.y, vc.z, 0.0f, matAlbedo);
        continue;
      }
      for (int z = z0; z <= z1; ++z) {
        for (int y = y0; y <= y1; ++y) {
          for (int x = x0; x <= x1; ++x) {
            const float d = pointTriangleDistance(voxelCenter(x, y, z), a, b, c);
            if (d <= voxelSize * 1.35f) {
              markSurface(x, y, z, d, matAlbedo);
            }
          }
        }
      }
    }
  }

  // ---- 2) Meshlet spheres: thick interior mask (walls) ----
  uint32_t meshletBudget = 2048;
  for (const auto& obj : scene.objects) {
    if (!obj.mesh || meshletBudget == 0) {
      break;
    }
    const glm::mat4& w = obj.worldMatrix;
    const float maxScale = std::max({glm::length(glm::vec3(w[0])), glm::length(glm::vec3(w[1])),
                                     glm::length(glm::vec3(w[2]))});
    for (const auto& ml : obj.mesh->meshlets()) {
      if (meshletBudget == 0) {
        break;
      }
      const glm::vec3 c = glm::vec3(w * glm::vec4(ml.center, 1.0f));
      const float r = ml.radius * maxScale * 0.85f;
      const glm::ivec3 mn = worldToVoxel(c - glm::vec3(r));
      const glm::ivec3 mx = worldToVoxel(c + glm::vec3(r));
      const int x0 = std::max(mn.x, 0);
      const int y0 = std::max(mn.y, 0);
      const int z0 = std::max(mn.z, 0);
      const int x1 = std::min(mx.x, int(kRes) - 1);
      const int y1 = std::min(mx.y, int(kRes) - 1);
      const int z1 = std::min(mx.z, int(kRes) - 1);
      if (x0 > x1 || y0 > y1 || z0 > z1) {
        continue;
      }
      --meshletBudget;
      for (int z = z0; z <= z1; ++z) {
        for (int y = y0; y <= y1; ++y) {
          for (int x = x0; x <= x1; ++x) {
            const float dSphere = glm::length(voxelCenter(x, y, z) - c) - r;
            if (dSphere < 0.0f) {
              inside[voxelIndex(uint32_t(x), uint32_t(y), uint32_t(z))] = 1;
            }
          }
        }
      }
    }
  }

  bool anySeed = false;
  for (uint32_t i = 0; i < N; ++i) {
    if (seed[i] != 0xFFFFFFFFu) {
      anySeed = true;
      break;
    }
  }

  // ---- 3) 3D JFA ----
  if (anySeed) {
    auto decode = [](uint32_t sidx, int& x, int& y, int& z) {
      x = int(sidx % kRes);
      y = int((sidx / kRes) % kRes);
      z = int(sidx / (kRes * kRes));
    };
    for (int step : {32, 16, 8, 4, 2, 1}) {
      std::vector<float> nd = dist;
      std::vector<uint32_t> ns = seed;
      for (uint32_t z = 0; z < kRes; ++z) {
        for (uint32_t y = 0; y < kRes; ++y) {
          for (uint32_t x = 0; x < kRes; ++x) {
            const uint32_t idx = voxelIndex(x, y, z);
            float bestD = dist[idx];
            uint32_t bestS = seed[idx];
            for (int dz = -1; dz <= 1; ++dz) {
              for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                  if (dx == 0 && dy == 0 && dz == 0) {
                    continue;
                  }
                  const int nx = int(x) + dx * step;
                  const int ny = int(y) + dy * step;
                  const int nz = int(z) + dz * step;
                  if (nx < 0 || ny < 0 || nz < 0 || nx >= int(kRes) || ny >= int(kRes) || nz >= int(kRes)) {
                    continue;
                  }
                  const uint32_t nidx = voxelIndex(uint32_t(nx), uint32_t(ny), uint32_t(nz));
                  if (seed[nidx] == 0xFFFFFFFFu) {
                    continue;
                  }
                  int sx, sy, sz;
                  decode(seed[nidx], sx, sy, sz);
                  const float d = float(std::sqrt(double((int(x) - sx) * (int(x) - sx) +
                                                        (int(y) - sy) * (int(y) - sy) +
                                                        (int(z) - sz) * (int(z) - sz)))) *
                                 voxelSize;
                  if (d < bestD) {
                    bestD = d;
                    bestS = seed[nidx];
                  }
                }
              }
            }
            nd[idx] = bestD;
            ns[idx] = bestS;
          }
        }
      }
      dist.swap(nd);
      seed.swap(ns);
    }
  }

  // ---- 4) Signed distance + SH1 (L1.xyz, L0) ----
  const glm::vec3 L = glm::normalize(sunDir);
  const float sunI = std::max(sunIntensity, 0.0f);
  auto sampleDist = [&](int x, int y, int z) -> float {
    x = std::clamp(x, 0, int(kRes) - 1);
    y = std::clamp(y, 0, int(kRes) - 1);
    z = std::clamp(z, 0, int(kRes) - 1);
    float d = dist[voxelIndex(uint32_t(x), uint32_t(y), uint32_t(z))];
    if (d >= kInf * 0.5f) {
      d = ext;
    }
    if (inside[voxelIndex(uint32_t(x), uint32_t(y), uint32_t(z))]) {
      d = -std::max(d, voxelSize * 0.25f);
    }
    return d;
  };

  for (uint32_t z = 0; z < kRes; ++z) {
    for (uint32_t y = 0; y < kRes; ++y) {
      for (uint32_t x = 0; x < kRes; ++x) {
        const uint32_t idx = voxelIndex(x, y, z);
        float d = sampleDist(int(x), int(y), int(z));
        const uint32_t pix = atlasPixel(cascade, x, y, z);
        m_sdfCpu[pix] = d;

        // Finite-difference normal from SDF
        glm::vec3 n(sampleDist(int(x) + 1, int(y), int(z)) - sampleDist(int(x) - 1, int(y), int(z)),
                    sampleDist(int(x), int(y) + 1, int(z)) - sampleDist(int(x), int(y) - 1, int(z)),
                    sampleDist(int(x), int(y), int(z) + 1) - sampleDist(int(x), int(y), int(z) - 1));
        const float nLen = glm::length(n);
        n = (nLen > 1e-5f) ? (n / nLen) : glm::vec3(0, 1, 0);

        glm::vec3 albedo(0.45f, 0.42f, 0.38f);
        if (albedoW[idx] > 0.0f) {
          albedo = albedoAcc[idx] / albedoW[idx];
        }

        // Classic SH1: irradiance ≈ L0 + L1 · n  (GPU reconstructs with shading normal)
        const float ndotl = std::max(0.0f, glm::dot(n, -L));
        const float sky = 0.06f + 0.04f * std::max(0.0f, n.y);
        const float L0 = sky + sunI * 0.05f * (inside[idx] ? 0.25f : 1.0f);
        const glm::vec3 L1 = (-L) * (sunI * 0.18f * (0.35f + 0.65f * ndotl)) * albedo;

        // Pack: rgb = L1 (can be signed), a = L0 (scalar ambient)
        // GPU: (L0 + max(dot(L1, n),0)) * albedoTint — L1 already albedo-weighted
        m_shCpu[pix * 4 + 0] = L1.x;
        m_shCpu[pix * 4 + 1] = L1.y;
        m_shCpu[pix * 4 + 2] = L1.z;
        m_shCpu[pix * 4 + 3] = L0 * (0.7f + 0.3f * (albedo.x + albedo.y + albedo.z) / 3.0f);
      }
    }
  }
}

void WorldSDF::uploadCascade(rhi::Device& device, uint32_t /*cascade*/) {
  const uint32_t atlasW = kRes * kCascades;
  const uint32_t atlasH = kRes * kRes;
  device.uploadTexture(*m_sdf, m_sdfCpu.data(), atlasW, atlasH, atlasW * sizeof(float), 0, 0);

  std::vector<uint16_t> half(atlasW * atlasH * 4);
  for (size_t i = 0; i < m_shCpu.size(); ++i) {
    half[i] = floatToHalf(m_shCpu[i]);
  }
  device.uploadTexture(*m_sh, half.data(), atlasW, atlasH, atlasW * 4 * sizeof(uint16_t), 0, 0);
}

} // namespace tucano
