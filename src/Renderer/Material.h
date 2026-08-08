#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"
#include "Renderer/Texture.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>

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
  float fuzz = 0.0f;          // cloth / sheen amount
  float detailScale = 0.0f;   // 0 = off; else UV scale for detail maps
  float alphaCutoff = 0.5f;
  float _pad0 = 0.0f;
  glm::vec3 fuzzColor{1, 1, 1};
  float _pad1 = 0.0f;
};

struct TUCANO_TYPE() Material {
  TUCANO_FIELD(.label = "Name", .category = "Material")
  std::string name;

  // Color rather than Vec4: the storage cannot say whether four floats are a colour or a plane
  // equation, so intent is spelled out. Everything below infers its CoreType from the C++ type.
  TUCANO_FIELD(Color, .label = "Base color",
               .tooltip = "Multiplied with the albedo texture when there is one",
               .category = "Surface")
  glm::vec4 baseColorFactor{1, 1, 1, 1};

  TUCANO_FIELD(.label = "Metallic",
               .tooltip = "0 is dielectric, 1 is raw metal; values in between are not physical",
               .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
  float metallicFactor = 1.0f;

  TUCANO_FIELD(.label = "Roughness", .tooltip = "Microfacet roughness of the surface",
               .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
  float roughnessFactor = 1.0f;

  TUCANO_FIELD(.label = "Ambient occlusion",
               .tooltip = "Scales how much the baked AO map darkens indirect light",
               .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
  float aoFactor = 1.0f;

  TUCANO_FIELD(.label = "Reflectance",
               .tooltip = "Dielectric F0. 0.5 is the 4% default most surfaces want",
               .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
  float reflectance = 0.5f;

  TUCANO_FIELD(.label = "Clearcoat",
               .tooltip = "Strength of the second specular lobe: car paint, lacquer, varnish",
               .category = "Clearcoat", .minValue = 0.0f, .maxValue = 1.0f)
  float clearcoat = 0.0f;

  TUCANO_FIELD(.label = "Clearcoat roughness", .tooltip = "Roughness of the coat layer only",
               .category = "Clearcoat", .minValue = 0.0f, .maxValue = 1.0f)
  float clearcoatRoughness = 0.1f;

  TUCANO_FIELD(.label = "Fuzz", .tooltip = "Cloth sheen amount; 0 disables the lobe",
               .category = "Sheen", .minValue = 0.0f, .maxValue = 1.0f)
  float fuzz = 0.0f;

  TUCANO_FIELD(.label = "Detail scale",
               .tooltip = "UV multiplier for the detail maps. 0 turns detail off",
               .category = "Detail", .minValue = 0.0f, .maxValue = 64.0f)
  float detailScale = 0.0f;

  TUCANO_FIELD(Color, .label = "Fuzz color", .tooltip = "Tint of the sheen lobe", .category = "Sheen")
  glm::vec3 fuzzColor{1.0f, 1.0f, 1.0f};

  TUCANO_FIELD(Color, .label = "Emissive",
               .tooltip = "Radiance added on top of shading; not tone mapped away at 1.0",
               .category = "Emissive")
  glm::vec3 emissiveFactor{0, 0, 0};
  std::shared_ptr<Texture> albedo;
  std::shared_ptr<Texture> normal;
  std::shared_ptr<Texture> metallicRoughness;
  std::shared_ptr<Texture> ao;
  std::shared_ptr<Texture> emissive;
  std::shared_ptr<Texture> detailAlbedo;
  std::shared_ptr<Texture> detailNormal;
  TUCANO_FIELD(.label = "Alpha mask", .tooltip = "Cutout rather than blended transparency",
               .category = "Alpha")
  bool alphaMask = false;

  TUCANO_FIELD(.label = "Alpha cutoff", .tooltip = "Only read when Alpha mask is on",
               .category = "Alpha", .minValue = 0.0f, .maxValue = 1.0f)
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
  static constexpr uint32_t kFuzz = 1u << 4;
  static constexpr uint32_t kDetail = 1u << 5;
  uint64_t hash() const { return flags; }
};

} // namespace tucano
