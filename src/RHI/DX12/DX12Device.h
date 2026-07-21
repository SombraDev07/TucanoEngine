#pragma once

#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12DescriptorHeap.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/DX12Upload.h"
#include "RHI/DX12/BindlessManager.h"
#include "RHI/DX12/PipelineCache.h"
#include "RHI/DX12/SplitTransitionTracker.h"

#include <array>
#include <deque>
#include <memory>

namespace tucano::rhi {

class DX12SwapChain;

class DX12Device final : public Device {
public:
  explicit DX12Device(bool enableDebugLayer);
  ~DX12Device() override;

  std::unique_ptr<SwapChain> createSwapChain(void* hwnd, uint32_t width, uint32_t height, bool vsync) override;
  std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initialData) override;
  std::shared_ptr<Texture> createTexture(const TextureDesc& desc, const void* initialData, uint32_t rowPitch) override;
  std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) override;
  std::shared_ptr<RootSignature> createRootSignature(bool allowInputLayout) override;
  std::shared_ptr<RootSignature> createComputeRootSignature() override;
  std::shared_ptr<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
  std::shared_ptr<PipelineState> createComputePipeline(const ComputePipelineDesc& desc) override;

  CommandList* beginFrame() override;
  void endFrame(SwapChain& swapChain) override;
  void waitIdle() override;
  uint32_t frameIndex() const override { return m_frameIndex; }
  uint64_t frameFenceValue() const override { return m_fenceValues[m_frameIndex]; }

  void uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset) override;
  void uploadTexture(Texture& texture, const void* data, uint32_t width, uint32_t height, uint32_t rowPitch,
                     uint32_t mip, uint32_t arraySlice) override;

  void* nativeDevice() const override { return m_device.Get(); }

  ID3D12Device* device() const { return m_device.Get(); }
  ID3D12CommandQueue* queue() const { return m_queue.Get(); }
  ID3D12CommandQueue* computeQueue() const { return m_computeQueue.Get(); }
  ID3D12CommandQueue* copyQueue() const { return m_copyQueue.Get(); }
  IDXGIFactory4* factory() const { return m_factory.Get(); }
  DX12DescriptorHeap& srvHeap() { return m_srvHeap; }
  DX12DescriptorHeap& rtvHeap() { return m_rtvHeap; }
  DX12DescriptorHeap& dsvHeap() { return m_dsvHeap; }
  DX12DescriptorHeap& samplerHeap() { return m_samplerHeap; }
  DX12UploadAllocator& upload() { return m_upload[m_frameIndex]; }

  void createSrv(DX12Texture& tex);
  void createRtv(DX12Texture& tex);
  void createDsv(DX12Texture& tex);
  void createUav(DX12Texture& tex);

  // Allocate contiguous SRV table entries and copy source SRVs; returns heap index of first.
  uint32_t writeSrvTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  uint32_t writeUavTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpuAt(uint32_t index) const { return m_srvHeap.gpuHandle(index); }
  uint32_t writeSamplerTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuAt(uint32_t index) const { return m_samplerHeap.gpuHandle(index); }

  ID3D12CommandSignature* drawIndexedIndirectSig();

  BindlessManager& bindless() { return m_bindless; }
  PipelineCache* pipelineCache() { return m_pipelineCache.get(); }
  SplitTransitionTracker& splitTransitions() { return m_splitTransitions; }
  uint32_t registerBindlessTexture(DX12Texture& tex);
  void unregisterBindlessTexture(uint32_t slot);

private:
  void createDevice(bool enableDebug);
  void createFrameResources();
  void waitForGpu();
  void flushUploads();

  ComPtr<IDXGIFactory6> m_factory;
  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12CommandQueue> m_queue;
  ComPtr<ID3D12CommandQueue> m_computeQueue;
  ComPtr<ID3D12CommandQueue> m_copyQueue;
  ComPtr<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  uint64_t m_fenceValue = 0;
  uint32_t m_frameIndex = 0;
  std::array<uint64_t, kMaxFramesInFlight> m_fenceValues{};

  std::array<ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> m_allocators;
  std::array<std::unique_ptr<DX12CommandList>, kMaxFramesInFlight> m_cmdLists;
  std::array<DX12UploadAllocator, kMaxFramesInFlight> m_upload;

  DX12DescriptorHeap m_srvHeap;
  DX12DescriptorHeap m_rtvHeap;
  DX12DescriptorHeap m_dsvHeap;
  DX12DescriptorHeap m_samplerHeap;

  ComPtr<ID3D12CommandAllocator> m_copyAllocator;
  ComPtr<ID3D12GraphicsCommandList> m_copyList;
  bool m_copyOpen = false;
  ComPtr<ID3D12CommandSignature> m_drawIndexedIndirectSig;
  BindlessManager m_bindless;
  std::unique_ptr<PipelineCache> m_pipelineCache;
  SplitTransitionTracker m_splitTransitions;

  uint32_t m_srvTableCursor = 8192; // above bindless pool
  uint32_t m_samplerTableCursor = 64;
};

} // namespace tucano::rhi
