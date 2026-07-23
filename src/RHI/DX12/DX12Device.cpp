#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12SwapChain.h"

#include <d3d12sdklayers.h>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {
UINT calcSubresource(UINT mip, UINT item, UINT plane, UINT mipLevels, UINT arraySize) {
  return mip + item * mipLevels + plane * mipLevels * arraySize;
}
} // namespace

namespace tucano::rhi {

std::unique_ptr<Device> Device::create(bool enableDebugLayer) {
  return std::make_unique<DX12Device>(enableDebugLayer);
}

DX12Device::DX12Device(bool enableDebugLayer) {
  m_enableDebug = enableDebugLayer;
  createDevice(enableDebugLayer);
  createFrameResources();
  // Bindless [0,8192), transient tables [8192,16384)
  m_bindless.init(8192);
  m_pipelineCache = std::make_unique<PipelineCache>("cache/dx12_pso.cache");
  if (m_pipelineCache->load()) {
    // hits loaded from disk
  }
}

DX12Device::~DX12Device() {
  stopWorker();
  try {
    waitIdle();
  } catch (...) {
  }
  if (m_pipelineCache) {
    try {
      m_pipelineCache->save();
    } catch (...) {
    }
  }
  if (m_fenceEvent) {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
  if (m_computeFenceEvent) {
    CloseHandle(m_computeFenceEvent);
    m_computeFenceEvent = nullptr;
  }
  if (m_copyFenceEvent) {
    CloseHandle(m_copyFenceEvent);
    m_copyFenceEvent = nullptr;
  }
}

void DX12Device::createDevice(bool enableDebug) {
  UINT factoryFlags = 0;

  {
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
      dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
      dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
  }

  if (enableDebug) {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }

  if (!m_factory) {
    throwIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");
  }

  m_adapter.Reset();
  m_device.Reset();
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)))) {
      m_adapter = adapter;
      break;
    }
    m_device.Reset();
  }
  if (!m_device) {
    throwIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice");
  }

  m_crash.shutdown();
  m_crash.initialize(m_device.Get());

  {
#if defined(D3D12_FEATURE_D3D12_OPTIONS12)
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 opt12{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &opt12, sizeof(opt12)))) {
      m_supportsEnhancedBarriers = opt12.EnhancedBarriersSupported == TRUE;
    }
#endif
  }

  if (enableDebug) {
    ComPtr<ID3D12InfoQueue> info;
    if (SUCCEEDED(m_device.As(&info))) {
      info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
      info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
      D3D12_MESSAGE_ID hide[] = {
          D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
          D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
      };
      D3D12_INFO_QUEUE_FILTER filter{};
      filter.DenyList.NumIDs = _countof(hide);
      filter.DenyList.pIDList = hide;
      info->AddStorageFilterEntries(&filter);
    }
  }

  createQueuesAndFences();
  createHeaps();
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7)))) {
      m_meshShaderTier = opt7.MeshShaderTier;
    }
  }
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
      m_raytracingTier = opt5.RaytracingTier;
    }
    if (m_raytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      m_device.As(&m_device5);
    }
  }
  m_readback = std::make_unique<DX12ReadBackManager>(this);
  startWorker();
}

void DX12Device::createQueuesAndFences() {
  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  throwIfFailed(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue");

  D3D12_COMMAND_QUEUE_DESC cq{};
  cq.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  throwIfFailed(m_device->CreateCommandQueue(&cq, IID_PPV_ARGS(&m_computeQueue)), "CreateComputeQueue");
  cq.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  throwIfFailed(m_device->CreateCommandQueue(&cq, IID_PPV_ARGS(&m_copyQueue)), "CreateCopyQueue");

  throwIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");
  throwIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence)), "CreateComputeFence");
  throwIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)), "CreateCopyFence");
  m_fenceValue = 0;
  m_computeFenceValue = 0;
  m_copyFenceValue = 0;
  m_frameIndex = 0;
  m_fenceValues = {};
  m_computeFenceValues = {};

  if (!m_fenceEvent) {
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }
  if (!m_computeFenceEvent) {
    m_computeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }
  if (!m_copyFenceEvent) {
    m_copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }
  if (!m_fenceEvent || !m_computeFenceEvent || !m_copyFenceEvent) {
    throw std::runtime_error("CreateEvent failed");
  }
}

void DX12Device::createHeaps() {
  m_srvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16384, true);
  (void)m_srvHeap.allocate(8192);
  m_rtvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2048, false);
  m_dsvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256, false);
  m_samplerHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 512, true);
  m_bindless.init(8192);
  m_srvTableCursor = 8192;
  m_samplerTableCursor = 64;

  // UINT_MAX bindless tables + Tier-1 rules: every heap slot must be a valid descriptor.
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    for (uint32_t i = 0; i < m_srvHeap.capacity(); ++i) {
      m_device->CreateShaderResourceView(nullptr, &nullSrv, m_srvHeap.cpuHandle(i));
    }
  }
  {
    D3D12_SAMPLER_DESC nullSamp{};
    nullSamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    nullSamp.AddressU = nullSamp.AddressV = nullSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    nullSamp.MaxLOD = D3D12_FLOAT32_MAX;
    for (uint32_t i = 0; i < m_samplerHeap.capacity(); ++i) {
      m_device->CreateSampler(&nullSamp, m_samplerHeap.cpuHandle(i));
    }
  }
}

void DX12Device::releaseGpuObjects() {
  stopWorker();
  m_computeOpen = false;
  m_graphicsLate = false;
  m_copyOpen = false;
  m_pendingSubmits = 0;
  {
    std::lock_guard lock(m_workMutex);
    m_workQueue.clear();
  }

  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    m_cmdLists[i].reset();
    m_cmdListsLate[i].reset();
    m_computeCmdLists[i].reset();
    m_allocators[i].Reset();
    m_allocatorsLate[i].Reset();
    m_computeAllocators[i].Reset();
  }
  m_copyList.Reset();
  m_copyAllocator.Reset();
  m_uploadDirectList.Reset();
  m_uploadDirectAllocator.Reset();
  m_drawIndexedIndirectSig.Reset();
  m_readback.reset();
  m_dynamicBuffers.clear();
  m_splitTransitions.reset();

  m_queue.Reset();
  m_computeQueue.Reset();
  m_copyQueue.Reset();
  m_fence.Reset();
  m_computeFence.Reset();
  m_copyFence.Reset();
  m_crash.shutdown();
  m_device.Reset();
  m_device5.Reset();
  m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

