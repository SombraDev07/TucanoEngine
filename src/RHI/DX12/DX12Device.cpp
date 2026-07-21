#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12SwapChain.h"

#include <d3d12sdklayers.h>

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
  waitIdle();
  if (m_pipelineCache) {
    m_pipelineCache->save();
  }
  if (m_fenceEvent) {
    CloseHandle(m_fenceEvent);
  }
}

void DX12Device::createDevice(bool enableDebug) {
  UINT factoryFlags = 0;

  // DRED must be configured before device creation for best results.
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

  throwIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)))) {
      break;
    }
    m_device.Reset();
  }
  if (!m_device) {
    throwIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice");
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

  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  throwIfFailed(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue");

  // Async compute + copy queues (Dagor multi-queue model; graphics remains primary submit path).
  D3D12_COMMAND_QUEUE_DESC cq{};
  cq.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  throwIfFailed(m_device->CreateCommandQueue(&cq, IID_PPV_ARGS(&m_computeQueue)), "CreateComputeQueue");
  cq.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  throwIfFailed(m_device->CreateCommandQueue(&cq, IID_PPV_ARGS(&m_copyQueue)), "CreateCopyQueue");

  throwIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");
  m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!m_fenceEvent) {
    throw std::runtime_error("CreateEvent failed");
  }

  m_srvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16384, true);
  m_rtvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2048, false);
  m_dsvHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256, false);
  m_samplerHeap.init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 512, true);
}

void DX12Device::createFrameResources() {
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    throwIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i])),
                  "CreateCommandAllocator");
    m_cmdLists[i] = std::make_unique<DX12CommandList>(this, m_allocators[i].Get());
    m_upload[i].init(m_device.Get(), 512ull * 1024ull * 1024ull);
    m_fenceValues[i] = 0;
  }
  throwIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_copyAllocator)),
                "Create copy allocator");
  throwIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_copyAllocator.Get(), nullptr,
                                          IID_PPV_ARGS(&m_copyList)),
                "Create copy list");
  m_copyList->Close();
  m_copyOpen = false;
}

void DX12Device::waitForGpu() {
  const uint64_t fence = ++m_fenceValue;
  throwIfFailed(m_queue->Signal(m_fence.Get(), fence), "Signal");
  if (m_fence->GetCompletedValue() < fence) {
    throwIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent), "SetEventOnCompletion");
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
}

void DX12Device::waitIdle() { waitForGpu(); }

CommandList* DX12Device::beginFrame() {
  const uint64_t completed = m_fence->GetCompletedValue();
  if (m_fenceValues[m_frameIndex] != 0 && completed < m_fenceValues[m_frameIndex]) {
    throwIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "SetEventOnCompletion");
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
  m_upload[m_frameIndex].reset();
  m_cmdLists[m_frameIndex]->reset(m_allocators[m_frameIndex].Get());
  return m_cmdLists[m_frameIndex].get();
}

void DX12Device::endFrame(SwapChain& swapChain) {
  m_cmdLists[m_frameIndex]->close();
  ID3D12CommandList* lists[] = {m_cmdLists[m_frameIndex]->get()};
  m_queue->ExecuteCommandLists(1, lists);
  swapChain.present();
  const uint64_t fence = ++m_fenceValue;
  throwIfFailed(m_queue->Signal(m_fence.Get(), fence), "Signal");
  m_fenceValues[m_frameIndex] = fence;
  m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
  m_bindless.flushDeferredFrees();
}

std::unique_ptr<SwapChain> DX12Device::createSwapChain(void* hwnd, uint32_t width, uint32_t height, bool vsync) {
  return std::make_unique<DX12SwapChain>(this, hwnd, width, height, vsync);
}

void DX12Device::createSrv(DX12Texture& tex) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = toDxgi(tex.desc.format);
  if (tex.desc.isCube) {
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
  m_bindless.deferFree(slot, 1);
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
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uav.Texture2D.MipSlice = 0;
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
  buf->byteSize = desc.size;
  buf->usage = desc.usage;

  const bool upload = any(desc.usage, BufferUsage::Upload) || initialData != nullptr;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = upload && any(desc.usage, BufferUsage::Upload) ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
  if (any(desc.usage, BufferUsage::Constant) && !initialData) {
    // dynamic CB: upload heap
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  }

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = desc.size;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  if (heap.Type == D3D12_HEAP_TYPE_UPLOAD) {
    state = D3D12_RESOURCE_STATE_GENERIC_READ;
    buf->state = ResourceState::ConstantBuffer;
  }

  throwIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                                 IID_PPV_ARGS(&buf->resource)),
                "CreateBuffer");
  buf->gpuAddress = buf->resource->GetGPUVirtualAddress();

  if (heap.Type == D3D12_HEAP_TYPE_UPLOAD) {
    throwIfFailed(buf->resource->Map(0, nullptr, &buf->mappedPtr), "Map buffer");
    if (initialData) {
      memcpy(buf->mappedPtr, initialData, static_cast<size_t>(desc.size));
    }
  } else if (initialData) {
    uploadBuffer(*buf, initialData, desc.size, 0);
  }

  if (!desc.debugName.empty()) {
    std::wstring w(desc.debugName.begin(), desc.debugName.end());
    buf->resource->SetName(w.c_str());
  }
  return buf;
}

