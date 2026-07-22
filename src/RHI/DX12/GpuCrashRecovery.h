#pragma once

#include "RHI/DX12/DX12Common.h"

#include <string>

namespace tucano::rhi {

// GPU postmortem: DRED always; NVIDIA Aftermath when TUCANO_AFTERMATH + SDK in 3thirdy/.
class GpuCrashRecovery {
public:
  GpuCrashRecovery() = default;
  ~GpuCrashRecovery();

  // Call once after ID3D12Device create (enables Aftermath crash dumps if linked).
  void initialize(ID3D12Device* device);
  void shutdown();

  void beginMarker(ID3D12GraphicsCommandList* cmd, const char* name);
  void endMarker(ID3D12GraphicsCommandList* cmd);

  // Called when Present/Execute returns device removed — dump DRED (+ Aftermath note).
  static std::string dumpDred(ID3D12Device* device);

  bool aftermathEnabled() const { return m_aftermathEnabled; }

private:
  bool m_aftermathEnabled = false;
  bool m_aftermathContextOk = false;
};

} // namespace tucano::rhi