bool DX12Device::tryRecoverDevice() {
  if (m_deviceState != DeviceState::TdrDetected && m_deviceState != DeviceState::Recovering) {
    return false;
  }
  m_deviceState = DeviceState::Recovering;
  std::cerr << "[DX12Device] TDR detected — attempting recovery...\n";
  std::cerr << dumpGpuCrashInfo() << "\n";

  try {
    releaseGpuObjects();
    // Prefer same adapter if still valid.
    m_device.Reset();
    if (m_adapter) {
      if (FAILED(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)))) {
        m_device.Reset();
      }
    }
    if (!m_device) {
      createDevice(m_enableDebug);
    } else {
      m_crash.initialize(m_device.Get());
#if defined(D3D12_FEATURE_D3D12_OPTIONS12)
      {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 opt12{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &opt12, sizeof(opt12)))) {
          m_supportsEnhancedBarriers = opt12.EnhancedBarriersSupported == TRUE;
        }
      }
#endif
      createQueuesAndFences();
      createHeaps();
      {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7)))) {
          m_meshShaderTier = opt7.MeshShaderTier;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5)))) {
          m_raytracingTier = opt5.RaytracingTier;
        }
        m_device5.Reset();
        if (m_raytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
          m_device.As(&m_device5);
        }
      }
      m_readback = std::make_unique<DX12ReadBackManager>(this);
      startWorker();
    }
    createFrameResources();

    if (m_lostCallback) {
      m_lostCallback();
    }

    m_deviceState = DeviceState::Healthy;
    std::cerr << "[DX12Device] Recovery succeeded — GPU objects recreated.\n";
    return true;
  } catch (const std::exception& ex) {
    std::cerr << "[DX12Device] Recovery failed: " << ex.what() << "\n";
    m_deviceState = DeviceState::Dead;
    return false;
  }
}

void DX12Device::createFrameResources() {
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    throwIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i])),
                  "CreateCommandAllocator");
    m_cmdLists[i] = std::make_unique<DX12CommandList>(this, m_allocators[i].Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    throwIfFailed(
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocatorsLate[i])),
        "CreateLateAllocator");
    m_cmdListsLate[i] =
        std::make_unique<DX12CommandList>(this, m_allocatorsLate[i].Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    throwIfFailed(
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeAllocators[i])),
        "CreateComputeAllocator");
    m_computeCmdLists[i] =
        std::make_unique<DX12CommandList>(this, m_computeAllocators[i].Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    m_upload[i].init(m_device.Get(), 512ull * 1024ull * 1024ull);
    m_fenceValues[i] = 0;
    m_computeFenceValues[i] = 0;
  }
  throwIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyAllocator)),
                "Create copy allocator");
  throwIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyAllocator.Get(), nullptr,
                                          IID_PPV_ARGS(&m_copyList)),
                "Create copy list");
  m_copyList->Close();
  m_copyOpen = false;
  throwIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadDirectAllocator)),
                "Create upload-direct allocator");
  throwIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_uploadDirectAllocator.Get(), nullptr,
                                          IID_PPV_ARGS(&m_uploadDirectList)),
                "Create upload-direct list");
  m_uploadDirectList->Close();
}

void DX12Device::cpuWait(ID3D12Fence* fence, uint64_t value, HANDLE event) {
  if (!fence || !event) {
    return;
  }
  if (fence->GetCompletedValue() >= value) {
    return;
  }
  throwIfFailed(fence->SetEventOnCompletion(value, event), "SetEventOnCompletion");
  WaitForSingleObject(event, INFINITE);
}

void DX12Device::signalQueue(ID3D12CommandQueue* q, ID3D12Fence* fence, uint64_t& value) {
  if (!q || !fence) {
    return;
  }
  const uint64_t v = ++value;
  throwIfFailed(q->Signal(fence, v), "Queue Signal");
}

void DX12Device::queueWait(ID3D12CommandQueue* q, ID3D12Fence* fence, uint64_t value) {
  if (!q || !fence || value == 0) {
    return;
  }
  throwIfFailed(q->Wait(fence, value), "Queue Wait");
}

void DX12Device::waitForGpu() {
  if (!m_queue || !m_fence) {
    return;
  }
  signalQueue(m_queue.Get(), m_fence.Get(), m_fenceValue);
  cpuWait(m_fence.Get(), m_fenceValue, m_fenceEvent);
}

void DX12Device::waitForCopyGpu() {
  if (!m_copyFence || m_copyFenceValue == 0) {
    return;
  }
  cpuWait(m_copyFence.Get(), m_copyFenceValue, m_copyFenceEvent);
}

void DX12Device::waitIdle() {
  // After a failed TDR recovery, queues/fences are released — never null-deref on teardown.
  if (!m_queue || !m_fence) {
    return;
  }
  waitForGpu();
  if (m_computeQueue && m_computeFence) {
    signalQueue(m_computeQueue.Get(), m_computeFence.Get(), m_computeFenceValue);
    cpuWait(m_computeFence.Get(), m_computeFenceValue, m_computeFenceEvent);
  }
  waitForCopyGpu();
}

