#pragma once

#include "RHI/DX12/DX12Common.h"

namespace tucano::rhi {

struct DX12Buffer final : Buffer {
  ComPtr<ID3D12Resource> resource;
  D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
  uint64_t byteSize = 0;
  void* mappedPtr = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu{};
  uint32_t srvIndex = UINT32_MAX;

  uint64_t size() const override { return byteSize; }
  void* mapped() override { return mappedPtr; }
  ID3D12Resource* get() const { return resource.Get(); }
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

  uint32_t width() const override { return desc.width; }
  uint32_t height() const override { return desc.height; }
  Format format() const override { return desc.format; }
  uint32_t bindlessIndex() const override { return hasSrv ? srvIndex : ~0u; }
  ID3D12Resource* get() const { return resource.Get(); }
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
