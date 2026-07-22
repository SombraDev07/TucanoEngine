#include "RHI/DX12/DX12DescriptorHeap.h"

#include <algorithm>

namespace tucano::rhi {

void DX12DescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                              bool shaderVisible) {
  std::lock_guard lock(m_mutex);
  m_heap.Reset();
  m_capacity = capacity;
  m_shaderVisible = shaderVisible;
  m_allocated = 0;
  m_type = type;
  m_free.clear();
  m_deferred.clear();
  if (capacity > 0) {
    m_free.push_back({0, capacity});
  }

  D3D12_DESCRIPTOR_HEAP_DESC desc{};
  desc.Type = type;
  desc.NumDescriptors = capacity;
  desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  throwIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap)), "CreateDescriptorHeap");

  m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
  m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
  if (shaderVisible) {
    m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::allocateCpu() {
  return cpuHandle(allocateIndex());
}

uint32_t DX12DescriptorHeap::allocateIndex() { return allocate(1); }

uint32_t DX12DescriptorHeap::allocate(uint32_t count) {
  std::lock_guard lock(m_mutex);
  if (count == 0) {
    return UINT32_MAX;
  }
  int best = -1;
  for (size_t i = 0; i < m_free.size(); ++i) {
    if (m_free[i].count >= count) {
      if (best < 0 || m_free[i].count < m_free[static_cast<size_t>(best)].count) {
        best = static_cast<int>(i);
      }
    }
  }
  if (best < 0) {
    throw std::runtime_error("Descriptor heap exhausted (need=" + std::to_string(count) +
                             " capacity=" + std::to_string(m_capacity) + " allocated=" +
                             std::to_string(m_allocated) + " type=" + std::to_string(int(m_type)) + ")");
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

void DX12DescriptorHeap::free(uint32_t index, uint32_t count) {
  std::lock_guard lock(m_mutex);
  if (count == 0 || index == UINT32_MAX) {
    return;
  }
  m_free.push_back({index, count});
  m_allocated = (m_allocated >= count) ? (m_allocated - count) : 0;
  coalesceUnlocked();
}

void DX12DescriptorHeap::deferFree(uint32_t index, uint32_t count, uint64_t fenceValue) {
  std::lock_guard lock(m_mutex);
  m_deferred.push_back({index, count, fenceValue});
}

void DX12DescriptorHeap::flushDeferredFrees(uint64_t completedFence) {
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

void DX12DescriptorHeap::coalesceUnlocked() {
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

D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::cpuHandle(uint32_t index) const {
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuStart;
  h.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
  return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::gpuHandle(uint32_t index) const {
  D3D12_GPU_DESCRIPTOR_HANDLE h = m_gpuStart;
  h.ptr += static_cast<UINT64>(index) * m_descriptorSize;
  return h;
}

} // namespace tucano::rhi
