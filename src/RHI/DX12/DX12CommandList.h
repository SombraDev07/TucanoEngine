#pragma once

#include "RHI/DX12/DX12Resource.h"
#include "RHI/DX12/BarrierBatcher.h"

namespace tucano::rhi {

class DX12Device;

class DX12CommandList final : public CommandList {
public:
  DX12CommandList(DX12Device* device, ID3D12CommandAllocator* allocator,
                  D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

  void reset(ID3D12CommandAllocator* allocator);
  void close();
  ID3D12GraphicsCommandList* get() const { return m_cmd.Get(); }
  D3D12_COMMAND_LIST_TYPE type() const { return m_type; }
  BarrierBatcher& barriers() { return m_barriers; }

  void transition(Texture& texture, ResourceState state) override;
  void transition(Texture& texture, ResourceState state, uint32_t mip, uint32_t arraySlice) override;
  void transition(Buffer& buffer, ResourceState state) override;
  void setPipeline(PipelineState& pso) override;
  void setRootSignature(RootSignature& rs) override;
  void setViewport(const Viewport& vp) override;
  void setScissor(const Scissor& sc) override;
  void setRenderTargets(std::span<Texture*> rtvs, Texture* dsv) override;
  void clearRenderTarget(Texture& rtv, const float color[4]) override;
  void clearRenderTargetRect(Texture& rtv, const float color[4], uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h) override;
  void clearDepth(Texture& dsv, float depth) override;
  void setVertexBuffer(Buffer& vb, uint32_t stride) override;
  void setIndexBuffer(Buffer& ib, bool index32) override;
  void setPrimitiveTopology(PrimitiveTopology topology) override;
  void setGraphicsRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) override;
  void setGraphicsRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) override;
  void setGraphicsRootSRV(uint32_t rootIndex, Texture& texture) override;
  void setGraphicsRootSampler(uint32_t rootIndex, Sampler& sampler) override;
  void setGraphicsRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) override;
  void setGraphicsRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) override;
  void setComputeRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) override;
  void setComputeRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) override;
  void setComputeRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) override;
  void setComputeRootUavTable(uint32_t rootIndex, uint32_t heapIndex) override;
  void setComputeRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) override;
  void setDescriptorHeap() override;
  void draw(uint32_t vertexCount, uint32_t startVertex) override;
  void drawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override;
  void drawIndexedIndirect(Buffer& args, uint64_t offset) override;
  void drawIndexedIndirectCount(Buffer& args, uint64_t argsOffset, Buffer& count, uint64_t countOffset,
                                uint32_t maxCount) override;
  void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
  void dispatchMesh(uint32_t x, uint32_t y, uint32_t z) override;
  void flushBarriers() override;
  void uavBarrier(Texture* resource) override;
  void aliasingBarrier(Texture* before, Texture* after) override;
  void copyTextureToBuffer(Texture& src, Buffer& dst, uint32_t width, uint32_t height, Format format) override;
  void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset,
                  uint64_t size) override;
  void copyBufferToTexture(Buffer& src, Texture& dst, uint32_t width, uint32_t height, uint32_t depth,
                           Format format) override;
  void copyTextureRegion(Texture& dst, uint32_t dstX, uint32_t dstY, Texture& src, uint32_t srcX, uint32_t srcY,
                         uint32_t width, uint32_t height) override;

  void buildBottomLevelAS(Buffer& dest, Buffer& scratch, const BlasTriangleGeometry& geo) override;
  void buildTopLevelAS(Buffer& dest, Buffer& scratch, Buffer& instanceDescs, uint32_t instanceCount) override;
  void setComputeRootAccelerationStructure(uint32_t rootIndex, Buffer& asBuffer) override;

private:
  DX12Device* m_device = nullptr;
  D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12GraphicsCommandList> m_cmd;
  BarrierBatcher m_barriers;
};

} // namespace tucano::rhi
