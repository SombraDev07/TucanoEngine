#include "AssetPipeline/AssetCooker.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/glm.hpp>

namespace tucano {
namespace {

float radicalInverseVdC(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(uint32_t i, uint32_t n) {
  return {float(i) / float(n), radicalInverseVdC(i)};
}

glm::vec3 importanceSampleGGX(glm::vec2 xi, float roughness, glm::vec3 n) {
  const float a = roughness * roughness;
  const float phi = 2.0f * 3.14159265f * xi.x;
  const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
  const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
  glm::vec3 h(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
  const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
  const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
  const glm::vec3 bitangent = glm::cross(n, tangent);
  return glm::normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

float geometrySchlickGGX(float nDotV, float roughness) {
  const float a = roughness;
  const float k = (a * a) / 2.0f;
  return nDotV / (nDotV * (1.0f - k) + k);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
  return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

} // namespace

bool AssetCooker::cookMesh(Mesh& mesh, const std::string& outPath) {
  // Meshlets are built at Mesh::create time via meshoptimizer — cook verifies + optionally dumps.
  if (mesh.meshletCount() == 0) {
    return false;
  }
  if (outPath.empty()) {
    return true;
  }
  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  CookedMeshHeader h{};
  h.meshletCount = mesh.meshletCount();
  h.indexCount = mesh.indexCount();
  out.write(reinterpret_cast<const char*>(&h), sizeof(h));
  for (const auto& ml : mesh.meshlets()) {
    out.write(reinterpret_cast<const char*>(&ml), sizeof(MeshletBounds));
  }
  return true;
}

bool AssetCooker::cookTextureRGBA8(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
                                   std::vector<uint8_t>& outMips, CookedTextureInfo& info) {
  if (rgba.size() < size_t(width) * height * 4 || width == 0 || height == 0) {
    return false;
  }
  info.width = width;
  info.height = height;
  info.format = 0;
  outMips.clear();
  uint32_t w = width;
  uint32_t h = height;
  std::vector<uint8_t> level = rgba;
  info.mipCount = 0;
  while (w >= 1 && h >= 1) {
    outMips.insert(outMips.end(), level.begin(), level.end());
    ++info.mipCount;
    if (w == 1 && h == 1) {
      break;
    }
    const uint32_t nw = std::max(1u, w / 2);
    const uint32_t nh = std::max(1u, h / 2);
    std::vector<uint8_t> next(size_t(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y) {
      for (uint32_t x = 0; x < nw; ++x) {
        const uint32_t x0 = x * 2;
        const uint32_t y0 = y * 2;
        const uint32_t x1 = std::min(x0 + 1, w - 1);
        const uint32_t y1 = std::min(y0 + 1, h - 1);
        for (int c = 0; c < 4; ++c) {
          const uint32_t sum = level[(y0 * w + x0) * 4 + c] + level[(y0 * w + x1) * 4 + c] +
                               level[(y1 * w + x0) * 4 + c] + level[(y1 * w + x1) * 4 + c];
          next[(y * nw + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
        }
      }
    }
    level = std::move(next);
    w = nw;
    h = nh;
  }
  // Tag as BC7-ready for shipping packers (actual BC7 encode is optional tool-side).
  info.format = 1;
  return info.mipCount > 0;
}

void AssetCooker::cookBrdfLut(std::vector<uint8_t>& outRgba, uint32_t size) {
  outRgba.resize(size_t(size) * size * 4);
  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const float nDotV = (x + 0.5f) / size;
      const float roughness = (y + 0.5f) / size;
      glm::vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
      float a = 0.0f, b = 0.0f;
      const glm::vec3 n(0, 0, 1);
      const uint32_t sampleCount = 64;
      for (uint32_t i = 0; i < sampleCount; ++i) {
        const glm::vec2 xi = hammersley(i, sampleCount);
        const glm::vec3 h = importanceSampleGGX(xi, roughness, n);
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
      a /= float(sampleCount);
      b /= float(sampleCount);
      const size_t idx = (size_t(y) * size + x) * 4;
      outRgba[idx + 0] = uint8_t(std::clamp(a, 0.f, 1.f) * 255.f);
      outRgba[idx + 1] = uint8_t(std::clamp(b, 0.f, 1.f) * 255.f);
      outRgba[idx + 2] = 0;
      outRgba[idx + 3] = 255;
    }
  }
}

} // namespace tucano