CommandList* DX12Device::beginFrame() {
  if (m_deviceState == DeviceState::TdrDetected || m_deviceState == DeviceState::Dead) {
    if (!tryRecoverDevice()) {
      throw std::runtime_error("Device lost and recovery failed");
    }
  }
  if (!m_fence || !m_cmdLists[m_frameIndex]) {
    throw std::runtime_error("beginFrame: device not ready");
  }

  const uint64_t completed = m_fence->GetCompletedValue();
  if (completed == UINT64_MAX) {
    m_deviceState = DeviceState::TdrDetected;
    if (!tryRecoverDevice()) {
      throw std::runtime_error("Device removed (fence) and recovery failed");
    }
    return beginFrame();
  }
  if (m_fenceValues[m_frameIndex] != 0 && completed < m_fenceValues[m_frameIndex]) {
    cpuWait(m_fence.Get(), m_fenceValues[m_frameIndex], m_fenceEvent);
  }
  if (m_computeFenceValues[m_frameIndex] != 0) {
    cpuWait(m_computeFence.Get(), m_computeFenceValues[m_frameIndex], m_computeFenceEvent);
  }
  m_bindless.flushDeferredFrees(completed);
  m_srvHeap.flushDeferredFrees(completed);
  m_samplerHeap.flushDeferredFrees(completed);
  if (m_pipelineCache) {
    m_pipelineCache->pumpAsync();
  }
  m_upload[m_frameIndex].reset();
  m_srvTableCursor = 8192;
  m_samplerTableCursor = 64;

  // Rotate dynamic buffer backings (discard) so GPU may still use previous frames' maps.
  for (auto it = m_dynamicBuffers.begin(); it != m_dynamicBuffers.end();) {
    if (auto buf = it->lock()) {
      buf->rotateBacking();
      ++it;
    } else {
      it = m_dynamicBuffers.erase(it);
    }
  }

  m_cmdLists[m_frameIndex]->reset(m_allocators[m_frameIndex].Get());
  m_computeOpen = false;
  m_graphicsLate = false;
  return m_cmdLists[m_frameIndex].get();
}

void DX12Device::endFrame(SwapChain& swapChain) {
  if (m_computeOpen) {
    submitAsyncCompute();
    graphicsWaitAsyncCompute();
  }
  CommandList* active = m_graphicsLate ? m_cmdListsLate[m_frameIndex].get() : m_cmdLists[m_frameIndex].get();
  static_cast<DX12CommandList*>(active)->close();
  ID3D12CommandList* lists[] = {static_cast<DX12CommandList*>(active)->get()};
  submitOnWorker(lists[0]);

  if (m_readback) {
    m_readback->signalPending(m_fenceValue + 1);
  }

  try {
    swapChain.present();
  } catch (...) {
    m_deviceState = DeviceState::TdrDetected;
    const std::string dred = dumpGpuCrashInfo();
    if (tryRecoverDevice()) {
      m_graphicsLate = false;
      return;
    }
    throw std::runtime_error(std::string("Present failed / device removed. ") + dred);
  }

  if (!m_device) {
    throw std::runtime_error("endFrame: device is null");
  }
  const HRESULT reason = m_device->GetDeviceRemovedReason();
  if (FAILED(reason)) {
    m_deviceState = DeviceState::TdrDetected;
    const std::string dred = dumpGpuCrashInfo();
    if (tryRecoverDevice()) {
      m_graphicsLate = false;
      return;
    }
    throw std::runtime_error(std::string("Device removed after present. ") + dred);
  }

  signalQueue(m_queue.Get(), m_fence.Get(), m_fenceValue);
  m_fenceValues[m_frameIndex] = m_fenceValue;
  if (m_readback && m_fence) {
    m_readback->processCompleted(m_fence->GetCompletedValue());
  }
  m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
  m_graphicsLate = false;
}

void DX12Device::submitOnWorker(ID3D12CommandList* list) {
  m_pendingSubmits.fetch_add(1);
  {
    std::lock_guard lock(m_workMutex);
    m_workQueue.push_back({list, m_fenceValue + 1});
  }
  m_workCV.notify_one();
  std::unique_lock lock(m_workMutex);
  m_workCV.wait(lock, [&] { return m_pendingSubmits.load() == 0 || !m_workerRunning; });
}

void DX12Device::startWorker() {
  if (m_workerRunning) {
    return;
  }
  m_workerRunning = true;
  m_workerThread = std::thread([this] { workerLoop(); });
}

void DX12Device::stopWorker() {
  if (!m_workerRunning) {
    return;
  }
  {
    std::lock_guard lock(m_workMutex);
    m_workerRunning = false;
  }
  m_workCV.notify_all();
  if (m_workerThread.joinable()) {
    m_workerThread.join();
  }
}

void DX12Device::workerLoop() {
  while (true) {
    WorkItem item{};
    {
      std::unique_lock lock(m_workMutex);
      m_workCV.wait(lock, [&] { return !m_workQueue.empty() || !m_workerRunning; });
      if (!m_workerRunning && m_workQueue.empty()) {
        return;
      }
      if (m_workQueue.empty()) {
        continue;
      }
      item = m_workQueue.front();
      m_workQueue.pop_front();
    }
    if (item.list && m_queue) {
      m_queue->ExecuteCommandLists(1, &item.list);
    }
    m_pendingSubmits.fetch_sub(1);
    m_workCV.notify_all();
  }
}

CommandList* DX12Device::beginAsyncCompute() {
  if (m_computeOpen) {
    throw std::runtime_error("beginAsyncCompute: compute list already open");
  }
  m_computeCmdLists[m_frameIndex]->reset(m_computeAllocators[m_frameIndex].Get());
  m_computeOpen = true;
  return m_computeCmdLists[m_frameIndex].get();
}

void DX12Device::submitAsyncCompute() {
  if (!m_computeOpen) {
    return;
  }
  m_computeCmdLists[m_frameIndex]->close();
  ID3D12CommandList* lists[] = {m_computeCmdLists[m_frameIndex]->get()};
  m_computeQueue->ExecuteCommandLists(1, lists);
  signalQueue(m_computeQueue.Get(), m_computeFence.Get(), m_computeFenceValue);
  m_computeFenceValues[m_frameIndex] = m_computeFenceValue;
  m_computeOpen = false;
}

void DX12Device::graphicsWaitAsyncCompute() {
  queueWait(m_queue.Get(), m_computeFence.Get(), m_computeFenceValues[m_frameIndex]);
}

void DX12Device::asyncComputeWaitGraphics() {
  queueWait(m_computeQueue.Get(), m_fence.Get(), m_fenceValue);
}

