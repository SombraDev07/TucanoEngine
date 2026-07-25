#pragma once

// WM-6: the GPU side of the instance cloud.
//
// Uploads a cell's instances once, then each frame dispatches InstanceCull.hlsl to frustum-cull and
// LOD-select them, compacting the survivors into a visible-index buffer and growing the instance
// count of a DrawIndexedInstanced argument block. The renderer then issues ONE drawIndexedIndirect
// against that block — thousands of props, one draw.
//
// Like GpuCellCuller (WM-4) this is DX12-aware and lives beside the pure World module rather than
// in it. The cull() overload here blocks and reads back so it can be diffed against the CPU
// reference in the gate; cullForDraw() is the non-blocking per-frame path that leaves the results
// on the GPU for an indirect draw. The instance and visible buffers are exposed so the renderer can
// bind them.

#include "RHI/RHI.h"
#include "World/FrustumCull.h"
#include "World/InstanceCloud.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace tucano::world {

class InstanceCloudCuller {
public:
  InstanceCloudCuller(rhi::Device& device, rhi::RootSignature& computeRoot, rhi::PipelineState& cullPso);

  /// Uploads the instance set and remembers the per-instance mesh index count (for the draw args).
  /// Call once when a cell loads; cheap to call again if the set changes.
  void setInstances(const std::vector<InstanceGpu>& instances, uint32_t meshIndexCount);

  uint32_t instanceCount() const { return m_instanceCount; }
  uint32_t meshIndexCount() const { return m_meshIndexCount; }

  /// The instance buffer (SRV) the draw vertex shader reads by compacted index. Null before
  /// setInstances().
  rhi::Buffer* instanceBuffer() const { return m_instanceBuffer.get(); }
  /// Compacted visible original-indices (SRV), valid after a cull dispatch this frame.
  rhi::Buffer* visibleBuffer() const { return m_visibleBuffer.get(); }
  /// DrawIndexedInstanced args (element 0), for drawIndexedIndirect after a cull this frame.
  rhi::Buffer* argsBuffer() const { return m_argsBuffer.get(); }

  /// Blocking cull + readback, for the parity gate. Returns the visible original indices (compaction
  /// order) and, via out params, the DrawIndexedArgs the GPU produced. Do not call per frame.
  std::vector<uint32_t> cull(const glm::mat4& viewProj, const glm::vec3& observer,
                             const CullConfig& cfg, DrawIndexedArgs* outArgs = nullptr);

  /// Records the cull dispatch onto `cmd` and leaves visibleBuffer()/argsBuffer() ready for an
  /// indirect draw later in the same frame. Non-blocking — the per-frame render path. The caller is
  /// responsible for having called beginFrame() and setDescriptorHeap() as usual.
  void cullForDraw(rhi::CommandList& cmd, const glm::mat4& viewProj, const glm::vec3& observer,
                   const CullConfig& cfg);

private:
  void writeConstants(const glm::mat4& viewProj, const glm::vec3& observer, const CullConfig& cfg);
  void recordCull(rhi::CommandList& cmd);

  rhi::Device& m_device;
  rhi::RootSignature& m_computeRoot;
  rhi::PipelineState& m_cullPso;

  uint32_t m_instanceCount = 0;
  uint32_t m_capacity = 0;
  uint32_t m_meshIndexCount = 0;

  std::shared_ptr<rhi::Buffer> m_instanceBuffer; ///< StructuredBuffer<InstanceGpu>, upload
  std::shared_ptr<rhi::Buffer> m_visibleBuffer;  ///< RWStructuredBuffer<uint>, default
  std::shared_ptr<rhi::Buffer> m_argsBuffer;     ///< RWStructuredBuffer<DrawIndexedArgs>, default+indirect
  std::shared_ptr<rhi::Buffer> m_cb;             ///< InstanceCullCB, upload
  std::shared_ptr<rhi::Buffer> m_argsInit;       ///< upload template to reset the args each dispatch
  std::shared_ptr<rhi::Buffer> m_visibleReadback;
  std::shared_ptr<rhi::Buffer> m_argsReadback;
};

} // namespace tucano::world
