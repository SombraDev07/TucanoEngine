#pragma once

#include "Renderer/Texture.h"

#include <memory>

namespace tucano {

struct IBLTextures {
  std::shared_ptr<Texture> brdfLUT;
  std::shared_ptr<Texture> irradiance;   // lat-long
  std::shared_ptr<Texture> prefiltered;  // lat-long with mips
  float maxMip = 0.0f;
};

std::shared_ptr<Texture> createBRDFLUT(rhi::Device& device, uint32_t size = 256);

// Procedural outdoor HDR environment + convolved irradiance + GGX-prefiltered mips.
IBLTextures createProceduralIBL(rhi::Device& device, uint32_t envWidth = 512, uint32_t envHeight = 256);

} // namespace tucano
