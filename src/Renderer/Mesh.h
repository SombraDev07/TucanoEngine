#pragma once

#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec4 tangent;
  glm::vec2 uv;
  glm::vec4 color{1, 1, 1, 1};

  // Skinning. Four bone indices packed one per byte (matches DXGI_FORMAT_R8G8B8A8_UINT) plus their
  // weights. Carried on every vertex rather than in a second stream: one input layout and one PSO
  // keeps the whole renderer on a single path, at the cost of 20 bytes on static meshes.
  // A weight sum of 0 means "not skinned" and the vertex shader skips the transform entirely.
  uint32_t boneIndices = 0;
  glm::vec4 boneWeights{0.0f};

  /// Packs up to four bone influences. Weights are normalised so they sum to 1.
  void setSkinning(const uint8_t idx[4], glm::vec4 weights) {
    boneIndices = uint32_t(idx[0]) | (uint32_t(idx[1]) << 8) | (uint32_t(idx[2]) << 16) |
                  (uint32_t(idx[3]) << 24);
    const float sum = weights.x + weights.y + weights.z + weights.w;
    boneWeights = sum > 1e-6f ? weights / sum : glm::vec4(0.0f);
  }
};

struct SubMesh {
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t materialIndex = 0;
  glm::vec3 aabbMin{0};
  glm::vec3 aabbMax{0};
};

struct MeshletBounds {
  glm::vec3 center{0};
  float radius = 0;
  glm::vec3 coneAxis{0, 1, 0};
  float coneCutoff = -1.0f;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t submeshIndex = 0;
};

struct MeshletGPU {
  glm::vec3 center{0};
  float radius = 0;
  glm::vec3 coneAxis{0, 1, 0};
  float coneCutoff = -1.0f;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t materialIndex = 0;
  uint32_t pad1 = 0;
};

class Mesh {
public:
  static std::shared_ptr<Mesh> create(rhi::Device& device, const std::vector<Vertex>& vertices,
                                      const std::vector<uint32_t>& indices, std::vector<SubMesh> submeshes);

  rhi::Buffer& vertexBuffer() { return *m_vb; }
  rhi::Buffer& indexBuffer() { return *m_ib; }
  rhi::Buffer* meshletIndexBuffer() { return m_meshletIb.get(); }
  rhi::Buffer* meshletGpuBuffer() { return m_meshletGpu.get(); }
  rhi::Buffer* meshPositions() { return m_posGpu.get(); }
  rhi::Buffer* meshNormals() { return m_nrmGpu.get(); }
  rhi::Buffer* meshUVs() { return m_uvGpu.get(); }
  rhi::Buffer* meshPackedIndices() { return m_meshletIb.get(); } // uint structured view same buffer
  const std::vector<SubMesh>& submeshes() const { return m_submeshes; }
  const std::vector<MeshletBounds>& meshlets() const { return m_meshlets; }
  const std::vector<glm::vec3>& cpuPositions() const { return m_cpuPositions; }
  const std::vector<uint32_t>& packedIndices() const { return m_packedIndices; }
  uint32_t indexCount() const { return m_indexCount; }
  uint32_t meshletCount() const { return static_cast<uint32_t>(m_meshlets.size()); }
  uint32_t vertexCount() const { return m_vertexCount; }

private:
  std::shared_ptr<rhi::Buffer> m_vb;
  std::shared_ptr<rhi::Buffer> m_ib;
  std::shared_ptr<rhi::Buffer> m_meshletIb;
  std::shared_ptr<rhi::Buffer> m_meshletGpu;
  std::shared_ptr<rhi::Buffer> m_posGpu;
  std::shared_ptr<rhi::Buffer> m_nrmGpu;
  std::shared_ptr<rhi::Buffer> m_uvGpu;
  std::vector<SubMesh> m_submeshes;
  std::vector<MeshletBounds> m_meshlets;
  std::vector<uint32_t> m_packedIndices;
  std::vector<glm::vec3> m_cpuPositions;
  uint32_t m_indexCount = 0;
  uint32_t m_vertexCount = 0;
};

uint32_t cullMeshletsCPU(const Mesh& mesh, const glm::mat4& world, const glm::mat4& viewProj,
                         std::vector<uint32_t>& outVisible);

} // namespace tucano
