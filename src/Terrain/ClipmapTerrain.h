#pragma once

// Continuous-LOD geometry clipmap — the CPU side. See Shaders/ClipmapTerrain.hlsl for the technique.
//
// This owns only the pieces the shader cannot derive itself: the index buffers that give the grid its
// triangle connectivity (a full grid for the innermost level, a hollow ring for the rest), and the
// per-level placement recomputed each frame from the camera. There is deliberately NO vertex buffer
// and no PSO here — the grid comes from SV_VertexID, and the renderer owns the pipeline so the draw
// slots into the existing deferred g-buffer pass (the same way instance clouds do), reusing its
// bindless and sampler bindings instead of re-deriving them.
//
// The heightmap is whatever the caller points us at via a bindless index: a static procedural field
// for now, a streamed toroidal window later. Nothing here depends on which.

#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace tucano::terrain {

class TerrainVirtualTexture;

/// Grid resolution per level, in quads per side. Must match kGridN in the shader.
inline constexpr uint32_t kClipmapGridN = 64;
inline constexpr uint32_t kClipmapGridStride = kClipmapGridN + 1;

struct ClipmapLevel {
  float spacing = 1.0f;         ///< world metres between grid vertices at this level
  glm::vec2 origin{0.0f};       ///< world-space corner of the ring (min XZ)
  float extentHalf = 0.0f;      ///< half the ring's world extent, for the morph ramp
  bool ring = false;            ///< true = hollow ring (hole covered by the finer level); false = full grid
  bool visible = true;          ///< frustum result for this frame
};

struct ClipmapTerrainDesc {
  uint32_t levelCount = 6;
  float baseSpacing = 1.5f;     ///< level 0 vertex spacing; each level doubles it
  float morphStart = 0.6f;      ///< fraction of the ring at which the morph-to-coarse ramp begins
};

class ClipmapTerrain {
public:
  ClipmapTerrain(rhi::Device& device, const ClipmapTerrainDesc& desc = {});

  /// Points the clipmap at a heightmap: its bindless SRV index, the world-space min corner and size
  /// it covers, and the metric height multiplier (1 if the texture already stores metres).
  void setHeightmap(uint32_t bindlessIndex, const glm::vec2& worldMin, float worldSize,
                    float heightScale);

  /// Recomputes per-level placement for this camera and frustum-culls the rings. Call once a frame
  /// before render.
  void update(const glm::vec3& cameraPos, const glm::mat4& viewProj);

  const std::vector<ClipmapLevel>& levels() const { return m_levels; }
  const ClipmapTerrainDesc& desc() const { return m_desc; }

  rhi::Buffer& fullIndexBuffer() { return *m_fullIB; }
  rhi::Buffer& ringIndexBuffer() { return *m_ringIB; }
  uint32_t fullIndexCount() const { return m_fullCount; }
  uint32_t ringIndexCount() const { return m_ringCount; }

  /// Optional material virtual texture the terrain samples. Null = solid slope colour. Not owned.
  void setVirtualTexture(TerrainVirtualTexture* vt) { m_vt = vt; }
  TerrainVirtualTexture* virtualTexture() const { return m_vt; }

  uint32_t heightmapIndex() const { return m_hmIndex; }
  glm::vec2 worldMin() const { return m_worldMin; }
  float invWorldSize() const { return m_invWorldSize; }
  float heightScale() const { return m_heightScale; }
  float morphStart() const { return m_desc.morphStart; }

private:
  void buildIndexBuffers(rhi::Device& device);

  ClipmapTerrainDesc m_desc;
  std::vector<ClipmapLevel> m_levels;

  std::shared_ptr<rhi::Buffer> m_fullIB;
  std::shared_ptr<rhi::Buffer> m_ringIB;
  uint32_t m_fullCount = 0;
  uint32_t m_ringCount = 0;

  uint32_t m_hmIndex = 0;
  glm::vec2 m_worldMin{0.0f};
  float m_invWorldSize = 1.0f;
  float m_heightScale = 1.0f;
  TerrainVirtualTexture* m_vt = nullptr;
};

} // namespace tucano::terrain
