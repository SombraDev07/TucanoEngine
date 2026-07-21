#pragma once

#include "Renderer/Texture.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

class Texture;

// Material parameter block uploaded as root CBV.
struct MaterialGPU {
  glm::vec4 baseColorFactor{1, 1, 1, 1};
  glm::vec4 emissiveFactor{0, 0, 0, 0};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  float aoFactor = 1.0f;
  float reflectance = 0.5f;
  float clearcoat = 0.0f;
  float clearcoatRoughness = 0.1f;
  float alphaCutoff = 0.5f;
  float _pad = 0.0f;
};

struct Material {
  std::string name;
  glm::vec4 baseColorFactor{1, 1, 1, 1};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  float aoFactor = 1.0f;
  float reflectance = 0.5f;
  float clearcoat = 0.0f;
  float clearcoatRoughness = 0.1f;
  glm::vec3 emissiveFactor{0, 0, 0};
  std::shared_ptr<Texture> albedo;
  std::shared_ptr<Texture> normal;
  std::shared_ptr<Texture> metallicRoughness;
  std::shared_ptr<Texture> ao;
  std::shared_ptr<Texture> emissive;
  bool alphaMask = false;
  float alphaCutoff = 0.5f;

  // Optional master for instances (override non-null textures / factors).
  std::shared_ptr<Material> master;

  MaterialGPU toGPU() const;
  void bindParameters(MaterialGPU& out) const;
};

// Build-time shader variant key (Dagor-style #define set hashed into PSO cache).
struct ShaderVariantKey {
  uint32_t flags = 0;
  static constexpr uint32_t kNormalMap = 1u << 0;
  static constexpr uint32_t kAlphaMask = 1u << 1;
  static constexpr uint32_t kClearcoat = 1u << 2;
  static constexpr uint32_t kEmissive = 1u << 3;
  uint64_t hash() const { return flags; }
};

} // namespace tucano
