#include "World/InstanceCloudCuller.h"

#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <algorithm>
#include <cstring>

namespace tucano::world {
namespace {

// Mirror of InstanceCull.hlsl's InstanceCullCB. Byte layout must match.
struct InstanceCullCB {
  glm::vec4 frustumPlanes[6];
  glm::vec4 observer;
  uint32_t instanceCount;
  float lodStep;
  uint32_t maxLod;
  float maxDistance;
};

rhi::DX12Buffer& dxBuf(rhi::Buffer& b) { return static_cast<rhi::DX12Buffer&>(b); }

} // namespace

InstanceCloudCuller::InstanceCloudCuller(rhi::Device& device, rhi::RootSignature& computeRoot,
                                         rhi::PipelineState& cullPso)
    : m_device(device), m_computeRoot(computeRoot), m_cullPso(cullPso) {
  rhi::BufferDesc cbDesc{};
  cbDesc.size = sizeof(InstanceCullCB);
  cbDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
  cbDesc.debugName = "InstanceCullCB";
  m_cb = m_device.createBuffer(cbDesc, nullptr);

  // The args template: reset the DrawIndexedInstanced block to {indexCount, 0, 0, 0, 0} before each
  // dispatch. indexCountPerInstance is patched per setInstances(); the rest stay zero, and the cull
  // grows instanceCount from zero. A persistent upload buffer, not a per-call temporary — the same
  // reason WM-4's counter-zero buffer is a member.
  rhi::BufferDesc initDesc{};
  initDesc.size = sizeof(DrawIndexedArgs);
  initDesc.usage = rhi::BufferUsage::Upload;
  initDesc.debugName = "InstanceCullArgsInit";
  m_argsInit = m_device.createBuffer(initDesc, nullptr);

  rhi::BufferDesc argsDesc{};
  argsDesc.size = sizeof(DrawIndexedArgs);
  argsDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess |
                   rhi::BufferUsage::Indirect;
  argsDesc.stride = sizeof(DrawIndexedArgs);
  argsDesc.debugName = "InstanceCullArgs";
  m_argsBuffer = m_device.createBuffer(argsDesc, nullptr);

  rhi::BufferDesc argsRb{};
  argsRb.size = sizeof(DrawIndexedArgs);
  argsRb.usage = rhi::BufferUsage::Readback;
  argsRb.debugName = "InstanceCullArgsRB";
  m_argsReadback = m_device.createBuffer(argsRb, nullptr);
}

void InstanceCloudCuller::setInstances(const std::vector<InstanceGpu>& instances,
                                       uint32_t meshIndexCount) {
  m_instanceCount = uint32_t(instances.size());
  m_meshIndexCount = meshIndexCount;

  if (m_instanceCount > m_capacity || !m_instanceBuffer) {
    m_capacity = std::max(m_instanceCount, m_capacity == 0 ? 1024u : m_capacity * 2u);

    rhi::BufferDesc inst{};
    inst.size = uint64_t(m_capacity) * sizeof(InstanceGpu);
    inst.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::Upload;
    inst.stride = sizeof(InstanceGpu);
    inst.debugName = "InstanceCloudInstances";
    m_instanceBuffer = m_device.createBuffer(inst, nullptr);

    rhi::BufferDesc vis{};
    vis.size = uint64_t(m_capacity) * sizeof(uint32_t);
    vis.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
    vis.stride = sizeof(uint32_t);
    vis.debugName = "InstanceCloudVisible";
    m_visibleBuffer = m_device.createBuffer(vis, nullptr);

    rhi::BufferDesc visRb{};
    visRb.size = vis.size;
    visRb.usage = rhi::BufferUsage::Readback;
    visRb.debugName = "InstanceCloudVisibleRB";
    m_visibleReadback = m_device.createBuffer(visRb, nullptr);
  }

  if (m_instanceCount > 0) {
    // The instance buffer is an upload heap; write it directly. Unlike the constant buffer this is
    // not a rotating dynamic backing, so it is safe to fill outside a beginFrame() window.
    std::memcpy(m_instanceBuffer->mapped(), instances.data(),
                size_t(m_instanceCount) * sizeof(InstanceGpu));
  }
}

void InstanceCloudCuller::writeConstants(const glm::mat4& viewProj, const glm::vec3& observer,
                                         const CullConfig& cfg) {
  const Frustum f = extractFrustum(viewProj);
  InstanceCullCB cb{};
  for (int i = 0; i < Frustum::Count; ++i) cb.frustumPlanes[i] = f.planes[i];
  cb.observer = glm::vec4(observer, 0.0f);
  cb.instanceCount = m_instanceCount;
  cb.lodStep = cfg.lodStep;
  cb.maxLod = cfg.maxLod;
  cb.maxDistance = cfg.maxDistance;
  std::memcpy(m_cb->mapped(), &cb, sizeof(cb));

  // Reset the args template: indexCount for the draw, instanceCount starts at zero for the cull to
  // grow. Written here (after beginFrame, before the copy) so it lands in the live upload backing.
  DrawIndexedArgs init{};
  init.indexCountPerInstance = m_meshIndexCount;
  std::memcpy(m_argsInit->mapped(), &init, sizeof(init));
}

void InstanceCloudCuller::recordCull(rhi::CommandList& cmd) {
  auto& dev = static_cast<rhi::DX12Device&>(m_device);

  // Seed the args from the template, then let the dispatch grow instanceCount in place.
  cmd.copyBuffer(*m_argsBuffer, 0, *m_argsInit, 0, sizeof(DrawIndexedArgs));

  cmd.transition(*m_visibleBuffer, rhi::ResourceState::UnorderedAccess);
  cmd.transition(*m_argsBuffer, rhi::ResourceState::UnorderedAccess);

  cmd.setRootSignature(m_computeRoot);
  cmd.setDescriptorHeap();
  cmd.setPipeline(m_cullPso);
  cmd.setComputeRootCBV(1, *m_cb);

  D3D12_CPU_DESCRIPTOR_HANDLE srvs[] = {dxBuf(*m_instanceBuffer).srvCpu};
  D3D12_CPU_DESCRIPTOR_HANDLE uavs[] = {dxBuf(*m_visibleBuffer).uavCpu, dxBuf(*m_argsBuffer).uavCpu};
  cmd.setComputeRootSrvTable(2, dev.writeSrvTable(srvs, 1));
  cmd.setComputeRootUavTable(3, dev.writeUavTable(uavs, 2));
  cmd.dispatch((m_instanceCount + 63u) / 64u, 1, 1);
}

std::vector<uint32_t> InstanceCloudCuller::cull(const glm::mat4& viewProj, const glm::vec3& observer,
                                                const CullConfig& cfg, DrawIndexedArgs* outArgs) {
  std::vector<uint32_t> result;
  if (m_instanceCount == 0) {
    if (outArgs) *outArgs = DrawIndexedArgs{m_meshIndexCount, 0, 0, 0, 0};
    return result;
  }

  // beginFrame rotates the dynamic upload backings (the constant buffer and args template among
  // them), so all mapped() writes must come after it — same rule as GpuCellCuller.
  rhi::CommandList* cmd = m_device.beginFrame();
  writeConstants(viewProj, observer, cfg);
  recordCull(*cmd);

  cmd->copyBuffer(*m_argsReadback, 0, *m_argsBuffer, 0, sizeof(DrawIndexedArgs));
  cmd->copyBuffer(*m_visibleReadback, 0, *m_visibleBuffer, 0,
                  uint64_t(m_instanceCount) * sizeof(uint32_t));

  m_device.submitAndWaitHeadless();

  DrawIndexedArgs args{};
  std::memcpy(&args, m_argsReadback->mapped(), sizeof(DrawIndexedArgs));
  if (outArgs) *outArgs = args;

  const uint32_t visibleCount = std::min(args.instanceCount, m_instanceCount);
  result.resize(visibleCount);
  if (visibleCount > 0) {
    std::memcpy(result.data(), m_visibleReadback->mapped(), size_t(visibleCount) * sizeof(uint32_t));
  }
  return result;
}

void InstanceCloudCuller::cullForDraw(rhi::CommandList& cmd, const glm::mat4& viewProj,
                                      const glm::vec3& observer, const CullConfig& cfg) {
  if (m_instanceCount == 0) return;
  writeConstants(viewProj, observer, cfg);
  recordCull(cmd);
  // Leave the args ready to be consumed by drawIndexedIndirect. The caller transitions it to
  // IndirectArgument when it binds the draw.
  cmd.transition(*m_argsBuffer, rhi::ResourceState::IndirectArgument);
}

} // namespace tucano::world
