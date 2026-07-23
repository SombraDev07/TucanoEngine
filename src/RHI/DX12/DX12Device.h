#pragma once

#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12DescriptorHeap.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/DX12Upload.h"
#include "RHI/DX12/BindlessManager.h"
#include "RHI/DX12/PipelineCache.h"
#include "RHI/DX12/SplitTransitionTracker.h"
#include "RHI/DX12/GpuCrashRecovery.h"
#include "RHI/DX12/DX12ReadBack.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tucano::rhi {

class DX12SwapChain;

class DX12Device final : public Device {
public:
  enum class DeviceState { Healthy, TdrDetected, Recovering, Dead };

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

  CommandList* beginAsyncCompute() override;
  void submitAsyncCompute() override;
  void graphicsWaitAsyncCompute() override;
  void asyncComputeWaitGraphics() override;
  CommandList* submitGraphicsCheckpoint() override;
  void submitAndWaitHeadless() override;
  void beginGpuMarker(CommandList& cmd, const char* name) override;
  void endGpuMarker(CommandList& cmd) override;
  std::string dumpGpuCrashInfo() override;

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
  void createBufferSrv(DX12Buffer& buf);
  void createBufferUav(DX12Buffer& buf);

  // Allocate contiguous SRV table entries and copy source SRVs; returns heap index of first.
  // Prefer bindless indices + table base 0; transient tables kept for legacy/fallback paths.
  uint32_t writeSrvTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  uint32_t writeUavTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpuAt(uint32_t index) const { return m_srvHeap.gpuHandle(index); }
  uint32_t writeSamplerTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count);
  D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuAt(uint32_t index) const { return m_samplerHeap.gpuHandle(index); }

  ID3D12CommandSignature* drawIndexedIndirectSig();
  bool supportsMeshShaders() const override { return m_meshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
  bool supportsRaytracing() const override {
    return m_raytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  }
  std::shared_ptr<Buffer> createAccelerationStructureBuffer(uint64_t sizeBytes,
                                                            const std::string& debugName) override;
  uint32_t createAccelerationStructureSrv(Buffer& asBuffer) override;
  void getRaytracingPrebuildInfo(uint32_t triangleCount, uint32_t vertexCount, uint64_t* resultSize,
                                 uint64_t* scratchSize) override;
  void getRaytracingTopLevelPrebuildInfo(uint32_t instanceCount, uint64_t* resultSize,
                                         uint64_t* scratchSize) override;

  ID3D12Device5* device5() const { return m_device5.Get(); }

  BindlessManager& bindless() { return m_bindless; }
  PipelineCache* pipelineCache() { return m_pipelineCache.get(); }
  SplitTransitionTracker& splitTransitions() { return m_splitTransitions; }
  uint32_t registerBindlessTexture(DX12Texture& tex);
  void unregisterBindlessTexture(uint32_t slot);

  // Split barrier helpers for graphics↔compute handoff.
  void beginSplitTransition(Texture& texture, ResourceState after);
  void endSplitTransitions(CommandList& cmd);

  DX12ReadBackManager& readback() { return *m_readback; }
  bool supportsEnhancedBarriers() const { return m_supportsEnhancedBarriers; }
  DeviceState deviceState() const { return m_deviceState; }
  bool tryRecoverDevice();

  bool isDeviceLost() const override {
    return m_deviceState == DeviceState::TdrDetected || m_deviceState == DeviceState::Dead ||
           m_deviceState == DeviceState::Recovering;
  }
  bool recoverFromDeviceLost() override { return tryRecoverDevice(); }
  void setDeviceLostCallback(std::function<void()> cb) override { m_lostCallback = std::move(cb); }

private:
  void createDevice(bool enableDebug);
  void createQueuesAndFences();
  void createHeaps();
  void createFrameResources();
  void releaseGpuObjects();
  void waitForGpu();
  void waitForCopyGpu();
  void flushUploads();
  void transitionToCopyDestDirect(ID3D12Resource* resource, ResourceState& state);
  void signalQueue(ID3D12CommandQueue* q, ID3D12Fence* fence, uint64_t& value);
  void queueWait(ID3D12CommandQueue* q, ID3D12Fence* fence, uint64_t value);
  void cpuWait(ID3D12Fence* fence, uint64_t value, HANDLE event);
  void workerLoop();
  void startWorker();
  void stopWorker();
  void submitOnWorker(ID3D12CommandList* list);

  bool m_enableDebug = true;
  ComPtr<IDXGIAdapter1> m_adapter;
  std::function<void()> m_lostCallback;

  ComPtr<IDXGIFactory6> m_factory;
  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12CommandQueue> m_queue;
  ComPtr<ID3D12CommandQueue> m_computeQueue;
  ComPtr<ID3D12CommandQueue> m_copyQueue;

  ComPtr<ID3D12Fence> m_fence;
  ComPtr<ID3D12Fence> m_computeFence;
  ComPtr<ID3D12Fence> m_copyFence;
  HANDLE m_fenceEvent = nullptr;
  HANDLE m_computeFenceEvent = nullptr;
  HANDLE m_copyFenceEvent = nullptr;
  uint64_t m_fenceValue = 0;
  uint64_t m_computeFenceValue = 0;
  uint64_t m_copyFenceValue = 0;
  uint32_t m_frameIndex = 0;
  std::array<uint64_t, kMaxFramesInFlight> m_fenceValues{};
  std::array<uint64_t, kMaxFramesInFlight> m_computeFenceValues{};

  std::array<ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> m_allocators;
  std::array<std::unique_ptr<DX12CommandList>, kMaxFramesInFlight> m_cmdLists;
  std::array<ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> m_allocatorsLate;
  std::array<std::unique_ptr<DX12CommandList>, kMaxFramesInFlight> m_cmdListsLate;
  std::array<ComPtr<ID3D12CommandAllocator>, kMaxFramesInFlight> m_computeAllocators;
  std::array<std::unique_ptr<DX12CommandList>, kMaxFramesInFlight> m_computeCmdLists;
  std::array<DX12UploadAllocator, kMaxFramesInFlight> m_upload;
  bool m_computeOpen = false;
  bool m_graphicsLate = false;

  DX12DescriptorHeap m_srvHeap;
  DX12DescriptorHeap m_rtvHeap;
  DX12DescriptorHeap m_dsvHeap;
  DX12DescriptorHeap m_samplerHeap;

  ComPtr<ID3D12CommandAllocator> m_copyAllocator;
  ComPtr<ID3D12GraphicsCommandList> m_copyList;
  bool m_copyOpen = false;
  ComPtr<ID3D12CommandAllocator> m_uploadDirectAllocator;
  ComPtr<ID3D12GraphicsCommandList> m_uploadDirectList;
  ComPtr<ID3D12CommandSignature> m_drawIndexedIndirectSig;
  BindlessManager m_bindless;
  std::unique_ptr<PipelineCache> m_pipelineCache;
  SplitTransitionTracker m_splitTransitions;
  GpuCrashRecovery m_crash;
  std::vector<std::weak_ptr<DX12Buffer>> m_dynamicBuffers;
  std::unique_ptr<DX12ReadBackManager> m_readback;
  bool m_supportsEnhancedBarriers = false;
  DeviceState m_deviceState = DeviceState::Healthy;

  // Async submit worker (GAP-6)
  struct WorkItem {
    ID3D12CommandList* list = nullptr;
    uint64_t fenceValue = 0;
  };
  std::thread m_workerThread;
  std::mutex m_workMutex;
  std::condition_variable m_workCV;
  std::deque<WorkItem> m_workQueue;
  std::atomic<bool> m_workerRunning{false};
  std::atomic<uint32_t> m_pendingSubmits{0};

  uint32_t m_srvTableCursor = 8192;
  uint32_t m_samplerTableCursor = 64;
  D3D12_MESH_SHADER_TIER m_meshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
  D3D12_RAYTRACING_TIER m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  ComPtr<ID3D12Device5> m_device5;
};

} // namespace tucano::rhi
