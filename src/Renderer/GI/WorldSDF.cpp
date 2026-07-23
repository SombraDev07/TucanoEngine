#include "Renderer/GI/WorldSDF.h"

#include "Renderer/Material.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef TUCANO_SHADER_DIR
#define TUCANO_SHADER_DIR "shaders"
#endif

namespace tucano {
namespace {

constexpr float kInf = 1.0e5f;
constexpr uint32_t kEmptySeed = 0xFFFFFFFFu;

std::string shaderPath(const char* file) { return std::string(TUCANO_SHADER_DIR) + "/" + file; }

inline uint32_t voxelIndex(uint32_t x, uint32_t y, uint32_t z) {
  return x + y * WorldSDF::kRes + z * WorldSDF::kRes * WorldSDF::kRes;
}

// Point–triangle distance (Ericson). Returns unsigned distance.
float pointTriangleDistance(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
  const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) {
    return glm::length(ap);
  }
  const glm::vec3 bp = p - b;
  const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) {
    return glm::length(bp);
  }
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    return glm::length(ap + ab * (d1 / (d1 - d3)));
  }
  const glm::vec3 cp = p - c;
  const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) {
    return glm::length(cp);
  }
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    return glm::length(ap + ac * (d2 / (d2 - d6)));
  }
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    return glm::length(bp + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))));
  }
  const float denom = 1.0f / (va + vb + vc);
  return glm::length(ap + ab * (vb * denom) + ac * (vc * denom));
}

uint32_t packRGBA8(const glm::vec3& albedo, bool inside) {
  auto u8 = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return u8(albedo.r) | (u8(albedo.g) << 8) | (u8(albedo.b) << 16) | (uint32_t(inside ? 255 : 0) << 24);
}

rhi::DX12Texture& dxTex(rhi::Texture& t) { return static_cast<rhi::DX12Texture&>(t); }

} // namespace

void WorldSDF::init(rhi::Device& device) {
  const uint32_t atlasW = kRes * kCascades;
  const uint32_t atlasH = kRes * kRes;
  m_sdfCpu.assign(atlasW * atlasH, kInf);
  m_shCpu.assign(atlasW * atlasH * 4, 0.0f);

  const auto uavSrv = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;

  rhi::TextureDesc sdfDesc{};
  sdfDesc.width = atlasW;
  sdfDesc.height = atlasH;
  sdfDesc.format = rhi::Format::R32_FLOAT;
  sdfDesc.usage = uavSrv;
  sdfDesc.debugName = "WorldSDF";
  m_sdf = device.createTexture(sdfDesc, nullptr, 0);

  rhi::TextureDesc shDesc = sdfDesc;
  shDesc.format = rhi::Format::R16G16B16A16_FLOAT;
  shDesc.debugName = "WorldSDF_SH1";
  m_sh = device.createTexture(shDesc, nullptr, 0);
  m_ready = true;
}

void WorldSDF::ensureGpu(rhi::Device& device) {
  if (m_gpuReady) {
    return;
  }
  const auto uavSrv = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
  auto makeVol = [&](rhi::Format fmt, const char* name) {
    rhi::TextureDesc d{};
    d.width = d.height = kRes;
    d.depth = kRes;
    d.format = fmt;
    d.usage = uavSrv;
    d.debugName = name;
    return device.createTexture(d, nullptr, 0);
  };
  m_seedA = makeVol(rhi::Format::R32_UINT, "SdfSeedA");
  m_seedB = makeVol(rhi::Format::R32_UINT, "SdfSeedB");
  m_aux = makeVol(rhi::Format::R8G8B8A8_UNORM, "SdfAux");

  m_computeRoot = device.createComputeRootSignature();
  auto load = [](const char* f) { return rhi::ShaderBytecode::loadFromFile(shaderPath(f)); };
  rhi::ComputePipelineDesc jd{};
  jd.rootSignature = m_computeRoot;
  jd.cs = load("SdfJfa_CSMain.cso");
  m_jfaPSO = device.createComputePipeline(jd);
  rhi::ComputePipelineDesc fd{};
  fd.rootSignature = m_computeRoot;
  fd.cs = load("SdfFinalize_CSMain.cso");
  m_finalizePSO = device.createComputePipeline(fd);

  const uint32_t N = kRes * kRes * kRes;
  m_seedCpu.assign(N, kEmptySeed);
  m_auxCpu.assign(N, 0);

  // Persistent upload staging (rows are already 256-aligned: kRes*4 bytes == 256).
  rhi::BufferDesc ub{};
  ub.size = uint64_t(N) * sizeof(uint32_t);
  ub.usage = rhi::BufferUsage::Upload;
  ub.debugName = "SdfSeedUpload";
  m_seedUpload = device.createBuffer(ub, nullptr);
  ub.debugName = "SdfAuxUpload";
  m_auxUpload = device.createBuffer(ub, nullptr);
  m_gpuReady = true;
}

void WorldSDF::fillCascadeCB(glm::vec4 out[4]) const {
  for (uint32_t c = 0; c < kCascades; ++c) {
    out[c] = glm::vec4(m_origin[c], m_extent[c]);
  }
}

