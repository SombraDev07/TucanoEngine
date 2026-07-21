#include "RHI/DX12/SplitTransitionTracker.h"

namespace tucano::rhi {

void SplitTransitionTracker::reset() { m_pending.clear(); }

void SplitTransitionTracker::begin(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                                   UINT subresource) {
  if (!resource || before == after) {
    return;
  }
  m_pending.push_back({resource, before, after, subresource});
}

void SplitTransitionTracker::emitBegin(ID3D12GraphicsCommandList* cmd) {
  if (!cmd || m_pending.empty()) {
    return;
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(m_pending.size());
  for (const auto& s : m_pending) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    b.Transition.pResource = s.resource;
    b.Transition.StateBefore = s.before;
    b.Transition.StateAfter = s.after;
    b.Transition.Subresource = s.subresource;
    barriers.push_back(b);
  }
  cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void SplitTransitionTracker::end(ID3D12GraphicsCommandList* cmd) {
  if (!cmd || m_pending.empty()) {
    return;
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(m_pending.size());
  for (const auto& s : m_pending) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    b.Transition.pResource = s.resource;
    b.Transition.StateBefore = s.before;
    b.Transition.StateAfter = s.after;
    b.Transition.Subresource = s.subresource;
    barriers.push_back(b);
  }
  cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  m_pending.clear();
}

} // namespace tucano::rhi
