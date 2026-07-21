#include "RHI/DX12/BarrierBatcher.h"

namespace tucano::rhi {

void BarrierBatcher::reset() {
  m_barriers.clear();
  m_coalesced = 0;
  m_erased = 0;
}

bool BarrierBatcher::tryEraseTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                        D3D12_RESOURCE_STATES after, UINT subresource) {
  for (size_t i = 0; i < m_barriers.size(); ++i) {
    auto& b = m_barriers[i];
    if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
      continue;
    }
    if (b.Transition.pResource == resource && b.Transition.Subresource == subresource &&
        b.Transition.StateBefore == before && b.Transition.StateAfter == after) {
      m_barriers.erase(m_barriers.begin() + static_cast<std::ptrdiff_t>(i));
      ++m_erased;
      return true;
    }
  }
  return false;
}

bool BarrierBatcher::tryUpdateTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after, UINT subresource) {
  for (int i = static_cast<int>(m_barriers.size()) - 1; i >= 0; --i) {
    auto& b = m_barriers[static_cast<size_t>(i)];
    if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
      continue;
    }
    if (b.Transition.pResource != resource || b.Transition.Subresource != subresource) {
      continue;
    }
    // Pending A→B, request B→C ⇒ patch to A→C
    if (b.Transition.StateAfter == before) {
      if (b.Transition.StateBefore == after) {
        m_barriers.erase(m_barriers.begin() + i);
        ++m_erased;
        return true;
      }
      b.Transition.StateAfter = after;
      ++m_coalesced;
      return true;
    }
  }
  return false;
}

void BarrierBatcher::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                                UINT subresource) {
  if (!resource || before == after) {
    return;
  }
  // Cancel exact reverse if still pending
  if (tryEraseTransition(resource, after, before, subresource)) {
    return;
  }
  if (tryUpdateTransition(resource, before, after, subresource)) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = subresource;
  m_barriers.push_back(barrier);
}

void BarrierBatcher::uav(ID3D12Resource* resource) {
  if (!resource) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  m_barriers.push_back(barrier);
}

void BarrierBatcher::flush(ID3D12GraphicsCommandList* cmd) {
  if (m_barriers.empty() || !cmd) {
    return;
  }
  cmd->ResourceBarrier(static_cast<UINT>(m_barriers.size()), m_barriers.data());
  m_barriers.clear();
}

} // namespace tucano::rhi
