#include "Renderer/Mesh.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>

namespace tucano {

std::shared_ptr<Mesh> Mesh::create(rhi::Device& device, const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices, std::vector<SubMesh> submeshes) {
  auto mesh = std::shared_ptr<Mesh>(new Mesh());
  rhi::BufferDesc vb{};
  vb.size = vertices.size() * sizeof(Vertex);
  vb.usage = rhi::BufferUsage::Vertex;
  vb.debugName = "MeshVB";
  mesh->m_vb = device.createBuffer(vb, vertices.data());

  // Build meshlets and a packed index buffer for fine-grained draws.
  std::vector<uint32_t> packedIndices;
  packedIndices.reserve(indices.size());

  for (uint32_t si = 0; si < submeshes.size(); ++si) {
    SubMesh& sub = submeshes[si];
    if (sub.indexCount < 3) {
      continue;
    }

    glm::vec3 mn(1e9f), mx(-1e9f);
    for (uint32_t i = 0; i < sub.indexCount; ++i) {
      const auto& p = vertices[indices[sub.indexOffset + i]].position;
      mn = glm::min(mn, p);
      mx = glm::max(mx, p);
    }
    sub.aabbMin = mn;
    sub.aabbMax = mx;

    const uint32_t* subIdx = indices.data() + sub.indexOffset;
    const size_t maxMeshlets = meshopt_buildMeshletsBound(sub.indexCount, 64, 124);
    std::vector<meshopt_Meshlet> mls(maxMeshlets);
    std::vector<unsigned int> mlVerts(maxMeshlets * 64);
    std::vector<unsigned char> mlTris(maxMeshlets * 124 * 3);

    size_t count =
        meshopt_buildMeshlets(mls.data(), mlVerts.data(), mlTris.data(), subIdx, sub.indexCount,
                              &vertices[0].position.x, vertices.size(), sizeof(Vertex), 64, 124, 0.0f);
    mls.resize(count);

    if (count == 0) {
      MeshletBounds mb{};
      mb.center = (mn + mx) * 0.5f;
      mb.radius = glm::length(mx - mn) * 0.5f;
      mb.coneCutoff = -1.0f;
      mb.indexOffset = static_cast<uint32_t>(packedIndices.size());
      mb.indexCount = sub.indexCount;
      mb.submeshIndex = si;
      for (uint32_t i = 0; i < sub.indexCount; ++i) {
        packedIndices.push_back(indices[sub.indexOffset + i]);
      }
      mesh->m_meshlets.push_back(mb);
      continue;
    }

    for (size_t mi = 0; mi < count; ++mi) {
      const meshopt_Meshlet& ml = mls[mi];
      meshopt_Bounds b =
          meshopt_computeMeshletBounds(&mlVerts[ml.vertex_offset], &mlTris[ml.triangle_offset], ml.triangle_count,
                                       &vertices[0].position.x, vertices.size(), sizeof(Vertex));
      MeshletBounds mb{};
      mb.center = {b.center[0], b.center[1], b.center[2]};
      mb.radius = b.radius;
      mb.coneAxis = {b.cone_axis[0], b.cone_axis[1], b.cone_axis[2]};
      mb.coneCutoff = b.cone_cutoff;
      mb.indexOffset = static_cast<uint32_t>(packedIndices.size());
      mb.indexCount = ml.triangle_count * 3;
      mb.submeshIndex = si;
      for (unsigned t = 0; t < ml.triangle_count; ++t) {
        const unsigned char* tri = &mlTris[ml.triangle_offset + t * 3];
        packedIndices.push_back(mlVerts[ml.vertex_offset + tri[0]]);
        packedIndices.push_back(mlVerts[ml.vertex_offset + tri[1]]);
        packedIndices.push_back(mlVerts[ml.vertex_offset + tri[2]]);
      }
      mesh->m_meshlets.push_back(mb);
    }

    // Keep original submesh offsets pointing into packed buffer start for classic path fallback —
    // classic path still uses original `indices`; store original ranges unchanged.
  }

  // Classic IB = original indices; meshlet draws use packed IB when enableMeshlets.
  rhi::BufferDesc ib{};
  ib.size = indices.size() * sizeof(uint32_t);
  ib.usage = rhi::BufferUsage::Index;
  ib.debugName = "MeshIB";
  mesh->m_ib = device.createBuffer(ib, indices.data());

  if (!packedIndices.empty()) {
    rhi::BufferDesc mib{};
    mib.size = packedIndices.size() * sizeof(uint32_t);
    mib.usage = rhi::BufferUsage::Index;
    mib.debugName = "MeshletIB";
    // Store meshlet IB by replacing... we need a second buffer. Add m_meshletIb to Mesh.
  }

  mesh->m_submeshes = std::move(submeshes);
  mesh->m_indexCount = static_cast<uint32_t>(indices.size());
  mesh->m_packedIndices = std::move(packedIndices);
  if (!mesh->m_packedIndices.empty()) {
    rhi::BufferDesc mib{};
    mib.size = mesh->m_packedIndices.size() * sizeof(uint32_t);
    mib.usage = rhi::BufferUsage::Index;
    mib.debugName = "MeshletIB";
    mesh->m_meshletIb = device.createBuffer(mib, mesh->m_packedIndices.data());
  }
  return mesh;
}

uint32_t cullMeshletsCPU(const Mesh& mesh, const glm::mat4& world, const glm::mat4& viewProj,
                         std::vector<uint32_t>& outVisible) {
  outVisible.clear();
  // Conservative: keep meshlets whose world-space sphere intersects the view frustum AABB
  // approximated by testing clip-space center within expanded NDC bounds.
  const auto& mls = mesh.meshlets();
  outVisible.reserve(mls.size());
  for (uint32_t i = 0; i < mls.size(); ++i) {
    const auto& m = mls[i];
    const glm::vec4 cw = world * glm::vec4(m.center, 1.0f);
    const float sx = glm::length(glm::vec3(world[0]));
    const float sy = glm::length(glm::vec3(world[1]));
    const float sz = glm::length(glm::vec3(world[2]));
    const float radius = m.radius * std::max(sx, std::max(sy, sz));
    const glm::vec4 clip = viewProj * cw;
    if (clip.w <= 0.0f) {
      continue;
    }
    const float invW = 1.0f / clip.w;
    const float cx = clip.x * invW;
    const float cy = clip.y * invW;
    const float cz = clip.z * invW;
    const float pad = (radius / std::max(clip.w, 1e-3f)) * 2.0f + 0.15f;
    if (cx < -1.0f - pad || cx > 1.0f + pad || cy < -1.0f - pad || cy > 1.0f + pad || cz < -pad ||
        cz > 1.0f + pad) {
      continue;
    }
    outVisible.push_back(i);
  }
  return static_cast<uint32_t>(outVisible.size());
}

} // namespace tucano
