#pragma once

#include "RHI/DX12/DX12Common.h"

#include <unordered_map>
#include <vector>

namespace tucano::rhi {

// Tracks begin/end split transitions across graphics↔compute queues (Dagor SplitTransitionTracker).
class SplitTransitionTracker {
public:
  void begin(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
             UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  void emitBegin(ID3D12GraphicsCommandList* cmd);
  // Emits END_ONLY barriers for matching begins onto `cmd`.
  void end(ID3D12GraphicsCommandList* cmd);
  void reset();
  uint32_t pending() const { return static_cast<uint32_t>(m_pending.size()); }

private:
  struct Split {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  };
  std::vector<Split> m_pending;
};

} // namespace tucano::rhi
