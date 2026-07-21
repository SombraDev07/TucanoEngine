#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Device.h"

namespace tucano::rhi {

DX12CommandList::DX12CommandList(DX12Device* device, ID3D12CommandAllocator* allocator) : m_device(device) {
  throwIfFailed(device->device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                   IID_PPV_ARGS(&m_cmd)),
                "CreateCommandList");
  m_cmd->Close();
}

void DX12CommandList::reset(ID3D12CommandAllocator* allocator) {
  throwIfFailed(allocator->Reset(), "Allocator Reset");
  throwIfFailed(m_cmd->Reset(allocator, nullptr), "CommandList Reset");
  m_barriers.reset();
}

void DX12CommandList::flushBarriers() { m_barriers.flush(m_cmd.Get()); }

void DX12CommandList::close() {
  flushBarriers();
  throwIfFailed(m_cmd->Close(), "CommandList Close");
}

void DX12CommandList::transition(Texture& texture, ResourceState state) {
  auto& tex = static_cast<DX12Texture&>(texture);
  if (tex.state == state) {
    return;
  }
  m_barriers.transition(tex.get(), toD3D(tex.state), toD3D(state));
  tex.state = state;
}

void DX12CommandList::transition(Buffer& buffer, ResourceState state) {
  auto& buf = static_cast<DX12Buffer&>(buffer);
  if (buf.state == state || any(buf.usage, BufferUsage::Upload)) {
    buf.state = state;
    return;
  }
  m_barriers.transition(buf.get(), toD3D(buf.state), toD3D(state));
  buf.state = state;
}

void DX12CommandList::setPipeline(PipelineState& pso) {
  m_barriers.flush(m_cmd.Get());
  auto& dx = static_cast<DX12PipelineState&>(pso);
  m_cmd->SetPipelineState(dx.pso.Get());
}

void DX12CommandList::setRootSignature(RootSignature& rs) {
  auto& dx = static_cast<DX12RootSignature&>(rs);
  if (dx.isCompute) {
    m_cmd->SetComputeRootSignature(dx.rs.Get());
  } else {
    m_cmd->SetGraphicsRootSignature(dx.rs.Get());
  }
}

void DX12CommandList::setViewport(const Viewport& vp) {
  D3D12_VIEWPORT v{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
  m_cmd->RSSetViewports(1, &v);
}

void DX12CommandList::setScissor(const Scissor& sc) {
  D3D12_RECT r{sc.left, sc.top, sc.right, sc.bottom};
  m_cmd->RSSetScissorRects(1, &r);
}

void DX12CommandList::setRenderTargets(std::span<Texture*> rtvs, Texture* dsv) {
  D3D12_CPU_DESCRIPTOR_HANDLE handles[8]{};
  UINT count = 0;
  for (size_t i = 0; i < rtvs.size() && i < 8; ++i) {
    auto* tex = static_cast<DX12Texture*>(rtvs[i]);
    if (!tex || !tex->hasRtv) {
      throw std::runtime_error("setRenderTargets: missing RTV");
    }
    handles[i] = tex->rtv;
    count = static_cast<UINT>(i + 1);
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu{};
  if (dsv) {
    auto* depth = static_cast<DX12Texture*>(dsv);
    if (!depth->hasDsv) {
      throw std::runtime_error("setRenderTargets: missing DSV");
    }
    dsvCpu = depth->dsv;
    dsvHandle = &dsvCpu;
  }
  m_cmd->OMSetRenderTargets(count, handles, FALSE, dsvHandle);
}

void DX12CommandList::clearRenderTarget(Texture& rtv, const float color[4]) {
  auto& tex = static_cast<DX12Texture&>(rtv);
  m_cmd->ClearRenderTargetView(tex.rtv, color, 0, nullptr);
}

void DX12CommandList::clearDepth(Texture& dsv, float depth) {
  auto& tex = static_cast<DX12Texture&>(dsv);
  m_cmd->ClearDepthStencilView(tex.dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void DX12CommandList::setVertexBuffer(Buffer& vb, uint32_t stride) {
  auto& buf = static_cast<DX12Buffer&>(vb);
  D3D12_VERTEX_BUFFER_VIEW view{};
  view.BufferLocation = buf.gpuAddress;
  view.SizeInBytes = static_cast<UINT>(buf.byteSize);
  view.StrideInBytes = stride;
  m_cmd->IASetVertexBuffers(0, 1, &view);
}

void DX12CommandList::setIndexBuffer(Buffer& ib, bool index32) {
  auto& buf = static_cast<DX12Buffer&>(ib);
  D3D12_INDEX_BUFFER_VIEW view{};
  view.BufferLocation = buf.gpuAddress;
  view.SizeInBytes = static_cast<UINT>(buf.byteSize);
  view.Format = index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
  m_cmd->IASetIndexBuffer(&view);
}

void DX12CommandList::setPrimitiveTopology(PrimitiveTopology topology) {
  m_cmd->IASetPrimitiveTopology(toD3D(topology));
}

void DX12CommandList::setGraphicsRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) {
  auto& buf = static_cast<DX12Buffer&>(buffer);
  m_cmd->SetGraphicsRootConstantBufferView(rootIndex, buf.gpuAddress + offset);
}

void DX12CommandList::setGraphicsRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) {
  m_cmd->SetGraphicsRoot32BitConstants(rootIndex, num32BitValues, data, 0);
}

void DX12CommandList::setGraphicsRootSRV(uint32_t rootIndex, Texture& texture) {
  auto& tex = static_cast<DX12Texture&>(texture);
  m_cmd->SetGraphicsRootDescriptorTable(rootIndex, tex.srvGpu);
}

void DX12CommandList::setGraphicsRootSampler(uint32_t rootIndex, Sampler& sampler) {
  auto& s = static_cast<DX12Sampler&>(sampler);
  m_cmd->SetGraphicsRootDescriptorTable(rootIndex, s.gpu);
}

void DX12CommandList::setGraphicsRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) {
  m_cmd->SetGraphicsRootDescriptorTable(rootIndex, m_device->srvGpuAt(heapIndex));
}

void DX12CommandList::setGraphicsRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) {
  m_cmd->SetGraphicsRootDescriptorTable(rootIndex, m_device->samplerGpuAt(heapIndex));
}

