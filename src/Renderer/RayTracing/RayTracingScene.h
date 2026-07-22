#pragma once

#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tucano {

// Builds BLAS per unique Mesh and a TLAS from RenderObject world matrices (DXR 1.1).
class RayTracingScene {
public:
  void init(rhi::Device& device);
  void shutdown();

  // Rebuild BLAS for new meshes + update TLAS instance transforms. Call each frame before RQ passes.
  void update(rhi::Device& device, rhi::CommandList& cmd, Scene& scene);

  bool supported() const { return m_supported; }
  bool ready() const { return m_ready && m_tlas != nullptr; }
  rhi::Buffer* tlas() const { return m_tlas.get(); }
  uint32_t instanceCount() const { return m_instanceCount; }

private:
  struct BlasEntry {
    const Mesh* mesh = nullptr;
    std::shared_ptr<rhi::Buffer> blas;
    std::shared_ptr<rhi::Buffer> scratch;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
  };

  bool ensureBlas(rhi::Device& device, rhi::CommandList& cmd, Mesh& mesh);
  void rebuildTlas(rhi::Device& device, rhi::CommandList& cmd, Scene& scene);

  bool m_supported = false;
  bool m_ready = false;
  bool m_blasBuilt = false;
  uint32_t m_instanceCount = 0;
  std::unordered_map<const Mesh*, BlasEntry> m_blas;
  std::shared_ptr<rhi::Buffer> m_tlas;
  std::shared_ptr<rhi::Buffer> m_tlasScratch;
  std::shared_ptr<rhi::Buffer> m_instanceDescs; // upload heap with D3D12_RAYTRACING_INSTANCE_DESC
};

} // namespace tucano
