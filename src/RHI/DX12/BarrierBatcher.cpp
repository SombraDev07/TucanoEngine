#include "RHI/DX12/BarrierBatcher.h"

namespace tucano::rhi {
namespace {

constexpr D3D12_RESOURCE_STATES kWriteBits =
    D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
    D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_COPY_DEST |
    D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_STREAM_OUT;

constexpr D3D12_RESOURCE_STATES kReadBits =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_COPY_SOURCE |
    D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
    D3D12_RESOURCE_STATE_PRESENT | D3D12_RESOURCE_STATE_PREDICATION;

} // namespace

bool BarrierBatcher::isWriteState(D3D12_RESOURCE_STATES s) {
  return (s & kWriteBits) != 0;
}

bool BarrierBatcher::isReadOnlyState(D3D12_RESOURCE_STATES s) {
  if (s == D3D12_RESOURCE_STATE_COMMON) {
    return true; // COMMON participates in promote
  }
  return !isWriteState(s) && (s & kReadBits) == s;
}

bool BarrierBatcher::canMerge(D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  return isReadOnlyState(a) && isReadOnlyState(b);
}

D3D12_RESOURCE_STATES BarrierBatcher::autoPromote(D3D12_RESOURCE_STATES current,
                                                  D3D12_RESOURCE_STATES requested) {
  if (canMerge(current, requested)) {
    // COMMON | read = read; readA | readB = union
    if (current == D3D12_RESOURCE_STATE_COMMON) {
      return requested;
    }
    if (requested == D3D12_RESOURCE_STATE_COMMON) {
      return current;
    }
    return current | requested;
  }
  return requested; // write replaces
}

D3D12_RESOURCE_STATES BarrierBatcher::decay(D3D12_RESOURCE_STATES s) {
  if (isWriteState(s)) {
    return D3D12_RESOURCE_STATE_COMMON;
  }
  return s;
}

void BarrierBatcher::reset() {
  m_barriers.clear();
  m_trackedDx.clear();
  m_cpuStates.clear();
  m_coalesced = 0;
  m_erased = 0;
  m_promoted = 0;
}

void BarrierBatcher::trackCpuState(ID3D12Resource* resource, ResourceState* cpuState) {
  if (resource && cpuState) {
    m_cpuStates[resource] = cpuState;
  }
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

void BarrierBatcher::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                D3D12_RESOURCE_STATES after, UINT subresource) {
  if (!resource) {
    return;
  }

  const D3D12_RESOURCE_STATES promoted = autoPromote(before, after);
  if (promoted != after) {
    ++m_promoted;
  }
  if (before == promoted) {
    m_trackedDx[resource] = promoted;
    return;
  }

  if (tryEraseTransition(resource, promoted, before, subresource)) {
    m_trackedDx[resource] = before;
    return;
  }
  if (tryUpdateTransition(resource, before, promoted, subresource)) {
    m_trackedDx[resource] = promoted;
    return;
  }

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = promoted;
  barrier.Transition.Subresource = subresource;
  m_barriers.push_back(barrier);
  m_trackedDx[resource] = promoted;
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

void BarrierBatcher::aliasing(ID3D12Resource* resourceBefore, ID3D12Resource* resourceAfter) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  barrier.Aliasing.pResourceBefore = resourceBefore;
  barrier.Aliasing.pResourceAfter = resourceAfter;
  m_barriers.push_back(barrier);
}

void BarrierBatcher::flush(ID3D12GraphicsCommandList* cmd) {
  if (m_barriers.empty() || !cmd) {
    return;
  }
  // GAP-11: Enhanced barriers path when Agility SDK exposes list->Barrier().
  // Legacy ResourceBarrier remains the default (compatible with all D3D12 runtimes).
#if defined(TUCANO_ENHANCED_BARRIERS) && defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
  ComPtr<ID3D12GraphicsCommandList7> cmd7;
  if (SUCCEEDED(cmd->QueryInterface(IID_PPV_ARGS(&cmd7))) && cmd7) {
    // Future: translate m_barriers → D3D12_BARRIER_GROUP. Keep legacy until fully validated.
  }
#endif
  cmd->ResourceBarrier(static_cast<UINT>(m_barriers.size()), m_barriers.data());
  m_barriers.clear();
}

void BarrierBatcher::decayTracked(ID3D12GraphicsCommandList* cmd) {
  for (auto& [res, state] : m_trackedDx) {
    const D3D12_RESOURCE_STATES next = decay(state);
    if (next == state) {
      continue;
    }
    // Explicit decay transition for write → COMMON at CL boundary (AMD-safe).
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = next;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_barriers.push_back(barrier);
    state = next;

    auto it = m_cpuStates.find(res);
    if (it != m_cpuStates.end() && it->second) {
      *it->second = ResourceState::Common;
    }
  }
  flush(cmd);
}

} // namespace tucano::rhi