void DX12CommandList::setComputeRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) {
  auto& buf = static_cast<DX12Buffer&>(buffer);
  m_cmd->SetComputeRootConstantBufferView(rootIndex, buf.gpuAddress + offset);
}

void DX12CommandList::setComputeRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) {
  m_cmd->SetComputeRoot32BitConstants(rootIndex, num32BitValues, data, 0);
}

void DX12CommandList::setComputeRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) {
  m_cmd->SetComputeRootDescriptorTable(rootIndex, m_device->srvGpuAt(heapIndex));
}

void DX12CommandList::setComputeRootUavTable(uint32_t rootIndex, uint32_t heapIndex) {
  m_cmd->SetComputeRootDescriptorTable(rootIndex, m_device->srvGpuAt(heapIndex));
}

void DX12CommandList::setComputeRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) {
  m_cmd->SetComputeRootDescriptorTable(rootIndex, m_device->samplerGpuAt(heapIndex));
}

void DX12CommandList::setDescriptorHeap() {
  ID3D12DescriptorHeap* heaps[] = {m_device->srvHeap().get(), m_device->samplerHeap().get()};
  m_cmd->SetDescriptorHeaps(2, heaps);
}

void DX12CommandList::draw(uint32_t vertexCount, uint32_t startVertex) {
  m_barriers.flush(m_cmd.Get());
  m_cmd->DrawInstanced(vertexCount, 1, startVertex, 0);
}

void DX12CommandList::drawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) {
  m_barriers.flush(m_cmd.Get());
  m_cmd->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
}

void DX12CommandList::drawIndexedIndirect(Buffer& args, uint64_t offset) {
  m_barriers.flush(m_cmd.Get());
  auto& buf = static_cast<DX12Buffer&>(args);
  m_cmd->ExecuteIndirect(m_device->drawIndexedIndirectSig(), 1, buf.get(), offset, nullptr, 0);
}

void DX12CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
  m_barriers.flush(m_cmd.Get());
  m_cmd->Dispatch(x, y, z);
}

void DX12CommandList::copyTextureToBuffer(Texture& src, Buffer& dst, uint32_t width, uint32_t height, Format format) {
  auto& tex = static_cast<DX12Texture&>(src);
  auto& buf = static_cast<DX12Buffer&>(dst);
  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = tex.get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = buf.get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint.Offset = 0;
  dstLoc.PlacedFootprint.Footprint.Format = toDxgi(format);
  dstLoc.PlacedFootprint.Footprint.Width = width;
  dstLoc.PlacedFootprint.Footprint.Height = height;
  dstLoc.PlacedFootprint.Footprint.Depth = 1;
  dstLoc.PlacedFootprint.Footprint.RowPitch = (formatRowPitch(format, width) + 255u) & ~255u;

  m_barriers.flush(m_cmd.Get());
  m_cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
}

void DX12CommandList::copyTextureRegion(Texture& dst, uint32_t dstX, uint32_t dstY, Texture& src, uint32_t srcX,
                                        uint32_t srcY, uint32_t width, uint32_t height) {
  auto& d = static_cast<DX12Texture&>(dst);
  auto& s = static_cast<DX12Texture&>(src);
  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = s.get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = d.get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = 0;
  D3D12_BOX box{};
  box.left = srcX;
  box.top = srcY;
  box.front = 0;
  box.right = srcX + width;
  box.bottom = srcY + height;
  box.back = 1;
  m_barriers.flush(m_cmd.Get());
  m_cmd->CopyTextureRegion(&dstLoc, dstX, dstY, 0, &srcLoc, &box);
}

} // namespace tucano::rhi
