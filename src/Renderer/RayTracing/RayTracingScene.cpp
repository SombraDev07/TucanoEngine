#include "Renderer/RayTracing/RayTracingScene.h"
#include "RHI/DX12/DX12Common.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace tucano {
namespace {

void writeInstanceTransform(float out[3][4], const glm::mat4& world) {
  // D3D12 wants row-major 3x4; glm is column-major.
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      out[r][c] = world[c][r];
    }
  }
}

} // namespace

void RayTracingScene::init(rhi::Device& device) {
  m_supported = device.supportsRaytracing();
  m_ready = false;
  m_blasBuilt = false;
  if (!m_supported) {
    std::cout << "[RayTracing] GPU has no DXR — RT shadows/reflections disabled\n";
  } else {
    std::cout << "[RayTracing] DXR available — Ray Query path enabled\n";
  }
}

void RayTracingScene::shutdown() {
  m_blas.clear();
  m_tlas.reset();
  m_tlasScratch.reset();
  m_instanceDescs.reset();
  m_ready = false;
  m_blasBuilt = false;
  m_instanceCount = 0;
}

bool RayTracingScene::ensureBlas(rhi::Device& device, rhi::CommandList& cmd, Mesh& mesh) {
  auto it = m_blas.find(&mesh);
  if (it != m_blas.end() && it->second.blas) {
    return true;
  }
  if (!mesh.meshPositions() || mesh.indexCount() < 3 || mesh.vertexCount() == 0) {
    return false;
  }

  const uint32_t triCount = mesh.indexCount() / 3;
  uint64_t resultSize = 0;
  uint64_t scratchSize = 0;
  device.getRaytracingPrebuildInfo(triCount, mesh.vertexCount(), &resultSize, &scratchSize);
  if (resultSize == 0 || scratchSize == 0) {
    return false;
  }

  BlasEntry entry{};
  entry.mesh = &mesh;
  entry.indexCount = mesh.indexCount();
  entry.vertexCount = mesh.vertexCount();
  entry.blas = device.createAccelerationStructureBuffer(resultSize, "BLAS");
  {
    rhi::BufferDesc sd{};
    sd.size = (scratchSize + 255ull) & ~255ull;
    sd.usage = rhi::BufferUsage::UnorderedAccess;
    sd.debugName = "BLASScratch";
    entry.scratch = device.createBuffer(sd, nullptr);
  }
  if (!entry.blas || !entry.scratch) {
    return false;
  }

  cmd.transition(*mesh.meshPositions(), rhi::ResourceState::ShaderResource);
  cmd.transition(mesh.indexBuffer(), rhi::ResourceState::ShaderResource);
  // Dest AS buffers stay in RAYTRACING_ACCELERATION_STRUCTURE (cannot transition). Scratch is UAV.
  cmd.transition(*entry.scratch, rhi::ResourceState::UnorderedAccess);

  rhi::BlasTriangleGeometry geo{};
  geo.vertexBuffer = mesh.meshPositions();
  geo.vertexCount = mesh.vertexCount();
  geo.vertexStride = 16;
  geo.indexBuffer = &mesh.indexBuffer();
  geo.indexCount = mesh.indexCount();
  cmd.buildBottomLevelAS(*entry.blas, *entry.scratch, geo);
  cmd.transition(mesh.indexBuffer(), rhi::ResourceState::IndexBuffer);

  m_blas.emplace(&mesh, std::move(entry));
  return true;
}

void RayTracingScene::rebuildTlas(rhi::Device& device, rhi::CommandList& cmd, Scene& scene) {
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
  instances.reserve(scene.objects.size());

  for (size_t i = 0; i < scene.objects.size(); ++i) {
    auto& obj = scene.objects[i];
    if (!obj.mesh || !obj.visible) {
      continue;
    }
    auto it = m_blas.find(obj.mesh.get());
    if (it == m_blas.end() || !it->second.blas) {
      continue;
    }
    D3D12_RAYTRACING_INSTANCE_DESC desc{};
    writeInstanceTransform(desc.Transform, obj.worldMatrix);
    desc.InstanceID = static_cast<UINT>(i) & 0xFFFFFFu;
    desc.InstanceMask = 0xFF;
    desc.InstanceContributionToHitGroupIndex = 0;
    desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    desc.AccelerationStructure = static_cast<rhi::DX12Buffer&>(*it->second.blas).gpuAddress;
    instances.push_back(desc);
  }

  m_instanceCount = static_cast<uint32_t>(instances.size());
  if (m_instanceCount == 0) {
    m_ready = false;
    return;
  }

  uint64_t resultSize = 0;
  uint64_t scratchSize = 0;
  device.getRaytracingTopLevelPrebuildInfo(m_instanceCount, &resultSize, &scratchSize);
  if (resultSize == 0) {
    m_ready = false;
    return;
  }

  const uint64_t instBytes = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_instanceCount;
  if (!m_instanceDescs || m_instanceDescs->size() < instBytes) {
    rhi::BufferDesc bd{};
    bd.size = instBytes;
    bd.usage = rhi::BufferUsage::Upload;
    bd.debugName = "TLASInstances";
    m_instanceDescs = device.createBuffer(bd, nullptr);
  }
  std::memcpy(m_instanceDescs->mapped(), instances.data(), static_cast<size_t>(instBytes));

  if (!m_tlas || m_tlas->size() < resultSize) {
    m_tlas = device.createAccelerationStructureBuffer(resultSize, "TLAS");
  }
  if (!m_tlasScratch || m_tlasScratch->size() < scratchSize) {
    rhi::BufferDesc sd{};
    sd.size = (scratchSize + 255ull) & ~255ull;
    sd.usage = rhi::BufferUsage::UnorderedAccess;
    sd.debugName = "TLASScratch";
    m_tlasScratch = device.createBuffer(sd, nullptr);
  }
  if (!m_tlas || !m_tlasScratch) {
    m_ready = false;
    return;
  }

  cmd.transition(*m_tlasScratch, rhi::ResourceState::UnorderedAccess);
  cmd.buildTopLevelAS(*m_tlas, *m_tlasScratch, *m_instanceDescs, m_instanceCount);
  m_ready = true;
}

void RayTracingScene::update(rhi::Device& device, rhi::CommandList& cmd, Scene& scene) {
  if (!m_supported) {
    return;
  }

  bool needBlas = !m_blasBuilt;
  for (auto& obj : scene.objects) {
    if (!obj.mesh || !obj.visible) {
      continue;
    }
    if (m_blas.find(obj.mesh.get()) == m_blas.end()) {
      needBlas = true;
      break;
    }
  }

  if (needBlas) {
    for (auto& obj : scene.objects) {
      if (obj.mesh) {
        ensureBlas(device, cmd, *obj.mesh);
      }
    }
    m_blasBuilt = true;
  }

  rebuildTlas(device, cmd, scene);
}

} // namespace tucano
