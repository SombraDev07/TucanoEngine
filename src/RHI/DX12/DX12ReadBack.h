#pragma once

#include "RHI/DX12/DX12Resource.h"

#include <functional>
#include <mutex>
#include <vector>

namespace tucano::rhi {

class DX12Device;

// Async GPU→CPU readback (Dagor ReadBackManager-inspired).
class DX12ReadBackManager {
public:
  explicit DX12ReadBackManager(DX12Device* device);

  using Callback = std::function<void(const void* data, uint64_t size)>;

  // Copies texture → staging; callback fires after fence completes (ProcessCompleted).
  bool enqueueTexture(ID3D12GraphicsCommandList* cmd, DX12Texture& src, uint32_t width, uint32_t height,
                      Format format, Callback cb);
  bool enqueueBuffer(ID3D12GraphicsCommandList* cmd, DX12Buffer& src, uint64_t size, uint64_t offset,
                     Callback cb);

  void signalPending(uint64_t fenceValue);
  void processCompleted(uint64_t completedFence);

  uint32_t pendingCount() const;

private:
  struct Request {
    ComPtr<ID3D12Resource> staging;
    uint64_t size = 0;
    uint64_t fenceValue = 0;
    Callback callback;
    bool signaled = false;
  };

  DX12Device* m_device = nullptr;
  std::vector<Request> m_pending;
  mutable std::mutex m_mutex;
  static constexpr uint32_t kMaxPending = kMaxFramesInFlight * 2;
};

} // namespace tucano::rhi
