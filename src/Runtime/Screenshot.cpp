#include "Runtime/Screenshot.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Common.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <wrl/client.h>
#include <stb_image_write.h>

#include <vector>

namespace tucano {

struct ScreenshotPending::Impl {
  Microsoft::WRL::ComPtr<ID3D12Resource> readback;
};

ScreenshotPending beginScreenshot(rhi::Device& device, rhi::CommandList& cmd, rhi::Texture& backbuffer) {
  using namespace rhi;
  auto& dxDevice = static_cast<DX12Device&>(device);
  auto& tex = static_cast<DX12Texture&>(backbuffer);
  auto& dxCmd = static_cast<DX12CommandList&>(cmd);

  ScreenshotPending out;
  out.impl = std::make_shared<ScreenshotPending::Impl>();
  out.width = tex.width();
  out.height = tex.height();
  out.rowPitch = (formatRowPitch(Format::R8G8B8A8_UNORM, out.width) + 255u) & ~255u;
  const uint64_t size = static_cast<uint64_t>(out.rowPitch) * out.height;

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = size;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rhi::throwIfFailed(dxDevice.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                               IID_PPV_ARGS(&out.impl->readback)),
                     "Create screenshot readback");

  cmd.transition(backbuffer, ResourceState::CopySrc);

  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = tex.get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = out.impl->readback.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  dstLoc.PlacedFootprint.Footprint.Width = out.width;
  dstLoc.PlacedFootprint.Footprint.Height = out.height;
  dstLoc.PlacedFootprint.Footprint.Depth = 1;
  dstLoc.PlacedFootprint.Footprint.RowPitch = out.rowPitch;

  dxCmd.get()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
  return out;
}

void finalizeScreenshot(const ScreenshotPending& pending, const std::string& path) {
  if (!pending.impl || !pending.impl->readback) {
    throw std::runtime_error("Invalid screenshot pending");
  }
  void* mapped = nullptr;
  const uint64_t size = static_cast<uint64_t>(pending.rowPitch) * pending.height;
  D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
  rhi::throwIfFailed(pending.impl->readback->Map(0, &range, &mapped), "Map screenshot");

  std::vector<uint8_t> rgba(static_cast<size_t>(pending.width) * pending.height * 4);
  const auto* src = static_cast<const uint8_t*>(mapped);
  for (uint32_t y = 0; y < pending.height; ++y) {
    memcpy(rgba.data() + static_cast<size_t>(y) * pending.width * 4,
           src + static_cast<size_t>(y) * pending.rowPitch, static_cast<size_t>(pending.width) * 4);
  }
  pending.impl->readback->Unmap(0, nullptr);

  if (!stbi_write_png(path.c_str(), static_cast<int>(pending.width), static_cast<int>(pending.height), 4,
                      rgba.data(), static_cast<int>(pending.width * 4))) {
    throw std::runtime_error("stbi_write_png failed: " + path);
  }
}

} // namespace tucano