void DX12Device::submitAndWaitHeadless() {
  // Close and execute the primary list of the current frame, then block on the fence. No swapchain,
  // no late list — the next beginFrame() resets cleanly, so this is safe to call once per frame in
  // a loop (which submitGraphicsCheckpoint is not, because it leaves the late list open).
  static_cast<DX12CommandList*>(m_cmdLists[m_frameIndex].get())->close();
  ID3D12CommandList* lists[] = {static_cast<DX12CommandList*>(m_cmdLists[m_frameIndex].get())->get()};
  m_queue->ExecuteCommandLists(1, lists);
  waitForGpu();
  m_fenceValues[m_frameIndex] = m_fenceValue;
}

CommandList* DX12Device::submitGraphicsCheckpoint() {
  if (m_graphicsLate) {
    throw std::runtime_error("submitGraphicsCheckpoint: already on late graphics list");
  }
  m_cmdLists[m_frameIndex]->close();
  ID3D12CommandList* lists[] = {m_cmdLists[m_frameIndex]->get()};
  m_queue->ExecuteCommandLists(1, lists);
  signalQueue(m_queue.Get(), m_fence.Get(), m_fenceValue);
  m_fenceValues[m_frameIndex] = m_fenceValue;
  m_cmdListsLate[m_frameIndex]->reset(m_allocatorsLate[m_frameIndex].Get());
  m_graphicsLate = true;
  return m_cmdListsLate[m_frameIndex].get();
}

void DX12Device::beginGpuMarker(CommandList& cmd, const char* name) {
  auto& dx = static_cast<DX12CommandList&>(cmd);
  m_crash.beginMarker(dx.get(), name);
}

void DX12Device::endGpuMarker(CommandList& cmd) {
  auto& dx = static_cast<DX12CommandList&>(cmd);
  m_crash.endMarker(dx.get());
}

std::string DX12Device::dumpGpuCrashInfo() { return GpuCrashRecovery::dumpDred(m_device.Get()); }

void DX12Device::beginSplitTransition(Texture& texture, ResourceState after) {
  auto& tex = static_cast<DX12Texture&>(texture);
  if (tex.state == after) {
    return;
  }
  m_splitTransitions.begin(tex.get(), toD3D(tex.state), toD3D(after));
  tex.state = after;
}

void DX12Device::endSplitTransitions(CommandList& cmd) {
  auto& dx = static_cast<DX12CommandList&>(cmd);
  m_splitTransitions.end(dx.get());
}

std::unique_ptr<SwapChain> DX12Device::createSwapChain(void* hwnd, uint32_t width, uint32_t height, bool vsync) {
  return std::make_unique<DX12SwapChain>(this, hwnd, width, height, vsync);
}

void DX12Device::createSrv(DX12Texture& tex) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = toDxgi(tex.desc.format);
  if (tex.desc.depth > 1) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Texture3D.MipLevels = tex.desc.mipLevels;
  } else if (tex.desc.isCube) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.TextureCube.MipLevels = tex.desc.mipLevels;
  } else if (tex.desc.arraySize > 1) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = tex.desc.mipLevels;
    srv.Texture2DArray.ArraySize = tex.desc.arraySize;
  } else {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = tex.desc.mipLevels;
  }
  // Bindless-stable slot from free-list (0..8191)
  tex.srvIndex = m_bindless.allocate(1);
  if (tex.srvIndex == UINT32_MAX) {
    throw std::runtime_error("Bindless SRV heap exhausted");
  }
  tex.srvCpu = m_srvHeap.cpuHandle(tex.srvIndex);
  tex.srvGpu = m_srvHeap.gpuHandle(tex.srvIndex);
  m_device->CreateShaderResourceView(tex.get(), &srv, tex.srvCpu);
  tex.hasSrv = true;
}

uint32_t DX12Device::registerBindlessTexture(DX12Texture& tex) {
  if (!tex.hasSrv) {
    createSrv(tex);
  }
  return tex.srvIndex;
}

void DX12Device::unregisterBindlessTexture(uint32_t slot) {
  m_bindless.deferFree(slot, 1, m_fenceValue);
}

void DX12Device::createRtv(DX12Texture& tex) {
  tex.rtv = m_rtvHeap.allocateCpu();
  D3D12_RENDER_TARGET_VIEW_DESC rtv{};
  rtv.Format = toDxgi(tex.desc.format);
  rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  m_device->CreateRenderTargetView(tex.get(), &rtv, tex.rtv);
  tex.hasRtv = true;
}

void DX12Device::createDsv(DX12Texture& tex) {
  tex.dsv = m_dsvHeap.allocateCpu();
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = toDxgi(tex.desc.format);
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  m_device->CreateDepthStencilView(tex.get(), &dsv, tex.dsv);
  tex.hasDsv = true;
}

void DX12Device::createUav(DX12Texture& tex) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = toDxgi(tex.desc.format);
  if (tex.desc.depth > 1) {
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav.Texture3D.MipSlice = 0;
    uav.Texture3D.FirstWSlice = 0;
    uav.Texture3D.WSize = tex.desc.depth;
  } else {
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = 0;
  }
  tex.uavIndex = m_bindless.allocate(1);
  if (tex.uavIndex == UINT32_MAX) {
    throw std::runtime_error("Bindless UAV heap exhausted");
  }
  tex.uavCpu = m_srvHeap.cpuHandle(tex.uavIndex);
  tex.uavGpu = m_srvHeap.gpuHandle(tex.uavIndex);
  m_device->CreateUnorderedAccessView(tex.get(), nullptr, &uav, tex.uavCpu);
  tex.hasUav = true;
}

