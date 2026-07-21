#include "Renderer/GI/IBL.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tucano {
namespace {

constexpr float kPi = 3.14159265359f;

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

float geometrySchlickGGX(float nDotV, float roughness) {
  const float k = (roughness * roughness) / 2.0f;
  return nDotV / (nDotV * (1.0f - k) + k);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
  return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

glm::vec3 latLongToDir(float u, float v) {
  const float theta = (u * 2.0f - 1.0f) * kPi; // -pi..pi
  const float phi = (0.5f - v) * kPi;          // -pi/2..pi/2 (v=0 top)
  const float cp = std::cos(phi);
  return glm::normalize(glm::vec3(cp * std::cos(theta), std::sin(phi), cp * std::sin(theta)));
}

glm::vec2 dirToLatLong(glm::vec3 d) {
  d = glm::normalize(d);
  const float u = std::atan2(d.z, d.x) / kPi * 0.5f + 0.5f;
  const float v = 0.5f - std::asin(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
  return {u, v};
}

glm::vec3 sampleLatLong(const std::vector<float>& img, uint32_t w, uint32_t h, glm::vec3 dir) {
  const glm::vec2 uv = dirToLatLong(dir);
  const float x = uv.x * (w - 1);
  const float y = uv.y * (h - 1);
  const int x0 = (int)std::floor(x);
  const int y0 = (int)std::floor(y);
  const int x1 = std::min(x0 + 1, (int)w - 1);
  const int y1 = std::min(y0 + 1, (int)h - 1);
  const float tx = x - x0;
  const float ty = y - y0;
  auto fetch = [&](int ix, int iy) {
    const size_t i = (static_cast<size_t>(iy) * w + ix) * 4;
    return glm::vec3(img[i], img[i + 1], img[i + 2]);
  };
  const glm::vec3 c00 = fetch(x0, y0);
  const glm::vec3 c10 = fetch(x1, y0);
  const glm::vec3 c01 = fetch(x0, y1);
  const glm::vec3 c11 = fetch(x1, y1);
  return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

glm::vec3 proceduralSky(glm::vec3 dir) {
  dir = glm::normalize(dir);
  const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.35f, 0.85f, 0.25f));
  const float sun = std::pow(std::max(glm::dot(dir, sunDir), 0.0f), 256.0f);
  const float hemi = dir.y * 0.5f + 0.5f;
  const glm::vec3 zenith(0.15f, 0.28f, 0.55f);
  const glm::vec3 horizon(0.55f, 0.62f, 0.72f);
  const glm::vec3 ground(0.08f, 0.07f, 0.06f);
  glm::vec3 sky = (dir.y > 0.0f) ? glm::mix(horizon, zenith, hemi) : glm::mix(horizon, ground, -dir.y);
  sky += glm::vec3(20.0f, 18.0f, 14.0f) * sun;
  sky += glm::vec3(0.04f, 0.05f, 0.07f);
  return sky;
}

std::vector<float> buildEnvLatLong(uint32_t w, uint32_t h) {
  std::vector<float> img(static_cast<size_t>(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const float u = (x + 0.5f) / w;
      const float v = (y + 0.5f) / h;
      const glm::vec3 c = proceduralSky(latLongToDir(u, v));
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      img[i + 0] = c.r;
      img[i + 1] = c.g;
      img[i + 2] = c.b;
      img[i + 3] = 1.0f;
    }
  }
  return img;
}

std::vector<float> convolveIrradiance(const std::vector<float>& env, uint32_t ew, uint32_t eh, uint32_t w,
                                      uint32_t h) {
  std::vector<float> out(static_cast<size_t>(w) * h * 4);
  const uint32_t samples = 64;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const glm::vec3 n = latLongToDir((x + 0.5f) / w, (y + 0.5f) / h);
      const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
      const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
      const glm::vec3 bitangent = glm::cross(n, tangent);
      glm::vec3 irr(0);
      float weightSum = 0.0f;
      for (uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const float phi = 2.0f * kPi * xi.x;
        const float cosTheta = std::sqrt(1.0f - xi.y);
        const float sinTheta = std::sqrt(xi.y);
        const glm::vec3 l = glm::normalize(tangent * (std::cos(phi) * sinTheta) +
                                          bitangent * (std::sin(phi) * sinTheta) + n * cosTheta);
        const float nDotL = std::max(glm::dot(n, l), 0.0f);
        if (nDotL > 0.0f) {
          irr += sampleLatLong(env, ew, eh, l) * nDotL;
          weightSum += nDotL;
        }
      }
      irr = (kPi * irr) / std::max(weightSum, 1e-3f);
      const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
      out[idx + 0] = irr.r;
      out[idx + 1] = irr.g;
      out[idx + 2] = irr.b;
      out[idx + 3] = 1.0f;
    }
  }
  return out;
}

