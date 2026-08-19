#pragma once

#include <memory>

// -----------------------------------------------------------------------------
// Compile-time RHI backend selection.
//
// Exactly one backend per binary. Two RHIs in the same executable would need two
// definitions of Device::create() and would mix descriptor heaps, so the choice
// is made by the build system (cache variable TUCANO_RHI) and baked in here.
//
//   TUCANO_RHI_DX12    Direct3D 12   — production backend on Windows
//   TUCANO_RHI_VULKAN  Vulkan 1.3    — production backend on Linux
//   TUCANO_RHI_NULL    headless no-op — CI, asset cook, host-only tests
//
// The defaults below exist so a translation unit compiled outside our CMake
// still picks the platform's production backend rather than failing obscurely.
// -----------------------------------------------------------------------------

#if !defined(TUCANO_RHI_DX12) && !defined(TUCANO_RHI_VULKAN) && !defined(TUCANO_RHI_NULL)
#  if defined(_WIN32)
#    define TUCANO_RHI_DX12 1
#  else
#    define TUCANO_RHI_VULKAN 1
#  endif
#endif

#if !defined(TUCANO_RHI_DX12)
#  define TUCANO_RHI_DX12 0
#endif
#if !defined(TUCANO_RHI_VULKAN)
#  define TUCANO_RHI_VULKAN 0
#endif
#if !defined(TUCANO_RHI_NULL)
#  define TUCANO_RHI_NULL 0
#endif

#if (TUCANO_RHI_DX12 + TUCANO_RHI_VULKAN + TUCANO_RHI_NULL) != 1
#  error "Exactly one of TUCANO_RHI_DX12 / TUCANO_RHI_VULKAN / TUCANO_RHI_NULL must be 1"
#endif

#if TUCANO_RHI_DX12 && !defined(_WIN32)
#  error "TUCANO_RHI_DX12 requires Windows"
#endif

namespace tucano::rhi {

class Device;

// Per-backend factories. Device::create() (RHI.cpp) forwards to whichever one the
// macros above selected; only that backend's .cpp files are in the build, so the
// other declarations never resolve to a symbol and never need to.
std::unique_ptr<Device> createDX12Device(bool enableDebugLayer);
std::unique_ptr<Device> createVulkanDevice(bool enableDebugLayer);

// Also usable directly, on any platform: Null is a test double rather than a
// competing backend, so its .cpp is always compiled and Tools/RHITest calls this
// even in a DX12 or Vulkan build.
std::unique_ptr<Device> createNullDevice();

} // namespace tucano::rhi
