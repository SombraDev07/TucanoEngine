#pragma once

#include "Renderer/Mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

struct CookedMeshHeader {
  uint32_t magic = 0x4D435554; // 'TUCM'
  uint32_t version = 1;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint32_t meshletCount = 0;
  uint32_t flags = 0;
};

struct CookedTextureInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipCount = 0;
  uint32_t format = 0; // 0 = RGBA8 source, 1 = BC7 placeholder tag
};

// Offline-style cook between glTF source and runtime (Dagor daBuild-inspired stages).
class AssetCooker {
public:
  // Ensure meshlets exist (meshoptimizer) and write .tucmesh beside source if path given.
  static bool cookMesh(Mesh& mesh, const std::string& outPath = {});
  // Generate mip chain metadata / downsample RGBA8 in-place levels into contiguous buffer.
  static bool cookTextureRGBA8(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
                               std::vector<uint8_t>& outMips, CookedTextureInfo& info);
  // Build BRDF LUT bytes (same algorithm as IBL) for packaging.
  static void cookBrdfLut(std::vector<uint8_t>& outRgba, uint32_t size = 256);
};

} // namespace tucano
