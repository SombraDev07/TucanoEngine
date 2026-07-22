#include "Renderer/Mesh.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

static_assert(sizeof(tucano::MeshletGPU) == 48, "MeshletGPU must match MeshletCull.hlsl");

namespace tucano {

std::shared_ptr<Mesh> Mesh::create(rhi::Device& device, const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices, std::vector<SubMesh> submeshes) {
  for (uint32_t idx : indices) {
    if (idx >= vertices.size()) {
      throw std::runtime_error("Mesh::create: index out of range (idx=" + std::to_string(idx) +
                               " verts=" + std::to_string(vertices.size()) + ")");
    }
  }
  for (const auto& sub : submeshes) {
    if (sub.indexCount == 0) {
      continue;
    }
    if (uint64_t(sub.indexOffset) + sub.indexCount > indices.size()) {
      throw std::runtime_error("Mesh::create: submesh range out of bounds");
    }
  }

  auto mesh = std::shared_ptr<Mesh>(new Mesh());
  rhi::BufferDesc vb{};
  vb.size = vertices.size() * sizeof(Vertex);
  vb.usage = rhi::BufferUsage::Vertex;
  vb.debugName = "MeshVB";
  mesh->m_vb = device.createBuffer(vb, vertices.data());

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
  }

  rhi::BufferDesc ib{};
  ib.size = indices.size() * sizeof(uint32_t);
  ib.usage = rhi::BufferUsage::Index;
  ib.debugName = "MeshIB";
  mesh->m_ib = device.createBuffer(ib, indices.data());

  mesh->m_submeshes = std::move(submeshes);
  mesh->m_indexCount = static_cast<uint32_t>(indices.size());
  mesh->m_vertexCount = static_cast<uint32_t>(vertices.size());
  mesh->m_cpuPositions.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    mesh->m_cpuPositions[i] = vertices[i].position;
  }
  mesh->m_packedIndices = std::move(packedIndices);
  if (!mesh->m_packedIndices.empty()) {
    rhi::BufferDesc mib{};
    mib.size = mesh->m_packedIndices.size() * sizeof(uint32_t);
    mib.usage = rhi::BufferUsage::Index | rhi::BufferUsage::Structured;
    mib.stride = sizeof(uint32_t);
    mib.debugName = "MeshletIB";
    mesh->m_meshletIb = device.createBuffer(mib, mesh->m_packedIndices.data());
  }
  if (!mesh->m_meshlets.empty()) {
    std::vector<MeshletGPU> gpu(mesh->m_meshlets.size());
    for (size_t i = 0; i < mesh->m_meshlets.size(); ++i) {
      const auto& src = mesh->m_meshlets[i];
      gpu[i].center = src.center;
      gpu[i].radius = src.radius;
      gpu[i].coneAxis = src.coneAxis;
      gpu[i].coneCutoff = src.coneCutoff;
      gpu[i].indexOffset = src.indexOffset;
      gpu[i].indexCount = src.indexCount;
      const uint32_t si = src.submeshIndex;
      gpu[i].materialIndex =
          (si < mesh->m_submeshes.size()) ? mesh->m_submeshes[si].materialIndex : 0u;
    }
    rhi::BufferDesc gdb{};
    gdb.size = gpu.size() * sizeof(MeshletGPU);
    gdb.usage = rhi::BufferUsage::Structured;
    gdb.stride = sizeof(MeshletGPU);
    gdb.debugName = "MeshletGPU";
    mesh->m_meshletGpu = device.createBuffer(gdb, gpu.data());
  }
  {
    std::vector<glm::vec4> pos(vertices.size());
    std::vector<glm::vec4> nrm(vertices.size());
    std::vector<glm::vec2> uvs(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
      pos[i] = glm::vec4(vertices[i].position, 0.0f);
      nrm[i] = glm::vec4(vertices[i].normal, 0.0f);
      uvs[i] = vertices[i].uv;
    }
    rhi::BufferDesc bd{};
    bd.usage = rhi::BufferUsage::Structured;
    bd.size = pos.size() * sizeof(glm::vec4);
    bd.stride = sizeof(glm::vec4);
    bd.debugName = "MeshPos";
    mesh->m_posGpu = device.createBuffer(bd, pos.data());
    bd.debugName = "MeshNrm";
    mesh->m_nrmGpu = device.createBuffer(bd, nrm.data());
    bd.size = uvs.size() * sizeof(glm::vec2);
    bd.stride = sizeof(glm::vec2);
    bd.debugName = "MeshUV";
    mesh->m_uvGpu = device.createBuffer(bd, uvs.data());
  }
  return mesh;
}

uint32_t cullMeshletsCPU(const Mesh& mesh, const glm::mat4& world, const glm::mat4& viewProj,
                         std::vector<uint32_t>& outVisible) {
  outVisible.clear();
  const auto& mls = mesh.meshlets();
  outVisible.reserve(mls.size());
  const glm::mat4 invVP = glm::inverse(viewProj);
  const glm::vec3 camW = glm::vec3(invVP * glm::vec4(0, 0, 0, 1));
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
    if (m.coneCutoff >= 0.0f) {
      const glm::vec3 axisWorld = glm::normalize(glm::mat3(world) * m.coneAxis);
      const glm::vec3 dir = glm::normalize(glm::vec3(cw) - camW);
      if (-glm::dot(dir, axisWorld) > m.coneCutoff) {
        continue;
      }
    }
    outVisible.push_back(i);
  }
  return static_cast<uint32_t>(outVisible.size());
}

} // namespace tucano
