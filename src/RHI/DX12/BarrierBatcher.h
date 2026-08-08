#pragma once

#include "RHI/DX12/DX12Common.h"

#include <unordered_map>
#include <vector>

namespace tucano::rhi {

// Dagor-inspired barrier coalescing + D3D12 auto-promote/decay emulation.
class BarrierBatcher {
public:
  void reset();
  void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                  UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  void uav(ID3D12Resource* resource);
  // Aliasing barrier when two resources share physical heap memory (RenderGraph transients).
  void aliasing(ID3D12Resource* resourceBefore, ID3D12Resource* resourceAfter);
  void flush(ID3D12GraphicsCommandList* cmd);
  // Command-list boundary: write states decay to COMMON (CPU tracking + optional barrier).
  void decayTracked(ID3D12GraphicsCommandList* cmd);

  // Register CPU-side ResourceState for decay updates on close().
  //
  // `subresourceStates` is the per-subresource array a texture actually consults when deciding
  // whether a barrier is needed. It MUST be passed for textures: updating only the summary `state`
  // on decay leaves that array claiming the old state, so the next frame skips the barrier and the
  // resource is used while the GPU has already decayed it to COMMON.
  void trackCpuState(ID3D12Resource* resource, ResourceState* cpuState,
                     std::vector<ResourceState>* subresourceStates = nullptr);

  uint32_t pendingCount() const { return static_cast<uint32_t>(m_barriers.size()); }
  uint32_t coalescedCount() const { return m_coalesced; }
  uint32_t erasedCount() const { return m_erased; }
  uint32_t promotedCount() const { return m_promoted; }

  static bool isWriteState(D3D12_RESOURCE_STATES s);
  static bool isReadOnlyState(D3D12_RESOURCE_STATES s);
  static bool canMerge(D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b);
  static D3D12_RESOURCE_STATES autoPromote(D3D12_RESOURCE_STATES current, D3D12_RESOURCE_STATES requested);
  static D3D12_RESOURCE_STATES decay(D3D12_RESOURCE_STATES s);

private:
  bool tryUpdateTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                           UINT subresource);
  bool tryEraseTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                          UINT subresource);

  std::vector<D3D12_RESOURCE_BARRIER> m_barriers;
  std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> m_trackedDx;
  struct CpuStateRef {
    ResourceState* summary = nullptr;
    std::vector<ResourceState>* subresources = nullptr; // null for buffers
  };
  std::unordered_map<ID3D12Resource*, CpuStateRef> m_cpuStates;
  uint32_t m_coalesced = 0;
  uint32_t m_erased = 0;
  uint32_t m_promoted = 0;
};

} // namespace tucano::rhi
