#pragma once

#include "RHI/DX12/DX12Common.h"

#include <vector>

namespace tucano::rhi {

// Dagor-inspired barrier coalescing: merge A→B then B→C into A→C,
// erase A→B then B→A, skip no-ops. Flush once per draw/dispatch/close.
class BarrierBatcher {
public:
  void reset();
  void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                  UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  void uav(ID3D12Resource* resource);
  void flush(ID3D12GraphicsCommandList* cmd);
  uint32_t pendingCount() const { return static_cast<uint32_t>(m_barriers.size()); }
  uint32_t coalescedCount() const { return m_coalesced; }
  uint32_t erasedCount() const { return m_erased; }

private:
  bool tryUpdateTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                           UINT subresource);
  bool tryEraseTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                          UINT subresource);

  std::vector<D3D12_RESOURCE_BARRIER> m_barriers;
  uint32_t m_coalesced = 0;
  uint32_t m_erased = 0;
};

} // namespace tucano::rhi
