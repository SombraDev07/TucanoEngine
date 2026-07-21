#include "RHI/DX12/DX12Upload.h"

namespace tucano::rhi {

void DX12UploadAllocator::init(ID3D12Device* device, uint64_t size) {
  m_size = size;
  m_offset = 0;
  D3D12_HEAP_PROPERTIES heapProps{};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  throwIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_buffer)),
                "Create upload buffer");
  throwIfFailed(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped)), "Map upload buffer");
}

uint8_t* DX12UploadAllocator::allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS& outGpu,
                                       ID3D12Resource** outResource, uint64_t& outOffset) {
  const uint64_t aligned = (m_offset + alignment - 1) & ~(alignment - 1);
  if (aligned + size > m_size) {
    throw std::runtime_error("Upload allocator out of memory");
  }
  m_offset = aligned + size;
  outOffset = aligned;
  outGpu = m_buffer->GetGPUVirtualAddress() + aligned;
  *outResource = m_buffer.Get();
  return m_mapped + aligned;
}

void DX12UploadAllocator::reset() { m_offset = 0; }

} // namespace tucano::rhi
