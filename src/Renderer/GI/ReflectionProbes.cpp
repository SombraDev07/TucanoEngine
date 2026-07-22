#include "Renderer/GI/ReflectionProbes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tucano {
namespace {

constexpr float kPi = 3.14159265359f;

glm::vec3 latLongToDir(float u, float v) {
  const float theta = (u * 2.0f - 1.0f) * kPi;
  const float phi = (0.5f - v) * kPi;
  const float cp = std::cos(phi);
  return glm::normalize(glm::vec3(cp * std::cos(theta), std::sin(phi), cp * std::sin(theta)));
}

glm::vec2 dirToLatLong(glm::vec3 d) {
  d = glm::normalize(d);
  const float u = std::atan2(d.z, d.x) / kPi * 0.5f + 0.5f;
  const float v = 0.5f - std::asin(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
  return {u, v};
}

float radicalInverse(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(uint32_t i, uint32_t n) {
  return {static_cast<float>(i) / static_cast<float>(n), radicalInverse(i)};
}

glm::vec3 importanceSampleGGX(glm::vec2 xi, glm::vec3 n, float roughness) {
  const float a = roughness * roughness;
  const float phi = 2.0f * kPi * xi.x;
  const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
  const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
  glm::vec3 h(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
  const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
  const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
  const glm::vec3 bitangent = glm::cross(n, tangent);
  return glm::normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

glm::vec3 skyColor(const glm::vec3& dir, const glm::vec3& sunDir, float sunI) {
  const float up = std::max(dir.y, 0.0f);
  glm::vec3 sky = glm::mix(glm::vec3(0.55f, 0.65f, 0.85f), glm::vec3(0.12f, 0.22f, 0.45f), up);
  sky *= 0.35f + 0.65f * up;
  const glm::vec3 L = -glm::normalize(sunDir);
  const float sun = std::pow(std::max(0.0f, glm::dot(dir, L)), 48.0f);
  sky += glm::vec3(sunI) * sun * 3.0f;
  sky += glm::vec3(sunI) * 0.18f * std::max(0.0f, glm::dot(dir, L));
  if (dir.y < 0.0f) {
    sky = glm::mix(sky, glm::vec3(0.08f, 0.07f, 0.06f), -dir.y);
  }
  return sky;
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

glm::vec3 sampleProbeStrip(const std::vector<float>& atlas, uint32_t probe, glm::vec3 dir) {
  const glm::vec2 uv = dirToLatLong(dir);
  const float x = uv.x * (ReflectionProbes::kWidth - 1) + float(probe * ReflectionProbes::kWidth);
  const float y = uv.y * (ReflectionProbes::kHeight - 1);
  const uint32_t atlasW = ReflectionProbes::kWidth * ReflectionProbes::kMaxProbes;
  const int x0 = std::clamp(int(std::floor(x)), int(probe * ReflectionProbes::kWidth),
                            int((probe + 1) * ReflectionProbes::kWidth - 1));
  const int y0 = std::clamp(int(std::floor(y)), 0, int(ReflectionProbes::kHeight - 1));
  const int x1 = std::min(x0 + 1, int((probe + 1) * ReflectionProbes::kWidth - 1));
  const int y1 = std::min(y0 + 1, int(ReflectionProbes::kHeight - 1));
  const float tx = x - std::floor(x);
  const float ty = y - std::floor(y);
  auto fetch = [&](int ix, int iy) {
    const size_t i = (size_t(iy) * atlasW + ix) * 4;
    return glm::vec3(atlas[i], atlas[i + 1], atlas[i + 2]);
  };
  return glm::mix(glm::mix(fetch(x0, y0), fetch(x1, y0), tx), glm::mix(fetch(x0, y1), fetch(x1, y1), tx), ty);
}

} // namespace

void ReflectionProbes::init(rhi::Device& device) {
  const uint32_t atlasW = kWidth * kMaxProbes;
  m_cpu.assign(atlasW * kHeight * 4, 0.0f);

  rhi::TextureDesc d{};
  d.width = atlasW;
  d.height = kHeight;
  d.mipLevels = kMips;
  d.format = rhi::Format::R16G16B16A16_FLOAT;
  d.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::UnorderedAccess;
  d.debugName = "ReflectionProbeAtlas";
  m_atlas = device.createTexture(d, nullptr, 0);

  rhi::TextureDesc faceDesc{};
  faceDesc.width = kFaceSize * kFaces;
  faceDesc.height = kFaceSize;
  faceDesc.format = rhi::Format::R16G16B16A16_FLOAT;
  faceDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  faceDesc.debugName = "ReflectionProbeFaces";
  m_faceAtlas = device.createTexture(faceDesc, nullptr, 0);

  rhi::TextureDesc depthDesc{};
  depthDesc.width = kFaceSize;
  depthDesc.height = kFaceSize;
  depthDesc.format = rhi::Format::D32_FLOAT;
  depthDesc.usage = rhi::TextureUsage::DepthStencil;
  depthDesc.debugName = "ReflectionProbeFaceDepth";
  m_faceDepth = device.createTexture(depthDesc, nullptr, 0);

  m_ready = true;
  m_count = 0;
}

void ReflectionProbes::fillProbeCB(ProbeGPU out[kMaxProbes]) const {
  for (uint32_t i = 0; i < kMaxProbes; ++i) {
    out[i] = m_probes[i];
    out[i].boxMax.w = maxMip();
  }
}

void ReflectionProbes::placeProbes(const Scene& scene, const glm::vec3& cameraPos) {
  glm::vec3 bmin(1e9f), bmax(-1e9f);
  bool any = false;
  for (const auto& obj : scene.objects) {
    if (!obj.mesh) {
      continue;
    }
    for (const auto& sub : obj.mesh->submeshes()) {
      const glm::vec3 wa = glm::vec3(obj.worldMatrix * glm::vec4(sub.aabbMin, 1.0f));
      const glm::vec3 wb = glm::vec3(obj.worldMatrix * glm::vec4(sub.aabbMax, 1.0f));
      bmin = glm::min(bmin, glm::min(wa, wb));
      bmax = glm::max(bmax, glm::max(wa, wb));
      any = true;
    }
  }
  if (!any) {
    bmin = cameraPos - glm::vec3(8.0f);
    bmax = cameraPos + glm::vec3(8.0f);
  }
  const glm::vec3 center = (bmin + bmax) * 0.5f;
  const glm::vec3 ext = glm::max(bmax - bmin, glm::vec3(12.0f));
  bmin = center - ext * 0.5f;
  bmax = center + ext * 0.5f;

  const float midY = std::clamp(cameraPos.y, bmin.y + 1.0f, bmax.y - 0.5f);
  const glm::vec3 offsets[kMaxProbes] = {
      glm::vec3(cameraPos.x, midY + 0.4f, cameraPos.z),
      glm::vec3(center.x, midY, bmin.z + ext.z * 0.3f),
      glm::vec3(bmin.x + ext.x * 0.3f, midY, center.z),
      glm::vec3(bmax.x - ext.x * 0.3f, midY, center.z),
  };

  m_count = kMaxProbes;
  for (uint32_t i = 0; i < kMaxProbes; ++i) {
    m_probes[i].posIntensity = glm::vec4(offsets[i], 1.0f);
    m_probes[i].boxMin = glm::vec4(bmin - glm::vec3(0.5f), 0.0f);
    m_probes[i].boxMax = glm::vec4(bmax + glm::vec3(0.5f), maxMip());
  }
}

glm::vec3 ReflectionProbes::probePosition(uint32_t probeIndex) const {
  if (probeIndex >= kMaxProbes) {
    return glm::vec3(0);
  }
  return glm::vec3(m_probes[probeIndex].posIntensity);
}

glm::mat4 ReflectionProbes::faceViewProj(uint32_t probeIndex, uint32_t face) const {
  const glm::vec3 pos = probePosition(probeIndex);
  // D3D cubemap face order: +X -X +Y -Y +Z -Z
  static const glm::vec3 kTargets[6] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
  };
  static const glm::vec3 kUps[6] = {
      {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0},
  };
  const uint32_t f = face % kFaces;
  const glm::mat4 view = glm::lookAtLH(pos, pos + kTargets[f], kUps[f]);
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.05f, 120.0f);
  return proj * view;
}

void ReflectionProbes::bakeProbeCpu(uint32_t index, const WorldSDF& sdf, const glm::vec3& sunDir,
                                    float sunIntensity) {
  if (index >= kMaxProbes) {
    return;
  }
  const glm::vec3 origin = glm::vec3(m_probes[index].posIntensity);
  const float sunI = std::max(sunIntensity, 0.1f);
  const uint32_t atlasW = kWidth * kMaxProbes;

  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const float u = (float(x) + 0.5f) / float(kWidth);
      const float v = (float(y) + 0.5f) / float(kHeight);
      const glm::vec3 dir = latLongToDir(u, v);
      glm::vec3 col = skyColor(dir, sunDir, sunI);
      glm::vec3 pos = origin + dir * 0.12f;
      float traveled = 0.0f;
      for (int s = 0; s < 48; ++s) {
        const float sd = sdf.sampleDistanceCpu(pos);
        if (sd < 0.1f) {
          const glm::vec3 sh = sdf.sampleShCpu(pos);
          const float facing = std::clamp(1.0f - std::abs(glm::dot(dir, glm::vec3(0, 1, 0))) * 0.35f, 0.45f, 0.9f);
          col = glm::mix(col, glm::max(sh, glm::vec3(0.02f)) * 3.0f, facing);
          break;
        }
        const float step = std::clamp(sd * 0.85f, 0.06f, 1.25f);
        pos += dir * step;
        traveled += step;
        if (traveled > 48.0f) {
          break;
        }
      }
      const uint32_t px = index * kWidth + x;
      const uint32_t pix = (y * atlasW + px) * 4;
      m_cpu[pix + 0] = col.x;
      m_cpu[pix + 1] = col.y;
      m_cpu[pix + 2] = col.z;
      m_cpu[pix + 3] = 1.0f;
    }
  }
}

void ReflectionProbes::convertFacesToLatLongCpu(uint32_t probeIndex, const uint16_t* faceRgba16,
                                                uint32_t rowPitchBytes, const glm::vec3& sunDir,
                                                float sunIntensity) {
  if (probeIndex >= kMaxProbes || !faceRgba16) {
    return;
  }
  auto halfToFloat = [](uint16_t h) -> float {
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t out;
    if (exp == 0) {
      out = sign;
    } else if (exp == 31) {
      out = sign | 0x7F800000u | (mant << 13);
    } else {
      out = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &out, sizeof(f));
    return f;
  };
  auto sampleFace = [&](uint32_t face, float u, float v) -> glm::vec3 {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float x = float(face * kFaceSize) + u * float(kFaceSize - 1);
    const float y = v * float(kFaceSize - 1);
    const int x0 = int(x);
    const int y0 = int(y);
    const int x1 = std::min(x0 + 1, int((face + 1) * kFaceSize - 1));
    const int y1 = std::min(y0 + 1, int(kFaceSize - 1));
    const float tx = x - float(x0);
    const float ty = y - float(y0);
    auto fetch = [&](int ix, int iy) {
      const uint8_t* row = reinterpret_cast<const uint8_t*>(faceRgba16) + size_t(iy) * rowPitchBytes;
      const uint16_t* p = reinterpret_cast<const uint16_t*>(row) + size_t(ix) * 4;
      return glm::vec3(halfToFloat(p[0]), halfToFloat(p[1]), halfToFloat(p[2]));
    };
    return glm::mix(glm::mix(fetch(x0, y0), fetch(x1, y0), tx), glm::mix(fetch(x0, y1), fetch(x1, y1), tx), ty);
  };
  auto sampleCube = [&](glm::vec3 dir) -> glm::vec3 {
    dir = glm::normalize(dir);
    const glm::vec3 a = glm::abs(dir);
    uint32_t face = 0;
    float sc = 0, tc = 0, ma = 1;
    if (a.x >= a.y && a.x >= a.z) {
      face = dir.x > 0 ? 0u : 1u;
      sc = dir.x > 0 ? -dir.z : dir.z;
      tc = -dir.y;
      ma = a.x;
    } else if (a.y >= a.x && a.y >= a.z) {
      face = dir.y > 0 ? 2u : 3u;
      sc = dir.x;
      tc = dir.y > 0 ? dir.z : -dir.z;
      ma = a.y;
    } else {
      face = dir.z > 0 ? 4u : 5u;
      sc = dir.z > 0 ? dir.x : -dir.x;
      tc = -dir.y;
      ma = a.z;
    }
    const float u = sc / std::max(ma, 1e-5f) * 0.5f + 0.5f;
    const float v = tc / std::max(ma, 1e-5f) * 0.5f + 0.5f;
    glm::vec3 c = sampleFace(face, u, v);
    if (c.x + c.y + c.z < 1e-4f) {
      c = skyColor(dir, sunDir, sunIntensity);
    }
    return c;
  };

  const uint32_t atlasW = kWidth * kMaxProbes;
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const float u = (float(x) + 0.5f) / float(kWidth);
      const float v = (float(y) + 0.5f) / float(kHeight);
      const glm::vec3 col = sampleCube(latLongToDir(u, v));
      const uint32_t pix = (y * atlasW + (probeIndex * kWidth + x)) * 4;
      m_cpu[pix + 0] = col.x;
      m_cpu[pix + 1] = col.y;
      m_cpu[pix + 2] = col.z;
      m_cpu[pix + 3] = 1.0f;
    }
  }
}

