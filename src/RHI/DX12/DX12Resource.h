#pragma once

#include "RHI/DX12/DX12Common.h"

#include <algorithm>
#include <array>
#include <vector>

namespace tucano::rhi {

class DX12Device;

struct DX12Buffer final : Buffer {
  ComPtr<ID3D12Resource> resource; // alias of current backing
  std::vector<ComPtr<ID3D12Resource>> backings;
  uint32_t currentBacking = 0;
  bool dynamicDiscard = false;
  D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
  uint64_t byteSize = 0;
  void* mappedPtr = nullptr;
  std::vector<void*> mappedPtrs;
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE uavCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu{};
  uint32_t srvHeapIndex = UINT32_MAX;
  uint32_t uavHeapIndex = UINT32_MAX;
  uint32_t stride = 0;
  bool hasSrv = false;
  bool hasUav = false;
  DX12Device* device = nullptr;

  ~DX12Buffer() override;

  uint64_t size() const override { return byteSize; }
  void* mapped() override { return mappedPtr; }
  // Missing SRV/UAV → permanent null bindless slot 0 (never 0xFFFFFFFF).
  uint32_t srvIndex() const override { return hasSrv ? srvHeapIndex : 0u; }
  uint32_t uavIndex() const override { return hasUav ? uavHeapIndex : 0u; }
  ID3D12Resource* get() const { return resource.Get(); }

  void rotateBacking() {
    if (!dynamicDiscard || backings.size() < 2) {
      return;
    }
    currentBacking = (currentBacking + 1) % static_cast<uint32_t>(backings.size());
    resource = backings[currentBacking];
    gpuAddress = resource ? resource->GetGPUVirtualAddress() : 0;
    if (currentBacking < mappedPtrs.size()) {
      mappedPtr = mappedPtrs[currentBacking];
    }
  }
};

struct DX12Texture final : Texture {
  ComPtr<ID3D12Resource> resource;
  TextureDesc desc{};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE uavCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{};
  uint32_t srvIndex = UINT32_MAX;
  uint32_t uavIndex = UINT32_MAX;
  bool hasRtv = false;
  bool hasDsv = false;
  bool hasSrv = false;
  bool hasUav = false;
  DX12Device* device = nullptr;
  // Per-subresource states (mip * array * planes). Empty = use Texture::state for all.
  std::vector<ResourceState> subresourceStates;

  ~DX12Texture() override;

  uint32_t width() const override { return desc.width; }
  uint32_t height() const override { return desc.height; }
  Format format() const override { return desc.format; }
  // Missing SRV → permanent null bindless slot 0 (safe for GBuffer/Lighting sampling).
  uint32_t bindlessIndex() const override { return hasSrv ? srvIndex : 0u; }
  ID3D12Resource* get() const { return resource.Get(); }

  uint32_t subresourceCount() const {
    const uint32_t mips = std::max(1u, desc.mipLevels);
    const uint32_t layers = std::max(1u, desc.arraySize) * (desc.isCube ? 6u : 1u);
    return mips * layers; // plane 0 only for now
  }

  void ensureSubresourceTracking() {
    const uint32_t n = subresourceCount();
    if (subresourceStates.size() != n) {
      subresourceStates.assign(n, state);
    }
  }

  static uint32_t subresourceIndex(uint32_t mip, uint32_t arraySlice, uint32_t mipLevels) {
    return mip + arraySlice * mipLevels;
  }
};

struct DX12Sampler final : Sampler {
  D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
  uint32_t index = 0;
};

struct DX12RootSignature final : RootSignature {
  ComPtr<ID3D12RootSignature> rs;
  bool isCompute = false;
};

struct DX12PipelineState final : PipelineState {
  ComPtr<ID3D12PipelineState> pso;
  bool isCompute = false;
};

} // namespace tucano::rhi
