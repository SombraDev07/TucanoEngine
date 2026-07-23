#include "World/GpuCellCuller.h"

#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <cstring>

namespace tucano::world {
namespace {

// Mirror of the shader structs. Layout must match CellCull.hlsl byte for byte.
struct CellGpu {
  glm::vec3 boundsMin;
  uint32_t idLo;
  glm::vec3 boundsMax;
  uint32_t idHi;
};

struct VisibleCellGpu {
  uint32_t idLo;
  uint32_t idHi;
  uint32_t lod;
  float distance;
};

struct CullCB {
  glm::vec4 frustumPlanes[6];
  glm::vec4 observer;
  uint32_t cellCount;
  float lodStep;
  uint32_t maxLod;
  float maxDistance;
};

rhi::DX12Buffer& dxBuf(rhi::Buffer& b) { return static_cast<rhi::DX12Buffer&>(b); }

} // namespace

GpuCellCuller::GpuCellCuller(rhi::Device& device, rhi::RootSignature& computeRoot,
                             rhi::PipelineState& cullPso)
    : m_device(device), m_computeRoot(computeRoot), m_cullPso(cullPso) {
  rhi::BufferDesc cbDesc{};
  cbDesc.size = sizeof(CullCB);
  cbDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
  cbDesc.debugName = "CellCullCB";
  m_cb = m_device.createBuffer(cbDesc, nullptr);

  // A persistent little source of zeros used to clear the counter each run. Making it a member
  // rather than a per-call temporary avoids allocating and freeing a GPU resource inside every
  // cull — a freed buffer the GPU still references is a page-fault waiting to happen.
  const uint32_t zero[4] = {0, 0, 0, 0};
  rhi::BufferDesc z{};
  z.size = sizeof(zero);
  z.usage = rhi::BufferUsage::Upload;
  z.debugName = "CellCullCounterZero";
  m_counterZero = m_device.createBuffer(z, zero);
}

void GpuCellCuller::ensureCapacity(uint32_t cellCount) {
  if (cellCount <= m_capacity && m_cellBuffer) return;
  // Grow generously so a slowly-growing world does not reallocate every frame.
  m_capacity = std::max(cellCount, m_capacity == 0 ? 256u : m_capacity * 2u);

  rhi::BufferDesc cells{};
  cells.size = uint64_t(m_capacity) * sizeof(CellGpu);
  cells.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::Upload;
  cells.stride = sizeof(CellGpu);
  cells.debugName = "CellCullInput";
  m_cellBuffer = m_device.createBuffer(cells, nullptr);

  rhi::BufferDesc vis{};
  vis.size = uint64_t(m_capacity) * sizeof(VisibleCellGpu);
  vis.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
  vis.stride = sizeof(VisibleCellGpu);
  vis.debugName = "CellCullVisible";
  m_visibleBuffer = m_device.createBuffer(vis, nullptr);

  rhi::BufferDesc counter{};
  counter.size = sizeof(uint32_t) * 4; // padded
  counter.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
  counter.stride = sizeof(uint32_t);
  counter.debugName = "CellCullCounter";
  m_counterBuffer = m_device.createBuffer(counter, nullptr);

  rhi::BufferDesc visRb{};
  visRb.size = vis.size;
  visRb.usage = rhi::BufferUsage::Readback;
  visRb.debugName = "CellCullVisibleRB";
  m_visibleReadback = m_device.createBuffer(visRb, nullptr);

  rhi::BufferDesc cntRb{};
  cntRb.size = counter.size;
  cntRb.usage = rhi::BufferUsage::Readback;
  cntRb.debugName = "CellCullCounterRB";
  m_counterReadback = m_device.createBuffer(cntRb, nullptr);
}

std::vector<VisibleCell> GpuCellCuller::cull(const std::vector<CellId>& ids,
                                             const std::vector<glm::vec3>& mins,
                                             const std::vector<glm::vec3>& maxs,
                                             const glm::mat4& viewProj, const glm::vec3& observer,
                                             const CullConfig& cfg) {
  const uint32_t cellCount = uint32_t(std::min({ids.size(), mins.size(), maxs.size()}));
  std::vector<VisibleCell> result;
  if (cellCount == 0) return result;

  ensureCapacity(cellCount);

  auto& dev = static_cast<rhi::DX12Device&>(m_device);
  // beginFrame rotates dynamic upload buffers (the constant buffer is one). The CPU-visible pointer
  // it returns only points at the current backing AFTER the rotation, so every mapped() write must
  // come after this call — writing before it lands in a backing the GPU will not read.
  rhi::CommandList* cmd = m_device.beginFrame();

  // Pack the cell boxes into the upload buffer.
  {
    std::vector<CellGpu> packed(cellCount);
    for (uint32_t i = 0; i < cellCount; ++i) {
      packed[i].boundsMin = mins[i];
      packed[i].boundsMax = maxs[i];
      const uint64_t key = ids[i].key();
      packed[i].idLo = uint32_t(key & 0xFFFFFFFFu);
      packed[i].idHi = uint32_t(key >> 32);
    }
    std::memcpy(m_cellBuffer->mapped(), packed.data(), packed.size() * sizeof(CellGpu));
  }

  // Fill the constant buffer with the SAME frustum extraction the reference uses.
  {
    const Frustum f = extractFrustum(viewProj);
    CullCB cb{};
    for (int i = 0; i < Frustum::Count; ++i) cb.frustumPlanes[i] = f.planes[i];
    cb.observer = glm::vec4(observer, 0.0f);
    cb.cellCount = cellCount;
    cb.lodStep = cfg.lodStep;
    cb.maxLod = cfg.maxLod;
    cb.maxDistance = cfg.maxDistance;
    std::memcpy(m_cb->mapped(), &cb, sizeof(cb));
  }

  // Zero the visible counter each run from the persistent zero buffer.
  {
    cmd->copyBuffer(*m_counterBuffer, 0, *m_counterZero, 0, sizeof(uint32_t));

    cmd->transition(*m_visibleBuffer, rhi::ResourceState::UnorderedAccess);
    cmd->transition(*m_counterBuffer, rhi::ResourceState::UnorderedAccess);

    cmd->setRootSignature(m_computeRoot);
    // Bind the SRV/sampler heaps on this command list. Without it the descriptor tables resolve to
    // nothing and the dispatch silently writes zero cells — the render path calls this before every
    // pass, and a standalone dispatch must too.
    cmd->setDescriptorHeap();
    cmd->setPipeline(m_cullPso);
    cmd->setComputeRootCBV(1, *m_cb);

    D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {dxBuf(*m_cellBuffer).srvCpu};
    D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {dxBuf(*m_visibleBuffer).uavCpu,
                                          dxBuf(*m_counterBuffer).uavCpu};
    cmd->setComputeRootSrvTable(2, dev.writeSrvTable(srvs, 1));
    cmd->setComputeRootUavTable(3, dev.writeUavTable(uavs, 2));
    cmd->dispatch((cellCount + 63u) / 64u, 1, 1);

    // Copy both UAVs down to readback heaps. copyBuffer transitions them to CopySrc for us.
    cmd->copyBuffer(*m_counterReadback, 0, *m_counterBuffer, 0, sizeof(uint32_t));
    cmd->copyBuffer(*m_visibleReadback, 0, *m_visibleBuffer, 0,
                    uint64_t(cellCount) * sizeof(VisibleCellGpu));
  }

  // Execute and wait. This is the blocking part that makes cull() a test/debug helper, not a
  // per-frame call. The headless submit leaves no frame state behind, so cull() is repeatable.
  m_device.submitAndWaitHeadless();

  uint32_t visibleCount = 0;
  std::memcpy(&visibleCount, m_counterReadback->mapped(), sizeof(uint32_t));
  visibleCount = std::min(visibleCount, cellCount);

  result.reserve(visibleCount);
  const auto* gpu = static_cast<const VisibleCellGpu*>(m_visibleReadback->mapped());
  for (uint32_t i = 0; i < visibleCount; ++i) {
    VisibleCell vc;
    vc.id = CellId::fromKey((uint64_t(gpu[i].idHi) << 32) | uint64_t(gpu[i].idLo));
    vc.lod = gpu[i].lod;
    vc.distance = gpu[i].distance;
    result.push_back(vc);
  }
  return result;
}

} // namespace tucano::world