std::shared_ptr<Buffer> DX12Device::createBuffer(const BufferDesc& desc, const void* initialData) {
  auto buf = std::make_shared<DX12Buffer>();
  buf->device = this;
  buf->byteSize = desc.size;
  buf->usage = desc.usage;
  buf->stride = desc.stride;

  const bool wantUav = any(desc.usage, BufferUsage::UnorderedAccess);
  const bool isAS = any(desc.usage, BufferUsage::AccelerationStructure);
  const bool wantSrv = !isAS && (any(desc.usage, BufferUsage::Structured) || wantUav);
  const bool gpuWritable = wantUav || (any(desc.usage, BufferUsage::Indirect) && wantUav);
  // DEFAULT heap when GPU will write (UAV) or when Structured SRV is needed without Upload.
  const bool forceDefault =
      wantUav || (any(desc.usage, BufferUsage::Structured) && !any(desc.usage, BufferUsage::Upload));

  const bool upload = !forceDefault && (any(desc.usage, BufferUsage::Upload) || initialData != nullptr);
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = upload && any(desc.usage, BufferUsage::Upload) ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
  if (any(desc.usage, BufferUsage::Constant) && !initialData && !forceDefault) {
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  }
  if (forceDefault) {
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  }
  if (any(desc.usage, BufferUsage::Readback)) {
    heap.Type = D3D12_HEAP_TYPE_READBACK;
  }

  const bool wantDiscard =
      any(desc.usage, BufferUsage::Dynamic) ||
      (heap.Type == D3D12_HEAP_TYPE_UPLOAD && any(desc.usage, BufferUsage::Constant));
  const uint32_t discardCount = wantDiscard ? kDynamicBufferDiscardCount : 1;
  buf->dynamicDiscard = wantDiscard && discardCount > 1;
  buf->backings.resize(discardCount);
  buf->mappedPtrs.resize(discardCount, nullptr);

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = desc.size;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (wantUav) {
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  if (isAS) {
    state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    buf->state = ResourceState::AccelerationStructure;
  }
  if (heap.Type == D3D12_HEAP_TYPE_UPLOAD) {
    state = D3D12_RESOURCE_STATE_GENERIC_READ;
    buf->state = ResourceState::ConstantBuffer;
  }
  if (heap.Type == D3D12_HEAP_TYPE_READBACK) {
    state = D3D12_RESOURCE_STATE_COPY_DEST;
    buf->state = ResourceState::CopyDst;
  }

  for (uint32_t i = 0; i < discardCount; ++i) {
    throwIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                                   IID_PPV_ARGS(&buf->backings[i])),
                  "CreateBuffer");
    if (heap.Type == D3D12_HEAP_TYPE_UPLOAD) {
      throwIfFailed(buf->backings[i]->Map(0, nullptr, &buf->mappedPtrs[i]), "Map buffer");
      if (initialData && i == 0) {
        memcpy(buf->mappedPtrs[i], initialData, static_cast<size_t>(desc.size));
      }
    }
    // Readback heaps are persistently mapped for CPU read after a GPU→CPU copy completes. Passing a
    // null read range means "the whole buffer may have been written by the GPU". Readback buffers
    // are single-backed, so mappedPtr (what mapped() returns) mirrors backing 0.
    if (heap.Type == D3D12_HEAP_TYPE_READBACK) {
      throwIfFailed(buf->backings[i]->Map(0, nullptr, &buf->mappedPtrs[i]), "Map readback buffer");
      if (i == 0) buf->mappedPtr = buf->mappedPtrs[i];
    }
    if (!desc.debugName.empty()) {
      std::wstring w(desc.debugName.begin(), desc.debugName.end());
      w += L"#" + std::to_wstring(i);
      buf->backings[i]->SetName(w.c_str());
    }
  }

  buf->currentBacking = 0;
  buf->resource = buf->backings[0];
  buf->gpuAddress = buf->resource->GetGPUVirtualAddress();
  buf->mappedPtr = buf->mappedPtrs.empty() ? nullptr : buf->mappedPtrs[0];

  if (heap.Type != D3D12_HEAP_TYPE_UPLOAD && initialData) {
    uploadBuffer(*buf, initialData, desc.size, 0);
  }

  if (wantSrv) {
    createBufferSrv(*buf);
  }
  if (wantUav && !isAS) {
    createBufferUav(*buf);
  }

  if (buf->dynamicDiscard) {
    m_dynamicBuffers.push_back(buf);
  }
  (void)gpuWritable;
  return buf;
}

void DX12Device::createBufferSrv(DX12Buffer& buf) {
  // Buffer SRVs live in the transient half of the SRV heap (not texture bindless 0..8191).
  // Mixing Buffer descriptors into the Texture2D bindless range causes page-fault TDRs when
  // GBuffer/Lighting sample bindlessHeap[idx] as Texture2D.
  buf.srvHeapIndex = m_srvHeap.allocate(1);
  if (buf.srvHeapIndex == UINT32_MAX) {
    throw std::runtime_error("SRV heap full (buffer SRV)");
  }
  buf.srvCpu = m_srvHeap.cpuHandle(buf.srvHeapIndex);
  buf.srvGpu = m_srvHeap.gpuHandle(buf.srvHeapIndex);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  const uint32_t stride = buf.stride ? buf.stride : 4u;
  srv.Buffer.FirstElement = 0;
  srv.Buffer.NumElements = static_cast<UINT>(buf.byteSize / stride);
  srv.Buffer.StructureByteStride = stride;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  m_device->CreateShaderResourceView(buf.get(), &srv, buf.srvCpu);
  buf.hasSrv = true;
}

void DX12Device::createBufferUav(DX12Buffer& buf) {
  buf.uavHeapIndex = m_srvHeap.allocate(1);
  if (buf.uavHeapIndex == UINT32_MAX) {
    throw std::runtime_error("SRV heap full (buffer UAV)");
  }
  buf.uavCpu = m_srvHeap.cpuHandle(buf.uavHeapIndex);
  buf.uavGpu = m_srvHeap.gpuHandle(buf.uavHeapIndex);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  const uint32_t stride = buf.stride ? buf.stride : 4u;
  uav.Buffer.FirstElement = 0;
  uav.Buffer.NumElements = static_cast<UINT>(buf.byteSize / stride);
  uav.Buffer.StructureByteStride = stride;
  uav.Buffer.CounterOffsetInBytes = 0;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  m_device->CreateUnorderedAccessView(buf.get(), nullptr, &uav, buf.uavCpu);
  buf.hasUav = true;
}

