#include "RHI/DX12/DX12Common.h"

namespace tucano::rhi {

DXGI_FORMAT toDxgi(Format format) {
  switch (format) {
  case Format::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
  case Format::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  case Format::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
  case Format::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case Format::R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
  case Format::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
  case Format::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
  case Format::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case Format::R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
  case Format::R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
  case Format::R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
  case Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
  case Format::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
  case Format::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
  default: return DXGI_FORMAT_UNKNOWN;
  }
}

D3D12_RESOURCE_STATES toD3D(ResourceState state) {
  switch (state) {
  case ResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
  case ResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
  case ResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
  case ResourceState::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_READ;
  case ResourceState::ShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  case ResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  case ResourceState::CopySrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
  case ResourceState::CopyDst: return D3D12_RESOURCE_STATE_COPY_DEST;
  case ResourceState::VertexBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  case ResourceState::IndexBuffer: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
  case ResourceState::ConstantBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
  case ResourceState::IndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  case ResourceState::AccelerationStructure: return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
  default: return D3D12_RESOURCE_STATE_COMMON;
  }
}

D3D12_FILTER toD3D(Filter filter, bool comparison) {
  if (comparison) {
    return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
  }
  switch (filter) {
  case Filter::Point: return D3D12_FILTER_MIN_MAG_MIP_POINT;
  case Filter::Linear: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  case Filter::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
  }
  return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
}

D3D12_TEXTURE_ADDRESS_MODE toD3D(AddressMode mode) {
  switch (mode) {
  case AddressMode::Wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  case AddressMode::Clamp: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  case AddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
  case AddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  }
  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

D3D12_CULL_MODE toD3D(CullMode mode) {
  switch (mode) {
  case CullMode::None: return D3D12_CULL_MODE_NONE;
  case CullMode::Back: return D3D12_CULL_MODE_BACK;
  case CullMode::Front: return D3D12_CULL_MODE_FRONT;
  }
  return D3D12_CULL_MODE_BACK;
}

D3D12_COMPARISON_FUNC toD3D(CompareOp op) {
  switch (op) {
  case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
  case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
  case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
  case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
  case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
  case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
  case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
  }
  return D3D12_COMPARISON_FUNC_LESS;
}

D3D12_PRIMITIVE_TOPOLOGY toD3D(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
  case PrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
  }
  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3DType(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::LineList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

uint32_t formatStride(Format format) {
  switch (format) {
  case Format::R8G8B8A8_UNORM:
  case Format::R8G8B8A8_UNORM_SRGB:
  case Format::B8G8R8A8_UNORM:
  case Format::R11G11B10_FLOAT:
  case Format::R10G10B10A2_UNORM:
  case Format::D24_UNORM_S8_UINT:
  case Format::R32_FLOAT:
  case Format::R32_UINT:
    return 4;
  case Format::R16G16B16A16_FLOAT:
    return 8;
  case Format::R32G32_FLOAT:
    return 8;
  case Format::R32G32B32_FLOAT:
    return 12;
  case Format::R32G32B32A32_FLOAT:
    return 16;
  case Format::R16_FLOAT:
    return 2;
  case Format::D32_FLOAT:
    return 4;
  default:
    return 4;
  }
}

uint32_t formatRowPitch(Format format, uint32_t width) {
  return width * formatStride(format);
}

} // namespace tucano::rhi
