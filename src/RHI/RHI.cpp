#include "RHI/RHI.h"

#include "Platform/FileSystem.h"

#ifndef TUCANO_SHADER_EXT
#define TUCANO_SHADER_EXT ".cso"
#endif

#include <stdexcept>

// Backend-neutral part of the RHI: the pieces that must exist in every build regardless of which
// backend CMake selected, and that therefore cannot live inside RHI/DX12/ or RHI/Vulkan/.

namespace tucano::rhi {

ShaderBytecode ShaderBytecode::loadFromFile(const std::string& path) {
  std::string resolved = path;
#if TUCANO_RHI_VULKAN
  if (resolved.size() >= 4 && resolved.compare(resolved.size() - 4, 4, ".cso") == 0) {
    resolved.replace(resolved.size() - 4, 4, TUCANO_SHADER_EXT);
  }
#endif
  ShaderBytecode bc;
  bc.data = tucano::readFileBytes(resolved);
  return bc;
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

std::unique_ptr<Device> Device::create(bool enableDebugLayer) {
#if TUCANO_RHI_DX12
  return createDX12Device(enableDebugLayer);
#elif TUCANO_RHI_VULKAN
  return createVulkanDevice(enableDebugLayer);
#else
  (void)enableDebugLayer;
  return createNullDevice();
#endif
}

} // namespace tucano::rhi