void DX12Device::flushUploads() {
  if (!m_copyOpen) {
    return;
  }
  throwIfFailed(m_copyList->Close(), "Close copy list");
  ID3D12CommandList* lists[] = {m_copyList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  waitForGpu();
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

  if (!m_copyOpen) {
    throwIfFailed(m_copyAllocator->Reset(), "Reset copy allocator");
    throwIfFailed(m_copyList->Reset(m_copyAllocator.Get(), nullptr), "Reset copy list");
    m_copyOpen = true;
  }

  D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  ID3D12Resource* uploadRes = nullptr;
  uint64_t uploadOffset = 0;
  uint8_t* dst = m_upload[m_frameIndex].allocate(size, 256, gpu, &uploadRes, uploadOffset);
  memcpy(dst, data, static_cast<size_t>(size));

  D3D12_RESOURCE_BARRIER toCopy{};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = buf.get();
  toCopy.Transition.StateBefore = toD3D(buf.state);
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  if (buf.state != ResourceState::CopyDst) {
    m_copyList->ResourceBarrier(1, &toCopy);
  }

  m_copyList->CopyBufferRegion(buf.get(), offset, uploadRes, uploadOffset, size);

  D3D12_RESOURCE_BARRIER toSrv{};
  toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toSrv.Transition.pResource = buf.get();
  toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
  toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_copyList->ResourceBarrier(1, &toSrv);
  buf.state = ResourceState::VertexBuffer;
  flushUploads();
}

std::shared_ptr<Texture> DX12Device::createTexture(const TextureDesc& desc, const void* initialData,
                                                  uint32_t rowPitch) {
  auto tex = std::make_shared<DX12Texture>();
  tex->desc = desc;
  tex->usage = desc.usage;

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = desc.width;
  rd.Height = desc.height;
  rd.DepthOrArraySize = static_cast<UINT16>(desc.isCube ? 6 : desc.arraySize);
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
  auto& tex = static_cast<DX12Texture&>(texture);
  if (!m_copyOpen) {
    throwIfFailed(m_copyAllocator->Reset(), "Reset copy allocator");
    throwIfFailed(m_copyList->Reset(m_copyAllocator.Get(), nullptr), "Reset copy list");
    m_copyOpen = true;
  }

  const uint32_t alignedPitch = (rowPitch + 255u) & ~255u;
  const uint64_t total = static_cast<uint64_t>(alignedPitch) * height;
  D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
  ID3D12Resource* uploadRes = nullptr;
  uint64_t uploadOffset = 0;
  uint8_t* dst = m_upload[m_frameIndex].allocate(total, 512, gpu, &uploadRes, uploadOffset);
  const auto* src = static_cast<const uint8_t*>(data);
  for (uint32_t y = 0; y < height; ++y) {
    memcpy(dst + y * alignedPitch, src + y * rowPitch, rowPitch);
  }

  if (tex.state != ResourceState::CopyDst) {
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = tex.get();
    toCopy.Transition.StateBefore = toD3D(tex.state);
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_copyList->ResourceBarrier(1, &toCopy);
    tex.state = ResourceState::CopyDst;
  }

  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = tex.get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = calcSubresource(mip, arraySlice, 0, tex.desc.mipLevels, tex.desc.arraySize);

  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = uploadRes;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint.Offset = uploadOffset;
  srcLoc.PlacedFootprint.Footprint.Format = toDxgi(tex.desc.format);
  srcLoc.PlacedFootprint.Footprint.Width = width;
  srcLoc.PlacedFootprint.Footprint.Height = height;
  srcLoc.PlacedFootprint.Footprint.Depth = 1;
  srcLoc.PlacedFootprint.Footprint.RowPitch = alignedPitch;

  m_copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER toSrv{};
  toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toSrv.Transition.pResource = tex.get();
  toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_copyList->ResourceBarrier(1, &toSrv);
  tex.state = ResourceState::ShaderResource;
  flushUploads();
}

uint32_t DX12Device::writeSrvTable(const D3D12_CPU_DESCRIPTOR_HANDLE* srcCpu, uint32_t count) {
  // Transient copy region lives above bindless pool.
  constexpr uint32_t kTableBegin = 8192;
  constexpr uint32_t kTableEnd = 16384;
  if (m_srvTableCursor < kTableBegin || m_srvTableCursor + count >= kTableEnd) {
    m_srvTableCursor = kTableBegin;
  }
  const uint32_t base = m_srvTableCursor;
  m_srvTableCursor += count;
  for (uint32_t i = 0; i < count; ++i) {
    m_device->CopyDescriptorsSimple(1, m_srvHeap.cpuHandle(base + i), srcCpu[i],
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
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
  constexpr uint32_t kTableBegin = 64;
  constexpr uint32_t kTableEnd = 512;
  if (m_samplerTableCursor < kTableBegin || m_samplerTableCursor + count >= kTableEnd) {
    m_samplerTableCursor = kTableBegin;
  }
  const uint32_t base = m_samplerTableCursor;
  m_samplerTableCursor += count;
  for (uint32_t i = 0; i < count; ++i) {
    m_device->CopyDescriptorsSimple(1, m_samplerHeap.cpuHandle(base + i), srcCpu[i],
                                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }
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

} // namespace tucano::rhi
