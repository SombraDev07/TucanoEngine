#pragma once

#include "RHI/DX12/DX12Common.h"

#include <mutex>
#include <vector>

namespace tucano::rhi {

// Free-list bindless slot allocator (Dagor BindlessManager algorithm, reimplemented).
class BindlessManager {
public:
  void init(uint32_t capacity);

  // Allocate contiguous slots; returns base index or UINT32_MAX on failure.
  uint32_t allocate(uint32_t count = 1);
  void free(uint32_t index, uint32_t count = 1);

  // Deferred free — applied when GPU fence has completed (safe while GPU still uses slots).
  void deferFree(uint32_t index, uint32_t count = 1);
  void deferFree(uint32_t index, uint32_t count, uint64_t fenceValue);
  void flushDeferredFrees();
  void flushDeferredFrees(uint64_t completedFence);

  uint32_t capacity() const { return m_capacity; }
  uint32_t allocated() const { return m_allocated; }

private:
  struct Range {
    uint32_t begin = 0;
    uint32_t count = 0;
  };
  struct AgedRange {
    uint32_t begin = 0;
    uint32_t count = 0;
    uint64_t fenceValue = 0;
  };

  void coalesce();

  uint32_t m_capacity = 0;
  uint32_t m_allocated = 0;
  std::vector<Range> m_free;
  std::vector<AgedRange> m_deferred;
  std::mutex m_mutex;
};

} // namespace tucano::rhi