void DX12Device::transitionToCopyDestDirect(ID3D12Resource* resource, ResourceState& state) {
  if (state == ResourceState::CopyDst) {
    return;
  }
  // COPY queues only accept COMMON / COPY_SOURCE / COPY_DEST. Anything else must move on DIRECT first.
  if (state == ResourceState::Common || state == ResourceState::CopySrc) {
    return;
  }
  if (m_copyOpen) {
    flushUploads();
  }
  waitForGpu();
  throwIfFailed(m_uploadDirectAllocator->Reset(), "Reset upload-direct allocator");
  throwIfFailed(m_uploadDirectList->Reset(m_uploadDirectAllocator.Get(), nullptr), "Reset upload-direct list");
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = resource;
  b.Transition.StateBefore = toD3D(state);
  b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_uploadDirectList->ResourceBarrier(1, &b);
  throwIfFailed(m_uploadDirectList->Close(), "Close upload-direct list");
  ID3D12CommandList* lists[] = {m_uploadDirectList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  signalQueue(m_queue.Get(), m_fence.Get(), m_fenceValue);
  cpuWait(m_fence.Get(), m_fenceValue, m_fenceEvent);
  state = ResourceState::CopyDst;
}

void DX12Device::flushUploads() {
  if (!m_copyOpen) {
    return;
  }
  {
    const HRESULT closeHr = m_copyList->Close();
    if (FAILED(closeHr)) {
      ComPtr<ID3D12InfoQueue> info;
      if (SUCCEEDED(m_device.As(&info))) {
        const UINT64 n = info->GetNumStoredMessages();
        const UINT64 start = n > 16 ? n - 16 : 0;
        for (UINT64 i = start; i < n; ++i) {
          SIZE_T len = 0;
          info->GetMessage(i, nullptr, &len);
          std::vector<char> bytes(len);
          auto* msg = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
          if (SUCCEEDED(info->GetMessage(i, msg, &len))) {
            std::cerr << "[D3D12] " << msg->pDescription << "\n";
          }
        }
      }
      throwIfFailed(closeHr, "Close copy list");
    }
  }
  ID3D12CommandList* lists[] = {m_copyList.Get()};
  m_copyQueue->ExecuteCommandLists(1, lists);
  signalQueue(m_copyQueue.Get(), m_copyFence.Get(), m_copyFenceValue);
  waitForCopyGpu();
  // Graphics must observe copy completion before using uploaded resources.
  queueWait(m_queue.Get(), m_copyFence.Get(), m_copyFenceValue);
  throwIfFailed(m_copyAllocator->Reset(), "Reset copy allocator");
  throwIfFailed(m_copyList->Reset(m_copyAllocator.Get(), nullptr), "Reset copy list");
  m_copyOpen = false;
  m_copyList->Close();
  m_upload[m_frameIndex].reset();
}

void DX12Device::uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset) {
  auto& buf = static_cast<DX12Buffer&>(buffer);
  if (buf.mappedPtr) {
    memcpy(static_cast<uint8_t*>(buf.mappedPtr) + offset, data, static_cast<size_t>(size));
    return;
  }

  // Prefer DIRECT queue for buffer uploads: avoids COPY-queue state quirks that have
  // caused stable page-fault TDRs on static mesh VB/IB after cross-queue sync.
  if (m_copyOpen) {
    flushUploads();
  }
  waitForGpu();

  D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  ID3D12Resource* uploadRes = nullptr;
  uint64_t uploadOffset = 0;
  uint8_t* dst = m_upload[m_frameIndex].allocate(size, 256, gpu, &uploadRes, uploadOffset);
  memcpy(dst, data, static_cast<size_t>(size));

  throwIfFailed(m_uploadDirectAllocator->Reset(), "Reset upload-direct allocator");
  throwIfFailed(m_uploadDirectList->Reset(m_uploadDirectAllocator.Get(), nullptr), "Reset upload-direct list");

  if (buf.state != ResourceState::CopyDst) {
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = buf.get();
    toCopy.Transition.StateBefore = toD3D(buf.state);
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_uploadDirectList->ResourceBarrier(1, &toCopy);
  }

  m_uploadDirectList->CopyBufferRegion(buf.get(), offset, uploadRes, uploadOffset, size);

  D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
  ResourceState afterCpu = ResourceState::Common;
  if (any(buf.usage, BufferUsage::Structured) || any(buf.usage, BufferUsage::UnorderedAccess)) {
    after = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    afterCpu = ResourceState::ShaderResource;
    if (any(buf.usage, BufferUsage::Index)) {
      after |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }
  } else if (any(buf.usage, BufferUsage::Vertex)) {
    after = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    afterCpu = ResourceState::VertexBuffer;
  } else if (any(buf.usage, BufferUsage::Index)) {
    after = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    afterCpu = ResourceState::IndexBuffer;
  }

  D3D12_RESOURCE_BARRIER done{};
  done.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  done.Transition.pResource = buf.get();
  done.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  done.Transition.StateAfter = after;
  done.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_uploadDirectList->ResourceBarrier(1, &done);
  buf.state = afterCpu;

  throwIfFailed(m_uploadDirectList->Close(), "Close upload-direct list");
  ID3D12CommandList* lists[] = {m_uploadDirectList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  signalQueue(m_queue.Get(), m_fence.Get(), m_fenceValue);
  cpuWait(m_fence.Get(), m_fenceValue, m_fenceEvent);
  m_upload[m_frameIndex].reset();
}

std::shared_ptr<Texture> DX12Device::createTexture(const TextureDesc& desc, const void* initialData,
                                                  uint32_t rowPitch) {
  auto tex = std::make_shared<DX12Texture>();
  tex->device = this;
  tex->desc = desc;
  tex->usage = desc.usage;

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  const bool is3D = desc.depth > 1;
  rd.Dimension = is3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = desc.width;
  rd.Height = desc.height;
  rd.DepthOrArraySize = static_cast<UINT16>(is3D ? desc.depth : (desc.isCube ? 6 : desc.arraySize));
  rd.MipLevels = static_cast<UINT16>(desc.mipLevels);
  rd.Format = toDxgi(desc.format);
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (any(desc.usage, TextureUsage::RenderTarget)) {
    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  if (any(desc.usage, TextureUsage::DepthStencil)) {
    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  }
  if (any(desc.usage, TextureUsage::UnorderedAccess)) {
    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  D3D12_CLEAR_VALUE clear{};
  D3D12_CLEAR_VALUE* clearPtr = nullptr;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  if (any(desc.usage, TextureUsage::DepthStencil)) {
    clear.Format = rd.Format;
    clear.DepthStencil.Depth = 1.0f;
    clearPtr = &clear;
    state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    tex->state = ResourceState::DepthWrite;
  } else if (any(desc.usage, TextureUsage::RenderTarget)) {
    clear.Format = rd.Format;
    clear.Color[0] = clear.Color[1] = clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;
    clearPtr = &clear;
    state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    tex->state = ResourceState::RenderTarget;
  } else {
    tex->state = ResourceState::Common;
  }

  throwIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, clearPtr,
                                                 IID_PPV_ARGS(&tex->resource)),
                "CreateTexture");

  if (any(desc.usage, TextureUsage::RenderTarget)) {
    createRtv(*tex);
  }
  if (any(desc.usage, TextureUsage::DepthStencil)) {
    createDsv(*tex);
  }
  if (any(desc.usage, TextureUsage::ShaderResource) || initialData) {
    // Depth formats need typeless SRV handling; skip SRV for pure depth for now unless also SRV
    if (!any(desc.usage, TextureUsage::DepthStencil) || any(desc.usage, TextureUsage::ShaderResource)) {
      if (desc.format != Format::D32_FLOAT && desc.format != Format::D24_UNORM_S8_UINT) {
        createSrv(*tex);
      }
    }
  }
  if (any(desc.usage, TextureUsage::UnorderedAccess)) {
    createUav(*tex);
  }

  if (initialData) {
    uploadTexture(*tex, initialData, desc.width, desc.height, rowPitch ? rowPitch : formatRowPitch(desc.format, desc.width),
                  0, 0);
  }

  if (!desc.debugName.empty()) {
    std::wstring w(desc.debugName.begin(), desc.debugName.end());
    tex->resource->SetName(w.c_str());
  }
  return tex;
}

void DX12Device::uploadTexture(Texture& texture, const void* data, uint32_t width, uint32_t height, uint32_t rowPitch,
                               uint32_t mip, uint32_t arraySlice) {
  (void)width; // extent comes from the resource footprint (2D rows / 3D slices)
  auto& tex = static_cast<DX12Texture&>(texture);
  transitionToCopyDestDirect(tex.get(), tex.state);

  if (!m_copyOpen) {
    throwIfFailed(m_copyAllocator->Reset(), "Reset copy allocator");
    throwIfFailed(m_copyList->Reset(m_copyAllocator.Get(), nullptr), "Reset copy list");
    m_copyOpen = true;
  }

  const UINT sub = calcSubresource(mip, arraySlice, 0, tex.desc.mipLevels, tex.desc.arraySize);
  D3D12_RESOURCE_DESC rd = tex.get()->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 requiredSize = 0;
  m_device->GetCopyableFootprints(&rd, sub, 1, 0, &footprint, &numRows, &rowSizeInBytes, &requiredSize);

  D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  ID3D12Resource* uploadRes = nullptr;
  uint64_t uploadOffset = 0;
  uint8_t* dst = m_upload[m_frameIndex].allocate(requiredSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, gpu,
                                                 &uploadRes, uploadOffset);
  const auto* src = static_cast<const uint8_t*>(data);
  const UINT rows = std::min(numRows, height);
  const UINT depthSlices = std::max<UINT>(1, footprint.Footprint.Depth);
  const UINT64 dstSlicePitch = UINT64(footprint.Footprint.RowPitch) * numRows;
  const UINT64 srcSlicePitch = UINT64(rowPitch) * height;
  for (UINT z = 0; z < depthSlices; ++z) {
    for (UINT y = 0; y < rows; ++y) {
      memcpy(dst + z * dstSlicePitch + y * footprint.Footprint.RowPitch, src + z * srcSlicePitch + y * rowPitch,
             static_cast<size_t>(std::min<UINT64>(rowSizeInBytes, rowPitch)));
    }
  }

  if (tex.state != ResourceState::CopyDst) {
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = tex.get();
    toCopy.Transition.StateBefore = toD3D(tex.state); // Common or CopySrc only
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_copyList->ResourceBarrier(1, &toCopy);
    tex.state = ResourceState::CopyDst;
  }

  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = tex.get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = sub;

  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = uploadRes;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;
  srcLoc.PlacedFootprint.Offset = uploadOffset;

  m_copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER toCommon{};
  toCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCommon.Transition.pResource = tex.get();
  toCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  toCommon.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_copyList->ResourceBarrier(1, &toCommon);
  tex.state = ResourceState::Common;
  flushUploads();
}

uint32_t DX12Device::writeSrvTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count) {
  const uint32_t base = m_srvHeap.allocate(count);
  for (uint32_t i = 0; i < count; ++i) {
    m_device->CopyDescriptorsSimple(1, m_srvHeap.cpuHandle(base + i), srcCpu[i],
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
  // Recycle after GPU finishes this frame's work.
  m_srvHeap.deferFree(base, count, m_fenceValue + 1);
  return base;
}

uint32_t DX12Device::writeUavTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count) {
  return writeSrvTable(srcCpu, count);
}

ID3D12CommandSignature* DX12Device::drawIndexedIndirectSig() {
  if (!m_drawIndexedIndirectSig) {
    D3D12_INDIRECT_ARGUMENT_DESC arg{};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &arg;
    throwIfFailed(m_device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&m_drawIndexedIndirectSig)),
                  "CreateCommandSignature");
  }
  return m_drawIndexedIndirectSig.Get();
}

