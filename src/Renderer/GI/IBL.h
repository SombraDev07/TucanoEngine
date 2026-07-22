#pragma once

#include "Renderer/Texture.h"

#include <memory>
#include <string>

namespace tucano {

struct IBLTextures {
  std::shared_ptr<Texture> brdfLUT;
  std::shared_ptr<Texture> irradiance;   // lat-long
  std::shared_ptr<Texture> prefiltered;  // lat-long with mips
  float maxMip = 0.0f;
};

std::shared_ptr<Texture> createBRDFLUT(rhi::Device& device, uint32_t size = 256);

// Build IBL from HDR lat-long RGBA32F buffer (owns convolution + prefilter mips).
IBLTextures createIBLFromLatLong(rhi::Device& device, const float* rgba, uint32_t envWidth, uint32_t envHeight);

// Procedural outdoor HDR environment + convolved irradiance + GGX-prefiltered mips.
IBLTextures createProceduralIBL(rhi::Device& device, uint32_t envWidth = 512, uint32_t envHeight = 256);

// Load .hdr/.exr via stbi_loadf; falls back to procedural if path missing/fails.
IBLTextures createIBLFromHDRIFile(rhi::Device& device, const std::string& path, uint32_t maxWidth = 512);

} // namespace tucano
