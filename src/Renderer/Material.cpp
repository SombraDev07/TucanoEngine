#include "Renderer/Material.h"

namespace tucano {

MaterialGPU Material::toGPU() const {
  MaterialGPU g{};
  bindParameters(g);
  return g;
}

void Material::bindParameters(MaterialGPU& out) const {
  const Material* src = this;
  // Inherit from master then apply local overrides.
  if (master) {
    out.baseColorFactor = master->baseColorFactor;
    out.emissiveFactor = glm::vec4(master->emissiveFactor, 0.0f);
    out.metallicFactor = master->metallicFactor;
    out.roughnessFactor = master->roughnessFactor;
    out.aoFactor = master->aoFactor;
    out.reflectance = master->reflectance;
    out.clearcoat = master->clearcoat;
    out.clearcoatRoughness = master->clearcoatRoughness;
    out.alphaCutoff = master->alphaCutoff;
  }
  out.baseColorFactor = src->baseColorFactor;
  out.emissiveFactor = glm::vec4(src->emissiveFactor, 0.0f);
  out.metallicFactor = src->metallicFactor;
  out.roughnessFactor = src->roughnessFactor;
  out.aoFactor = src->aoFactor;
  out.reflectance = src->reflectance;
  out.clearcoat = src->clearcoat;
  out.clearcoatRoughness = src->clearcoatRoughness;
  out.alphaCutoff = src->alphaCutoff;
}

} // namespace tucano