float WorldSDF::sampleDistanceCpu(const glm::vec3&) const { return kInf; }
glm::vec3 WorldSDF::sampleShCpu(const glm::vec3&) const { return glm::vec3(0.05f); }

void WorldSDF::seedCascade(uint32_t cascade, const Scene& scene) {
  const float ext = m_extent[cascade];
  const glm::vec3 origin = m_origin[cascade];
  const float voxelSize = ext / float(kRes);
  const uint32_t N = kRes * kRes * kRes;

  std::fill(m_seedCpu.begin(), m_seedCpu.end(), kEmptySeed);
  std::fill(m_auxCpu.begin(), m_auxCpu.end(), 0u);
  std::vector<float> dist(N, kInf);
  std::vector<glm::vec3> albedoAcc(N, glm::vec3(0));
  std::vector<float> albedoW(N, 0.0f);
  std::vector<uint8_t> inside(N, 0);

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
      m_seedCpu[idx] = idx; // this voxel is its own nearest seed
    }
    albedoAcc[idx] += albedo;
    albedoW[idx] += 1.0f;
  };

  // 1) Triangle seeds (primary surface).
  uint32_t triBudget = 12288;
  for (const auto& obj : scene.objects) {
    if (!obj.visible) {
      continue;
    }
    if (!obj.mesh || triBudget == 0) {
      break;
    }
    const auto& pos = obj.mesh->cpuPositions();
    const auto& idxs = obj.mesh->packedIndices();
    if (pos.empty() || idxs.size() < 3) {
      continue;
    }
    const glm::mat4& w = obj.worldMatrix;
    glm::vec3 matAlbedo(0.55f, 0.5f, 0.45f);
    if (!obj.materials.empty() && obj.materials[0]) {
      matAlbedo = glm::vec3(obj.materials[0]->baseColorFactor);
    }
    for (size_t t = 0; t + 2 < idxs.size() && triBudget > 0; t += 3) {
      const uint32_t i0 = idxs[t], i1 = idxs[t + 1], i2 = idxs[t + 2];
      if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) {
        continue;
      }
      const glm::vec3 a = glm::vec3(w * glm::vec4(pos[i0], 1.0f));
      const glm::vec3 b = glm::vec3(w * glm::vec4(pos[i1], 1.0f));
      const glm::vec3 c = glm::vec3(w * glm::vec4(pos[i2], 1.0f));
      const glm::vec3 mn = glm::min(a, glm::min(b, c)) - glm::vec3(voxelSize);
      const glm::vec3 mx = glm::max(a, glm::max(b, c)) + glm::vec3(voxelSize);
      if (mx.x < origin.x || mx.y < origin.y || mx.z < origin.z || mn.x > origin.x + ext ||
          mn.y > origin.y + ext || mn.z > origin.z + ext) {
        continue;
      }
      --triBudget;
      const glm::ivec3 v0 = worldToVoxel(mn);
      const glm::ivec3 v1 = worldToVoxel(mx);
      const int x0 = std::max(v0.x, 0), y0 = std::max(v0.y, 0), z0 = std::max(v0.z, 0);
      const int x1 = std::min(v1.x, int(kRes) - 1), y1 = std::min(v1.y, int(kRes) - 1),
                z1 = std::min(v1.z, int(kRes) - 1);
      if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 512) {
        const glm::vec3 mid = (a + b + c) * (1.0f / 3.0f);
        const glm::ivec3 vcm = worldToVoxel(mid);
        markSurface(vcm.x, vcm.y, vcm.z, 0.0f, matAlbedo);
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

  // 2) Meshlet spheres → interior mask (walls).
  uint32_t meshletBudget = 2048;
  for (const auto& obj : scene.objects) {
    if (!obj.visible) {
      continue;
    }
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
      const int x0 = std::max(mn.x, 0), y0 = std::max(mn.y, 0), z0 = std::max(mn.z, 0);
      const int x1 = std::min(mx.x, int(kRes) - 1), y1 = std::min(mx.y, int(kRes) - 1),
                z1 = std::min(mx.z, int(kRes) - 1);
      if (x0 > x1 || y0 > y1 || z0 > z1) {
        continue;
      }
      --meshletBudget;
      for (int z = z0; z <= z1; ++z) {
        for (int y = y0; y <= y1; ++y) {
          for (int x = x0; x <= x1; ++x) {
            if (glm::length(voxelCenter(x, y, z) - c) - r < 0.0f) {
              inside[voxelIndex(uint32_t(x), uint32_t(y), uint32_t(z))] = 1;
            }
          }
        }
      }
    }
  }

  // Pack aux (albedo + inside) for the finalize pass.
  for (uint32_t i = 0; i < N; ++i) {
    glm::vec3 albedo(0.45f, 0.42f, 0.38f);
    if (albedoW[i] > 0.0f) {
      albedo = albedoAcc[i] / albedoW[i];
    }
    m_auxCpu[i] = packRGBA8(albedo, inside[i] != 0);
  }
}