uint32_t DX12Device::writeSamplerTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count) {
  // Root sig sampler tables declare 8 slots; pad so STATIC/VOLATILE validation never sees holes.
  constexpr uint32_t kSamplerTableSlots = 8;
  const uint32_t n = count == 0 ? 1 : count;
  const uint32_t alloc = kSamplerTableSlots;
  const uint32_t base = m_samplerHeap.allocate(alloc);
  for (uint32_t i = 0; i < alloc; ++i) {
    const uint32_t src = i < n ? i : (n - 1);
    m_device->CopyDescriptorsSimple(1, m_samplerHeap.cpuHandle(base + i), srcCpu[src],
                                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
  m_samplerHeap.deferFree(base, alloc, m_fenceValue + 1);
  return base;
}

std::shared_ptr<Sampler> DX12Device::createSampler(const SamplerDesc& desc) {
  auto s = std::make_shared<DX12Sampler>();
  s->index = m_samplerHeap.allocateIndex();
  s->cpu = m_samplerHeap.cpuHandle(s->index);
  s->gpu = m_samplerHeap.gpuHandle(s->index);

  D3D12_SAMPLER_DESC sd{};
  sd.Filter = toD3D(desc.filter, desc.comparison);
  sd.AddressU = toD3D(desc.addressU);
  sd.AddressV = toD3D(desc.addressV);
  sd.AddressW = toD3D(desc.addressW);
  sd.MaxAnisotropy = static_cast<UINT>(desc.maxAnisotropy);
  sd.ComparisonFunc = toD3D(desc.compare);
  sd.MinLOD = 0.0f;
  sd.MaxLOD = D3D12_FLOAT32_MAX;
  m_device->CreateSampler(&sd, s->cpu);
  return s;
}

std::shared_ptr<Buffer> DX12Device::createAccelerationStructureBuffer(uint64_t sizeBytes,
                                                                      const std::string& debugName) {
  if (!supportsRaytracing() || sizeBytes == 0) {
    return nullptr;
  }
  BufferDesc desc{};
  desc.size = (sizeBytes + 255ull) & ~255ull;
  desc.usage = BufferUsage::UnorderedAccess | BufferUsage::AccelerationStructure;
  desc.debugName = debugName.empty() ? "ASBuffer" : debugName;
  return createBuffer(desc, nullptr);
}

uint32_t DX12Device::createAccelerationStructureSrv(Buffer& asBuffer) {
  if (!supportsRaytracing()) {
    return ~0u;
  }
  auto& buf = static_cast<DX12Buffer&>(asBuffer);
  if (buf.hasSrv) {
    return buf.srvHeapIndex;
  }
  buf.srvHeapIndex = m_bindless.allocate(1);
  if (buf.srvHeapIndex == UINT32_MAX) {
    return ~0u;
  }
  buf.srvCpu = m_srvHeap.cpuHandle(buf.srvHeapIndex);
  buf.srvGpu = m_srvHeap.gpuHandle(buf.srvHeapIndex);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.RaytracingAccelerationStructure.Location = buf.gpuAddress;
  m_device->CreateShaderResourceView(nullptr, &srv, buf.srvCpu);
  buf.hasSrv = true;
  return buf.srvHeapIndex;
}

void DX12Device::getRaytracingPrebuildInfo(uint32_t triangleCount, uint32_t vertexCount, uint64_t* resultSize,
                                           uint64_t* scratchSize) {
  if (resultSize) {
    *resultSize = 0;
  }
  if (scratchSize) {
    *scratchSize = 0;
  }
  if (!m_device5 || triangleCount == 0 || vertexCount == 0) {
    return;
  }
  D3D12_RAYTRACING_GEOMETRY_DESC geom{};
  geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
  geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geom.Triangles.IndexCount = triangleCount * 3;
  geom.Triangles.VertexCount = vertexCount;
  geom.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 4;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = 1;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.pGeometryDescs = &geom;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
  m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
  if (resultSize) {
    *resultSize = info.ResultDataMaxSizeInBytes;
  }
  if (scratchSize) {
    *scratchSize = info.ScratchDataSizeInBytes;
  }
}

void DX12Device::getRaytracingTopLevelPrebuildInfo(uint32_t instanceCount, uint64_t* resultSize,
                                                   uint64_t* scratchSize) {
  if (resultSize) {
    *resultSize = 0;
  }
  if (scratchSize) {
    *scratchSize = 0;
  }
  if (!m_device5 || instanceCount == 0) {
    return;
  }
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
  inputs.NumDescs = instanceCount;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
  m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
  if (resultSize) {
    *resultSize = info.ResultDataMaxSizeInBytes;
  }
  if (scratchSize) {
    *scratchSize = info.ScratchDataSizeInBytes;
  }
}

DX12Buffer::~DX12Buffer() {
  if (!device || !device->device()) {
    device = nullptr;
    return;
  }
  // Replace live SRV/UAV with null descriptors before the resource is released — otherwise
  // bindless heap entries keep pointing at freed GPU VAs (page-fault TDR).
  D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
  nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  nullSrv.Texture2D.MipLevels = 1;
  if (hasSrv && srvHeapIndex != UINT32_MAX) {
    device->device()->CreateShaderResourceView(nullptr, &nullSrv, srvCpu);
    device->srvHeap().deferFree(srvHeapIndex, 1, device->frameFenceValue() + 1);
  }
  if (hasUav && uavHeapIndex != UINT32_MAX) {
    device->device()->CreateShaderResourceView(nullptr, &nullSrv, uavCpu);
    device->srvHeap().deferFree(uavHeapIndex, 1, device->frameFenceValue() + 1);
  }
  hasSrv = false;
  hasUav = false;
  device = nullptr;
}

DX12Texture::~DX12Texture() {
  if (!device || !device->device()) {
    device = nullptr;
    return;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
  nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  nullSrv.Texture2D.MipLevels = 1;
  if (hasSrv && srvIndex != UINT32_MAX) {
    device->device()->CreateShaderResourceView(nullptr, &nullSrv, srvCpu);
    device->unregisterBindlessTexture(srvIndex);
  }
  if (hasUav && uavIndex != UINT32_MAX) {
    device->device()->CreateShaderResourceView(nullptr, &nullSrv, uavCpu);
    device->unregisterBindlessTexture(uavIndex);
  }
  hasSrv = false;
  hasUav = false;
  device = nullptr;
}

} // namespace tucano::rhi
