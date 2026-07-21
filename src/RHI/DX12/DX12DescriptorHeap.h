#pragma once

#include "RHI/DX12/DX12Common.h"

#include <mutex>
#include <vector>

namespace tucano::rhi {

class DX12DescriptorHeap {
public:
  void init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);

  D3D12_CPU_DESCRIPTOR_HANDLE allocateCpu();
  uint32_t allocateIndex();
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(uint32_t index) const;
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(uint32_t index) const;
  ID3D12DescriptorHeap* get() const { return m_heap.Get(); }
  uint32_t descriptorSize() const { return m_descriptorSize; }
  uint32_t capacity() const { return m_capacity; }
  uint32_t allocated() const { return m_allocated; }

private:
  ComPtr<ID3D12DescriptorHeap> m_heap;
  D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};
  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};
  uint32_t m_descriptorSize = 0;
  uint32_t m_capacity = 0;
  uint32_t m_allocated = 0;
  bool m_shaderVisible = false;
  std::mutex m_mutex;
};

} // namespace tucano::rhi
