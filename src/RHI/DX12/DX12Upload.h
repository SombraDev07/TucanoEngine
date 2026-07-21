#pragma once

#include "RHI/DX12/DX12Common.h"

namespace tucano::rhi {

class DX12UploadAllocator {
public:
  void init(ID3D12Device* device, uint64_t size);
  uint8_t* allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS& outGpu, ID3D12Resource** outResource,
                    uint64_t& outOffset);
  void reset();

private:
  ComPtr<ID3D12Resource> m_buffer;
  uint8_t* m_mapped = nullptr;
  uint64_t m_size = 0;
  uint64_t m_offset = 0;
};

} // namespace tucano::rhi
