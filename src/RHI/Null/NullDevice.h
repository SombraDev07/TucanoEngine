#pragma once

#include "RHI/RHI.h"

// -----------------------------------------------------------------------------
// Null RHI backend (headless, no-op).
//
// Purpose (roadmap Track A0):
//   1. Proves the tucano::rhi interface is truly backend-agnostic — this file and
//      NullDevice.cpp compile against RHI.h ALONE, pulling in zero DX12/Windows
//      headers. If it builds, the abstraction has no backend leakage and the
//      Vulkan backend (Track A1+) can be added as a peer of the DX12 one.
//   2. Serves as the skeleton the Vulkan backend grows from: every override in
//      NullDevice.cpp is annotated with `// TODO(vulkan):` pointing at the real
//      call the Vulkan implementation will make in its place.
//   3. Gives headless tooling (asset cook, CI shader validation, unit tests) a
//      Device that constructs without a GPU/swapchain.
//
// Not for rendering: it draws nothing. See MASTER_ROADMAP.md Track A.
// -----------------------------------------------------------------------------

// createNullDevice() is declared in RHI/RHIBackend.h alongside the other backend factories, so a
// caller that only wants the headless device does not have to pick it out of a backend header.
// Device::create() returns it when the build selected TUCANO_RHI_NULL; Tools/RHITest calls it
// directly in every build.
