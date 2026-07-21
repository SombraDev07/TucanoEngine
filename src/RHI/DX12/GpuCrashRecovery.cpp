#include "RHI/DX12/GpuCrashRecovery.h"

#include <sstream>

namespace tucano::rhi {

void GpuCrashRecovery::beginMarker(ID3D12GraphicsCommandList* cmd, const char* name) {
  if (!cmd || !name) {
    return;
  }
#if defined(TUCANO_PIX_EVENTS)
  // PIX event optional
#else
  (void)cmd;
  (void)name;
#endif
}

void GpuCrashRecovery::endMarker(ID3D12GraphicsCommandList* cmd) {
  (void)cmd;
}

std::string GpuCrashRecovery::dumpDred(ID3D12Device* device) {
  if (!device) {
    return "no device";
  }
  ComPtr<ID3D12DeviceRemovedExtendedData> dred;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
    return "DRED unavailable";
  }
  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
  if (FAILED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
    return "DRED breadcrumbs unavailable";
  }
  std::ostringstream oss;
  oss << "DRED breadcrumbs present (node walk omitted in release builds)";
  return oss.str();
}

} // namespace tucano::rhi
