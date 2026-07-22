#include "RHI/DX12/DX12ReadBack.h"
#include "RHI/DX12/DX12Device.h"

namespace tucano::rhi {

DX12ReadBackManager::DX12ReadBackManager(DX12Device* device) : m_device(device) {}

uint32_t DX12ReadBackManager::pendingCount() const {
  std::lock_guard lock(m_mutex);
  return static_cast<uint32_t>(m_pending.size());
}

bool DX12ReadBackManager::enqueueBuffer(ID3D12GraphicsCommandList* cmd, DX12Buffer& src, uint64_t size,
                                        uint64_t offset, Callback cb) {
  if (!cmd || !cb || size == 0) {
    return false;
  }
  std::lock_guard lock(m_mutex);
  if (m_pending.size() >= kMaxPending) {
    return false;
  }

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

  ComPtr<ID3D12Resource> staging;
  throwIfFailed(m_device->device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&staging)),
                "CreateReadbackBuffer");

  cmd->CopyBufferRegion(staging.Get(), 0, src.get(), offset, size);

  Request req;
  req.staging = staging;
  req.size = size;
  req.callback = std::move(cb);
  m_pending.push_back(std::move(req));
  return true;
}

bool DX12ReadBackManager::enqueueTexture(ID3D12GraphicsCommandList* cmd, DX12Texture& src, uint32_t width,
                                         uint32_t height, Format format, Callback cb) {
  if (!cmd || !cb) {
    return false;
  }
  const uint64_t rowPitch = (static_cast<uint64_t>(width) * 4ull + 255ull) & ~255ull;
  const uint64_t size = rowPitch * height;

  std::lock_guard lock(m_mutex);
  if (m_pending.size() >= kMaxPending) {
    return false;
  }

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

  ComPtr<ID3D12Resource> staging;
  throwIfFailed(m_device->device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&staging)),
                "CreateReadbackTextureStaging");

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = staging.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Offset = 0;
  dst.PlacedFootprint.Footprint.Format = toDxgi(format);
  dst.PlacedFootprint.Footprint.Width = width;
  dst.PlacedFootprint.Footprint.Height = height;
  dst.PlacedFootprint.Footprint.Depth = 1;
  dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = src.get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  cmd->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

  Request req;
  req.staging = staging;
  req.size = size;
  req.callback = std::move(cb);
  m_pending.push_back(std::move(req));
  return true;
}

void DX12ReadBackManager::signalPending(uint64_t fenceValue) {
  std::lock_guard lock(m_mutex);
  for (auto& r : m_pending) {
    if (!r.signaled) {
      r.fenceValue = fenceValue;
      r.signaled = true;
    }
  }
}

void DX12ReadBackManager::processCompleted(uint64_t completedFence) {
  std::vector<Request> done;
  {
    std::lock_guard lock(m_mutex);
    std::vector<Request> keep;
    for (auto& r : m_pending) {
      if (r.signaled && r.fenceValue <= completedFence) {
        done.push_back(std::move(r));
      } else {
        keep.push_back(std::move(r));
      }
    }
    m_pending = std::move(keep);
  }
  for (auto& r : done) {
    void* mapped = nullptr;
    if (SUCCEEDED(r.staging->Map(0, nullptr, &mapped)) && mapped) {
      r.callback(mapped, r.size);
      r.staging->Unmap(0, nullptr);
    }
  }
}

} // namespace tucano::rhi