void WorldSDF::update(rhi::Device& device, rhi::CommandList& cmd, const Scene& scene,
                      const glm::vec3& cameraPos, const glm::vec3& sunDir, float sunIntensity,
                      float amortizeFraction) {
  if (!m_ready) {
    init(device);
  }
  ensureGpu(device);

  for (uint32_t c = 0; c < kCascades; ++c) {
    const float ext = m_extent[c];
    const float cell = ext / float(kRes);
    const float snap = cell * 4.0f;
    auto snapAxis = [&](float v) { return std::floor(v / snap) * snap; };
    m_origin[c] = glm::vec3(snapAxis(cameraPos.x), snapAxis(cameraPos.y - ext * 0.25f), snapAxis(cameraPos.z)) -
                  glm::vec3(ext * 0.5f);
  }

  auto& dx = static_cast<rhi::DX12Device&>(device);
  const uint32_t budget =
      std::max(1u, static_cast<uint32_t>(std::ceil(float(kCascades) * std::clamp(amortizeFraction, 0.1f, 1.0f))));
  m_updated = 0;

  for (uint32_t n = 0; n < budget; ++n) {
    const uint32_t c = (m_cascadeCursor + n) % kCascades;
    const float ext = m_extent[c];
    const float voxelSize = ext / float(kRes);

    // --- CPU seed → upload buffers → copy into the 3D volumes on this command list ---
    seedCascade(c, scene);
    std::memcpy(m_seedUpload->mapped(), m_seedCpu.data(), m_seedCpu.size() * sizeof(uint32_t));
    std::memcpy(m_auxUpload->mapped(), m_auxCpu.data(), m_auxCpu.size() * sizeof(uint32_t));
    cmd.transition(*m_seedA, rhi::ResourceState::CopyDst);
    cmd.copyBufferToTexture(*m_seedUpload, *m_seedA, kRes, kRes, kRes, rhi::Format::R32_UINT);
    cmd.transition(*m_aux, rhi::ResourceState::CopyDst);
    cmd.copyBufferToTexture(*m_auxUpload, *m_aux, kRes, kRes, kRes, rhi::Format::R8G8B8A8_UNORM);

    // --- GPU JFA: ping-pong seedA ↔ seedB over decreasing steps ---
    cmd.setRootSignature(*m_computeRoot);
    cmd.setPipeline(*m_jfaPSO);
    rhi::Texture* src = m_seedA.get();
    rhi::Texture* dst = m_seedB.get();
    const uint32_t groups = (kRes + 3) / 4;
    for (uint32_t step = kRes / 2; step >= 1; step >>= 1) {
      cmd.transition(*src, rhi::ResourceState::ShaderResource);
      cmd.transition(*dst, rhi::ResourceState::UnorderedAccess);
      const uint32_t jfaConsts[4] = {step, kRes, 0, 0};
      cmd.setComputeRootConstants(0, jfaConsts, 4);
      const D3D12_CPU_DESCRIPTOR_HANDLE srv[] = {dxTex(*src).srvCpu};
      const D3D12_CPU_DESCRIPTOR_HANDLE uav[] = {dxTex(*dst).uavCpu};
      cmd.setComputeRootSrvTable(2, dx.writeSrvTable(srv, 1));
      cmd.setComputeRootUavTable(3, dx.writeUavTable(uav, 1));
      cmd.dispatch(groups, groups, groups);
      std::swap(src, dst);
    }

    // --- GPU finalize: seed(src) + aux → SDF/SH atlas ---
    cmd.transition(*src, rhi::ResourceState::ShaderResource);
    cmd.transition(*m_aux, rhi::ResourceState::ShaderResource);
    cmd.transition(*m_sdf, rhi::ResourceState::UnorderedAccess);
    cmd.transition(*m_sh, rhi::ResourceState::UnorderedAccess);
    cmd.setPipeline(*m_finalizePSO);
    struct {
      float sun[4];
      uint32_t res, cascade;
      float voxelSize, ext;
    } fc{{sunDir.x, sunDir.y, sunDir.z, sunIntensity}, kRes, c, voxelSize, ext};
    cmd.setComputeRootConstants(0, &fc, 8);
    const D3D12_CPU_DESCRIPTOR_HANDLE fsrv[] = {dxTex(*src).srvCpu, dxTex(*m_aux).srvCpu};
    const D3D12_CPU_DESCRIPTOR_HANDLE fuav[] = {dxTex(*m_sdf).uavCpu, dxTex(*m_sh).uavCpu};
    cmd.setComputeRootSrvTable(2, dx.writeSrvTable(fsrv, 2));
    cmd.setComputeRootUavTable(3, dx.writeUavTable(fuav, 2));
    cmd.dispatch(groups, groups, groups);

    m_updated += kRes * kRes * kRes;
  }
  cmd.transition(*m_sdf, rhi::ResourceState::ShaderResource);
  cmd.transition(*m_sh, rhi::ResourceState::ShaderResource);
  m_cascadeCursor = (m_cascadeCursor + budget) % kCascades;
}

} // namespace tucano
