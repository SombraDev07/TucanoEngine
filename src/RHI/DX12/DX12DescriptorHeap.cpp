#include "RHI/DX12/DX12DescriptorHeap.h"

namespace tucano::rhi {

void DX12DescriptorHeap::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                              bool shaderVisible) {
  m_capacity = capacity;
  m_shaderVisible = shaderVisible;
  m_allocated = 0;
  m_type = type;

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

uint32_t DX12DescriptorHeap::allocateIndex() {
  std::lock_guard lock(m_mutex);
  if (m_allocated >= m_capacity) {
    throw std::runtime_error("Descriptor heap exhausted (allocated=" + std::to_string(m_allocated) +
                             " capacity=" + std::to_string(m_capacity) + " type=" + std::to_string(int(m_type)) + ")");
  }
  return m_allocated++;
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
