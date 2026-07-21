#pragma once

#include "RHI/DX12/DX12Common.h"

#include <string>

namespace tucano::rhi {

// GPU postmortem hooks: DRED is enabled at device create; Nvidia Aftermath
// can be linked optionally via TUCANO_AFTERMATH. Breadcrumb markers per draw.
class GpuCrashRecovery {
public:
  void beginMarker(ID3D12GraphicsCommandList* cmd, const char* name);
  void endMarker(ID3D12GraphicsCommandList* cmd);
  // Called when Present/Execute returns device removed — dump DRED breadcrumbs.
  static std::string dumpDred(ID3D12Device* device);
};

} // namespace tucano::rhi
