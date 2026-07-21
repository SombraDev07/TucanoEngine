#pragma once

#include "RHI/RHI.h"
#include "RHI/RHITypes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <stdexcept>
#include <string>

namespace tucano::rhi {

using Microsoft::WRL::ComPtr;

inline void throwIfFailed(HRESULT hr, const char* what) {
  if (FAILED(hr)) {
    throw std::runtime_error(std::string(what) + " hr=0x" + std::to_string(static_cast<unsigned long>(hr)));
  }
}

DXGI_FORMAT toDxgi(Format format);
D3D12_RESOURCE_STATES toD3D(ResourceState state);
D3D12_FILTER toD3D(Filter filter, bool comparison);
D3D12_TEXTURE_ADDRESS_MODE toD3D(AddressMode mode);
D3D12_CULL_MODE toD3D(CullMode mode);
D3D12_COMPARISON_FUNC toD3D(CompareOp op);
D3D12_PRIMITIVE_TOPOLOGY toD3D(PrimitiveTopology topology);
D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3DType(PrimitiveTopology topology);
uint32_t formatStride(Format format);
uint32_t formatRowPitch(Format format, uint32_t width);

} // namespace tucano::rhi
