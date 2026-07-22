#include "RHI/DX12/GpuCrashRecovery.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#if defined(TUCANO_AFTERMATH)
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace tucano::rhi {

GpuCrashRecovery::~GpuCrashRecovery() { shutdown(); }

void GpuCrashRecovery::initialize(ID3D12Device* device) {
  (void)device;
#if defined(TUCANO_AFTERMATH)
  // Enable GPU crash dumps before heavy GPU work. Device pointer reserved for future
  // resource tracking helpers; EnableGpuCrashDumps is process-wide.
  const GFSDK_Aftermath_Result r = GFSDK_Aftermath_EnableGpuCrashDumps(
      GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
      GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, nullptr, nullptr, nullptr, nullptr,
      nullptr);
  m_aftermathEnabled = GFSDK_Aftermath_SUCCEED(r);
  if (m_aftermathEnabled && device) {
    GFSDK_Aftermath_Result ctx = GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API,
        GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
            GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugTable | GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting,
        device);
    m_aftermathContextOk = GFSDK_Aftermath_SUCCEED(ctx);
  }
#endif
}

void GpuCrashRecovery::shutdown() {
#if defined(TUCANO_AFTERMATH)
  if (m_aftermathEnabled) {
    GFSDK_Aftermath_DisableGpuCrashDumps();
  }
#endif
  m_aftermathEnabled = false;
  m_aftermathContextOk = false;
}

void GpuCrashRecovery::beginMarker(ID3D12GraphicsCommandList* cmd, const char* name) {
  if (!cmd || !name) {
    return;
  }
#if defined(TUCANO_AFTERMATH)
  if (m_aftermathContextOk) {
    GFSDK_Aftermath_SetEventMarker(cmd, name, static_cast<uint32_t>(std::strlen(name)));
  }
#endif
#if defined(TUCANO_PIX_EVENTS)
  // PIXBeginEvent(cmd, 0, name);
#else
  (void)cmd;
  (void)name;
#endif
}

void GpuCrashRecovery::endMarker(ID3D12GraphicsCommandList* cmd) {
#if defined(TUCANO_PIX_EVENTS)
  // PIXEndEvent(cmd);
#else
  (void)cmd;
#endif
}

std::string GpuCrashRecovery::dumpDred(ID3D12Device* device) {
  if (!device) {
    return "no device";
  }
  ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
    ComPtr<ID3D12DeviceRemovedExtendedData> dred0;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred0)))) {
      return "DRED unavailable";
    }
    return "DRED v0 present (upgrade OS/SDK for breadcrumb walk)";
  }

  std::ostringstream oss;
  const HRESULT removed = device->GetDeviceRemovedReason();
  oss << "DeviceRemovedReason=0x" << std::hex << static_cast<unsigned>(removed) << std::dec << "\n";

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
  if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
    uint32_t nodeCount = 0;
    for (auto* n = breadcrumbs.pHeadAutoBreadcrumbNode; n; n = n->pNext, ++nodeCount) {
      oss << "BreadcrumbNode[" << nodeCount << "] commandCount=" << n->BreadcrumbCount;
      if (n->pCommandListDebugNameA) {
        oss << " list=\"" << n->pCommandListDebugNameA << "\"";
      }
      if (n->pCommandQueueDebugNameA) {
        oss << " queue=\"" << n->pCommandQueueDebugNameA << "\"";
      }
      if (n->pLastBreadcrumbValue) {
        oss << " last=" << *n->pLastBreadcrumbValue;
      }
      oss << "\n";
      if (n->pCommandHistory && n->BreadcrumbCount > 0) {
        const UINT count = n->BreadcrumbCount;
        const UINT last = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
        // Summarize op histogram for the whole list, then a window around last.
        unsigned histCount[64]{};
        for (UINT i = 0; i < count; ++i) {
          const unsigned op = static_cast<unsigned>(n->pCommandHistory[i]);
          if (op < 64) {
            ++histCount[op];
          }
        }
        oss << "  opCounts:";
        for (unsigned op = 0; op < 64; ++op) {
          if (histCount[op]) {
            oss << " " << op << "=" << histCount[op];
          }
        }
        oss << "\n";
        const UINT window = 24u;
        UINT start = 0;
        if (last > window) {
          start = last - window;
        }
        if (start >= count) {
          start = count > window ? count - window : 0;
        }
        const UINT end = (std::min)(count, start + window * 2);
        for (UINT i = start; i < end; ++i) {
          oss << "  hist[" << i << "]=" << static_cast<unsigned>(n->pCommandHistory[i]);
          if (i + 1 == last) {
            oss << " <-- last";
          }
          oss << "\n";
        }
      }
    }
    if (nodeCount == 0) {
      oss << "No DRED breadcrumb nodes\n";
    }
  } else {
    oss << "DRED breadcrumbs unavailable\n";
  }

  D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
  if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault))) {
    oss << "PageFault VA=0x" << std::hex << pageFault.PageFaultVA << std::dec << "\n";
    auto dumpAllocs = [&](const char* label, const D3D12_DRED_ALLOCATION_NODE* node) {
      uint32_t n = 0;
      for (auto* a = node; a && n < 32; a = a->pNext, ++n) {
        oss << "  " << label << "[" << n << "] ";
        if (a->ObjectNameA) {
          oss << "\"" << a->ObjectNameA << "\" ";
        }
        oss << "type=" << static_cast<unsigned>(a->AllocationType) << "\n";
      }
      if (!node) {
        oss << "  " << label << ": (none)\n";
      }
    };
    dumpAllocs("existing", pageFault.pHeadExistingAllocationNode);
    dumpAllocs("recentFreed", pageFault.pHeadRecentFreedAllocationNode);
  }

#if defined(TUCANO_AFTERMATH)
  oss << "Aftermath: linked (TUCANO_AFTERMATH). Check .nv-gpudmp next to the exe / Nsight Graphics.\n";
#else
  oss << "Aftermath: not linked — drop SDK in 3thirdy/nsight-aftermath and reconfigure CMake.\n";
#endif
  return oss.str();
}

} // namespace tucano::rhi
