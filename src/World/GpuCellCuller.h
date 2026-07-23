#pragma once

// WM-4: the GPU side of cell culling.
//
// Uploads a set of cell AABBs, dispatches CellCull.hlsl (one thread per cell), and reads back the
// compact list of visible cells. The compute shader is a transliteration of WorldCuller; this
// class exists so the two can be run on the same input and compared. In the real renderer the
// visible buffer would feed indirect draws without ever touching the CPU — the readback here is
// for the parity test and for debug, not for the per-frame path.
//
// This is DX12-aware (it needs the device's descriptor-table writers), like the renderer. It is
// intentionally NOT in the pure World module, which stays free of RHI dependencies.

#include "RHI/RHI.h"
#include "World/FrustumCull.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace tucano::world {

class GpuCellCuller {
public:
  GpuCellCuller(rhi::Device& device, rhi::RootSignature& computeRoot,
                rhi::PipelineState& cullPso);

  /// Runs the cull on the GPU and returns the visible cells. Blocks: it dispatches, copies the
  /// result to a readback buffer, waits for the GPU, and maps it. That makes it usable in a test
  /// and a debug tool, not in a hot frame.
  ///
  /// `mins`/`maxs`/`ids` describe the cells; the three arrays are parallel. The frustum planes are
  /// computed on the CPU from `viewProj` (the same extraction the reference uses) and uploaded, so
  /// only the AABB-plane test itself runs on the GPU.
  std::vector<VisibleCell> cull(const std::vector<CellId>& ids, const std::vector<glm::vec3>& mins,
                                const std::vector<glm::vec3>& maxs, const glm::mat4& viewProj,
                                const glm::vec3& observer, const CullConfig& cfg);

private:
  void ensureCapacity(uint32_t cellCount);

  rhi::Device& m_device;
  rhi::RootSignature& m_computeRoot;
  rhi::PipelineState& m_cullPso;

  uint32_t m_capacity = 0;
  std::shared_ptr<rhi::Buffer> m_cellBuffer;      ///< StructuredBuffer<CellGpu>, upload
  std::shared_ptr<rhi::Buffer> m_visibleBuffer;   ///< RWStructuredBuffer<VisibleCellGpu>, default
  std::shared_ptr<rhi::Buffer> m_counterBuffer;   ///< RWStructuredBuffer<uint>, default
  std::shared_ptr<rhi::Buffer> m_cb;              ///< CullCB, upload
  std::shared_ptr<rhi::Buffer> m_counterZero;     ///< four zero uints, to clear the counter
  std::shared_ptr<rhi::Buffer> m_visibleReadback; ///< readback heap
  std::shared_ptr<rhi::Buffer> m_counterReadback; ///< readback heap
};

} // namespace tucano::world