void ReflectionProbes::uploadMip0(rhi::Device& device) {
  const uint32_t atlasW = kWidth * kMaxProbes;
  std::vector<uint16_t> half(atlasW * kHeight * 4);
  for (size_t i = 0; i < m_cpu.size(); ++i) {
    half[i] = floatToHalf(m_cpu[i]);
  }
  device.uploadTexture(*m_atlas, half.data(), atlasW, kHeight, atlasW * 4 * sizeof(uint16_t), 0, 0);
}

void ReflectionProbes::buildMipsAndUpload(rhi::Device& device) {
  const uint32_t atlasW0 = kWidth * kMaxProbes;
  auto toHalfUpload = [&](const std::vector<float>& rgba, uint32_t w, uint32_t h, uint32_t mip) {
    std::vector<uint16_t> half(w * h * 4);
    for (size_t i = 0; i < rgba.size() && i < half.size(); ++i) {
      half[i] = floatToHalf(rgba[i]);
    }
    device.uploadTexture(*m_atlas, half.data(), w, h, w * 4 * sizeof(uint16_t), mip, 0);
  };

  toHalfUpload(m_cpu, atlasW0, kHeight, 0);

  uint32_t mw = atlasW0;
  uint32_t mh = kHeight;
  for (uint32_t mip = 1; mip < kMips; ++mip) {
    mw = std::max(1u, mw / 2);
    mh = std::max(1u, mh / 2);
    const float roughness = float(mip) / float(kMips - 1);
    const uint32_t probeW = std::max(1u, mw / kMaxProbes);
    const uint32_t probeH = mh;
    std::vector<float> mipData(mw * mh * 4, 0.0f);
    const uint32_t samples = (mip <= 2) ? 16u : 32u;

    for (uint32_t probe = 0; probe < kMaxProbes; ++probe) {
      for (uint32_t y = 0; y < probeH; ++y) {
        for (uint32_t x = 0; x < probeW; ++x) {
          const float u = (float(x) + 0.5f) / float(probeW);
          const float v = (float(y) + 0.5f) / float(probeH);
          const glm::vec3 n = latLongToDir(u, v);
          glm::vec3 acc(0);
          float wsum = 0.0f;
          for (uint32_t s = 0; s < samples; ++s) {
            const glm::vec2 xi = hammersley(s, samples);
            const glm::vec3 h = importanceSampleGGX(xi, n, std::max(roughness, 0.04f));
            const glm::vec3 l = glm::normalize(2.0f * glm::dot(n, h) * h - n);
            const float nDotL = std::max(glm::dot(n, l), 0.0f);
            if (nDotL > 0.0f) {
              acc += sampleProbeStrip(m_cpu, probe, l) * nDotL;
              wsum += nDotL;
            }
          }
          acc = (wsum > 1e-4f) ? (acc / wsum) : sampleProbeStrip(m_cpu, probe, n);
          const uint32_t dx = probe * probeW + x;
          if (dx >= mw) {
            continue;
          }
          const size_t idx = (size_t(y) * mw + dx) * 4;
          mipData[idx + 0] = acc.x;
          mipData[idx + 1] = acc.y;
          mipData[idx + 2] = acc.z;
          mipData[idx + 3] = 1.0f;
        }
      }
    }
    toHalfUpload(mipData, mw, mh, mip);
  }
}

} // namespace tucano
