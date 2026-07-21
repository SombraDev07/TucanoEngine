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
  float coneCutoff = -1.0f; // cos(angle); <0 disables cone cull
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  uint32_t submeshIndex = 0;
};

class Mesh {
public:
  static std::shared_ptr<Mesh> create(rhi::Device& device, const std::vector<Vertex>& vertices,
                                      const std::vector<uint32_t>& indices, std::vector<SubMesh> submeshes);

  rhi::Buffer& vertexBuffer() { return *m_vb; }
  rhi::Buffer& indexBuffer() { return *m_ib; }
  rhi::Buffer* meshletIndexBuffer() { return m_meshletIb.get(); }
  const std::vector<SubMesh>& submeshes() const { return m_submeshes; }
  const std::vector<MeshletBounds>& meshlets() const { return m_meshlets; }
  uint32_t indexCount() const { return m_indexCount; }
  uint32_t meshletCount() const { return static_cast<uint32_t>(m_meshlets.size()); }

private:
  std::shared_ptr<rhi::Buffer> m_vb;
  std::shared_ptr<rhi::Buffer> m_ib;
  std::shared_ptr<rhi::Buffer> m_meshletIb;
  std::vector<SubMesh> m_submeshes;
  std::vector<MeshletBounds> m_meshlets;
  std::vector<uint32_t> m_packedIndices;
  uint32_t m_indexCount = 0;
};

// CPU frustum + cone cull; returns surviving meshlet indices into mesh.meshlets().
uint32_t cullMeshletsCPU(const Mesh& mesh, const glm::mat4& world, const glm::mat4& viewProj,
                         std::vector<uint32_t>& outVisible);

} // namespace tucano