std::vector<float> prefilterMip(const std::vector<float>& env, uint32_t ew, uint32_t eh, uint32_t w, uint32_t hOut,
                                float roughness) {
  std::vector<float> out(static_cast<size_t>(w) * hOut * 4);
  const uint32_t samples = roughness < 0.05f ? 32u : 64u;
  for (uint32_t y = 0; y < hOut; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const glm::vec3 r = latLongToDir((x + 0.5f) / w, (y + 0.5f) / hOut);
      const glm::vec3 n = r;
      const glm::vec3 v = r;
      glm::vec3 color(0);
      float weight = 0.0f;
      for (uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const glm::vec3 halfVec = importanceSampleGGX(xi, n, roughness);
        const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, halfVec) * halfVec - v);
        const float nDotL = std::max(glm::dot(n, l), 0.0f);
        if (nDotL > 0.0f) {
          color += sampleLatLong(env, ew, eh, l) * nDotL;
          weight += nDotL;
        }
      }
      color /= std::max(weight, 1e-3f);
      const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
      out[idx + 0] = color.r;
      out[idx + 1] = color.g;
      out[idx + 2] = color.b;
      out[idx + 3] = 1.0f;
    }
  }
  return out;
}

std::shared_ptr<Texture> uploadRGBA32F(rhi::Device& device, uint32_t w, uint32_t h, const std::vector<float>& data,
                                       uint32_t mips, const char* name) {
  rhi::TextureDesc desc{};
  desc.width = w;
  desc.height = h;
  desc.mipLevels = mips;
  desc.format = rhi::Format::R32G32B32A32_FLOAT;
  desc.usage = rhi::TextureUsage::ShaderResource;
  desc.debugName = name;
  auto tex = Texture::create(device, desc, nullptr, 0);
  uint32_t mw = w;
  uint32_t mh = h;
  // Caller uploads mips separately when mips>1 with only mip0 here if data matches w*h
  if (!data.empty() && data.size() >= static_cast<size_t>(w) * h * 4) {
    device.uploadTexture(tex->resource(), data.data(), w, h, w * sizeof(float) * 4, 0, 0);
  }
  (void)mw;
  (void)mh;
  return tex;
}

} // namespace

std::shared_ptr<Texture> createBRDFLUT(rhi::Device& device, uint32_t size) {
  std::vector<float> data(static_cast<size_t>(size) * size * 2);
  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const float nDotV = (x + 0.5f) / size;
      const float roughness = (y + 0.5f) / size;
      glm::vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
      float a = 0.0f, b = 0.0f;
      const glm::vec3 n(0, 0, 1);
      const uint32_t samples = 128;
      for (uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const glm::vec3 h = importanceSampleGGX(xi, n, roughness);
        const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, h) * h - v);
        const float nDotL = std::max(l.z, 0.0f);
        const float nDotH = std::max(h.z, 0.0f);
        const float vDotH = std::max(glm::dot(v, h), 0.0f);
        if (nDotL > 0.0f) {
          const float g = geometrySmith(nDotV, nDotL, roughness);
          const float gVis = (g * vDotH) / std::max(nDotH * nDotV, 1e-4f);
          const float fc = std::pow(1.0f - vDotH, 5.0f);
          a += (1.0f - fc) * gVis;
          b += fc * gVis;
        }
      }
      a /= static_cast<float>(samples);
      b /= static_cast<float>(samples);
      const size_t idx = (static_cast<size_t>(y) * size + x) * 2;
      data[idx + 0] = a;
      data[idx + 1] = b;
    }
  }

  rhi::TextureDesc desc{};
  desc.width = size;
  desc.height = size;
  desc.format = rhi::Format::R32G32_FLOAT;
  desc.usage = rhi::TextureUsage::ShaderResource;
  desc.debugName = "BRDFLUT";
  return Texture::create(device, desc, data.data(), size * sizeof(float) * 2);
}

IBLTextures createProceduralIBL(rhi::Device& device, uint32_t envWidth, uint32_t envHeight) {
  IBLTextures out;
  out.brdfLUT = createBRDFLUT(device, 256);

  const auto env = buildEnvLatLong(envWidth, envHeight);
  const uint32_t irrW = 64;
  const uint32_t irrH = 32;
  const auto irr = convolveIrradiance(env, envWidth, envHeight, irrW, irrH);
  out.irradiance = uploadRGBA32F(device, irrW, irrH, irr, 1, "IBL_Irradiance");

  // Prefiltered with mip chain: mip0 = envWidth, each mip halves
  uint32_t mipCount = 1;
  uint32_t mw = envWidth;
  uint32_t mh = envHeight;
  while (mw > 8 && mh > 4) {
    mw = std::max(1u, mw / 2);
    mh = std::max(1u, mh / 2);
    ++mipCount;
  }
  out.maxMip = static_cast<float>(mipCount - 1);

  rhi::TextureDesc prefDesc{};
  prefDesc.width = envWidth;
  prefDesc.height = envHeight;
  prefDesc.mipLevels = mipCount;
  prefDesc.format = rhi::Format::R32G32B32A32_FLOAT;
  prefDesc.usage = rhi::TextureUsage::ShaderResource;
  prefDesc.debugName = "IBL_Prefiltered";
  out.prefiltered = Texture::create(device, prefDesc, nullptr, 0);

  mw = envWidth;
  mh = envHeight;
  for (uint32_t mip = 0; mip < mipCount; ++mip) {
    const float roughness =
        (mipCount <= 1) ? 0.0f : static_cast<float>(mip) / static_cast<float>(mipCount - 1);
    std::vector<float> mipData =
        (mip == 0) ? env : prefilterMip(env, envWidth, envHeight, mw, mh, std::max(roughness, 0.04f));
    device.uploadTexture(out.prefiltered->resource(), mipData.data(), mw, mh, mw * sizeof(float) * 4, mip, 0);
    mw = std::max(1u, mw / 2);
    mh = std::max(1u, mh / 2);
  }

  return out;
}

} // namespace tucano
