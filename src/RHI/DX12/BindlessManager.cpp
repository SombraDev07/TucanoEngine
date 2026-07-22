#include "RHI/DX12/BindlessManager.h"

#include <algorithm>

namespace tucano::rhi {

void BindlessManager::init(uint32_t capacity) {
  std::lock_guard lock(m_mutex);
  m_capacity = capacity;
  m_allocated = 0;
  m_free.clear();
  m_deferred.clear();
  // Slot 0 is permanently reserved as a null SRV (safe bindless fallback).
  if (capacity > 1) {
    m_free.push_back({1, capacity - 1});
  }
}

uint32_t BindlessManager::allocate(uint32_t count) {
  std::lock_guard lock(m_mutex);
  if (count == 0) {
    return UINT32_MAX;
  }
  // Smallest-fit
  int best = -1;
  for (size_t i = 0; i < m_free.size(); ++i) {
    if (m_free[i].count >= count) {
      if (best < 0 || m_free[i].count < m_free[static_cast<size_t>(best)].count) {
        best = static_cast<int>(i);
      }
    }
  }
  if (best < 0) {
    return UINT32_MAX;
  }
  const uint32_t base = m_free[static_cast<size_t>(best)].begin;
  m_free[static_cast<size_t>(best)].begin += count;
  m_free[static_cast<size_t>(best)].count -= count;
  if (m_free[static_cast<size_t>(best)].count == 0) {
    m_free.erase(m_free.begin() + best);
  }
  m_allocated += count;
  return base;
}

void BindlessManager::free(uint32_t index, uint32_t count) {
  std::lock_guard lock(m_mutex);
  if (count == 0 || index == UINT32_MAX) {
    return;
  }
  // Never reclaim the permanent null slot.
  if (index == 0) {
    if (count <= 1) {
      return;
    }
    ++index;
    --count;
  }
  m_free.push_back({index, count});
  m_allocated = (m_allocated >= count) ? (m_allocated - count) : 0;
  coalesce();
}

void BindlessManager::deferFree(uint32_t index, uint32_t count) {
  deferFree(index, count, 0);
}

void BindlessManager::deferFree(uint32_t index, uint32_t count, uint64_t fenceValue) {
  if (count == 0 || index == UINT32_MAX || (index == 0 && count == 1)) {
    return;
  }
  if (index == 0) {
    ++index;
    --count;
  }
  std::lock_guard lock(m_mutex);
  m_deferred.push_back({index, count, fenceValue});
}

void BindlessManager::flushDeferredFrees() {
  flushDeferredFrees(UINT64_MAX);
}

void BindlessManager::flushDeferredFrees(uint64_t completedFence) {
  std::vector<AgedRange> ready;
  {
    std::lock_guard lock(m_mutex);
    std::vector<AgedRange> keep;
    for (const auto& r : m_deferred) {
      if (r.fenceValue <= completedFence) {
        ready.push_back(r);
      } else {
        keep.push_back(r);
      }
    }
    m_deferred.swap(keep);
  }
  for (const auto& r : ready) {
    free(r.begin, r.count);
  }
}

void BindlessManager::coalesce() {
  if (m_free.size() < 2) {
    return;
  }
  std::sort(m_free.begin(), m_free.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
  std::vector<Range> merged;
  merged.push_back(m_free[0]);
  for (size_t i = 1; i < m_free.size(); ++i) {
    auto& last = merged.back();
    if (last.begin + last.count == m_free[i].begin) {
      last.count += m_free[i].count;
    } else {
      merged.push_back(m_free[i]);
    }
  }
  m_free = std::move(merged);
}

} // namespace tucano::rhi
