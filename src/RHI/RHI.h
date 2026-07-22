#pragma once

#include "RHI/RHITypes.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tucano::rhi {

struct Buffer;
struct Texture;
struct Sampler;
struct PipelineState;
struct RootSignature;
struct ShaderBytecode;

class CommandList;
class SwapChain;
class Device;

struct ShaderBytecode {
  std::vector<uint8_t> data;
  static ShaderBytecode loadFromFile(const std::string& path);
};

struct GraphicsPipelineDesc {
  std::shared_ptr<RootSignature> rootSignature;
  ShaderBytecode vs;
  ShaderBytecode ps;
  ShaderBytecode as; // amplification (mesh pipeline)
  ShaderBytecode ms; // mesh shader
  bool useMeshPipeline = false;
  std::vector<Format> rtvFormats;
  Format dsvFormat = Format::Unknown;
  CullMode cullMode = CullMode::Back;
  bool depthEnable = true;
  bool depthWrite = true;
  CompareOp depthFunc = CompareOp::Less;
  BlendMode blend = BlendMode::Opaque;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  uint32_t sampleCount = 1;
  bool wireframe = false;
  bool useInputLayout = true;
};

struct ComputePipelineDesc {
  std::shared_ptr<RootSignature> rootSignature;
  ShaderBytecode cs;
};

class Device {
public:
  virtual ~Device() = default;

  static std::unique_ptr<Device> create(bool enableDebugLayer = true);

  virtual std::unique_ptr<SwapChain> createSwapChain(void* hwnd, uint32_t width, uint32_t height,
                                                     bool vsync = true) = 0;
  virtual std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
  virtual std::shared_ptr<Texture> createTexture(const TextureDesc& desc, const void* initialData = nullptr,
                                                 uint32_t rowPitch = 0) = 0;
  virtual std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc = {}) = 0;
  virtual std::shared_ptr<RootSignature> createRootSignature(bool allowInputLayout = true) = 0;
  virtual std::shared_ptr<RootSignature> createComputeRootSignature() = 0;
  virtual std::shared_ptr<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
  virtual std::shared_ptr<PipelineState> createComputePipeline(const ComputePipelineDesc& desc) = 0;

  virtual CommandList* beginFrame() = 0;
  virtual void endFrame(SwapChain& swapChain) = 0;
  virtual void waitIdle() = 0;
  virtual uint32_t frameIndex() const = 0;
  virtual uint64_t frameFenceValue() const = 0;

  virtual void uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset = 0) = 0;
  virtual void uploadTexture(Texture& texture, const void* data, uint32_t width, uint32_t height,
                             uint32_t rowPitch, uint32_t mip = 0, uint32_t arraySlice = 0) = 0;

  virtual void* nativeDevice() const = 0;

  // Device-removed / TDR recovery.
  virtual bool isDeviceLost() const { return false; }
  virtual bool recoverFromDeviceLost() { return false; }
  virtual void setDeviceLostCallback(std::function<void()> cb) { (void)cb; }

  // Multi-queue async compute (DX12). Defaults no-op for future backends.
  virtual CommandList* beginAsyncCompute() { return nullptr; }
  virtual void submitAsyncCompute() {}
  virtual void graphicsWaitAsyncCompute() {}
  virtual void asyncComputeWaitGraphics() {}
  // Mid-frame: close+execute+signal early graphics list; returns late list for continued recording.
  virtual CommandList* submitGraphicsCheckpoint() { return nullptr; }

  virtual void beginGpuMarker(CommandList& /*cmd*/, const char* /*name*/) {}
  virtual void endGpuMarker(CommandList& /*cmd*/) {}
  virtual std::string dumpGpuCrashInfo() { return {}; }
  virtual bool supportsMeshShaders() const { return false; }
  virtual bool supportsRaytracing() const { return false; }

  // DXR acceleration-structure buffer (UAV + AS state). Returns null if RT unsupported.
  virtual std::shared_ptr<Buffer> createAccelerationStructureBuffer(uint64_t sizeBytes,
                                                                    const std::string& debugName = {}) {
    (void)sizeBytes;
    (void)debugName;
    return nullptr;
  }
  // Bindless AS SRV (D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE). Returns slot or ~0u.
  virtual uint32_t createAccelerationStructureSrv(Buffer& asBuffer) {
    (void)asBuffer;
    return ~0u;
  }
  virtual void getRaytracingPrebuildInfo(uint32_t /*triangleCount*/, uint32_t /*vertexCount*/,
                                         uint64_t* resultSize, uint64_t* scratchSize) {
    if (resultSize) {
      *resultSize = 0;
    }
    if (scratchSize) {
      *scratchSize = 0;
    }
  }
  virtual void getRaytracingTopLevelPrebuildInfo(uint32_t /*instanceCount*/, uint64_t* resultSize,
                                                 uint64_t* scratchSize) {
    if (resultSize) {
      *resultSize = 0;
    }
    if (scratchSize) {
      *scratchSize = 0;
    }
  }
};

struct BlasTriangleGeometry {
  Buffer* vertexBuffer = nullptr; // float3 or float4 positions
  uint32_t vertexCount = 0;
  uint32_t vertexStride = 16; // bytes
  Buffer* indexBuffer = nullptr; // uint32 indices
  uint32_t indexCount = 0;
};

struct TlasInstance {
  Buffer* blas = nullptr;
  float transform[3][4]{}; // row-major 3x4
  uint32_t instanceId = 0;
  uint8_t mask = 0xFF;
};

class SwapChain {
public:
  virtual ~SwapChain() = default;
  virtual void resize(uint32_t width, uint32_t height) = 0;
  virtual Texture& backBuffer() = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  virtual void present() = 0;
};

class CommandList {
public:
  virtual ~CommandList() = default;

  virtual void transition(Texture& texture, ResourceState state) = 0;
  virtual void transition(Texture& texture, ResourceState state, uint32_t mip, uint32_t arraySlice) {
    (void)mip;
    (void)arraySlice;
    transition(texture, state);
  }
  virtual void transition(Buffer& buffer, ResourceState state) = 0;
  virtual void setPipeline(PipelineState& pso) = 0;
  virtual void setRootSignature(RootSignature& rs) = 0;
  virtual void setViewport(const Viewport& vp) = 0;
  virtual void setScissor(const Scissor& sc) = 0;
  virtual void setRenderTargets(std::span<Texture*> rtvs, Texture* dsv) = 0;
  virtual void clearRenderTarget(Texture& rtv, const float color[4]) = 0;
  virtual void clearDepth(Texture& dsv, float depth = 1.0f) = 0;
  virtual void setVertexBuffer(Buffer& vb, uint32_t stride) = 0;
  virtual void setIndexBuffer(Buffer& ib, bool index32 = true) = 0;
  virtual void setPrimitiveTopology(PrimitiveTopology topology) = 0;
  virtual void setGraphicsRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset = 0) = 0;
  virtual void setGraphicsRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) = 0;
  virtual void setGraphicsRootSRV(uint32_t rootIndex, Texture& texture) = 0;
  virtual void setGraphicsRootSampler(uint32_t rootIndex, Sampler& sampler) = 0;
  virtual void setGraphicsRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) = 0;
  virtual void setGraphicsRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) = 0;
  virtual void setComputeRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset = 0) = 0;
  virtual void setComputeRootConstants(uint32_t rootIndex, const void* data, uint32_t num32BitValues) = 0;
  virtual void setComputeRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) = 0;
  virtual void setComputeRootUavTable(uint32_t rootIndex, uint32_t heapIndex) = 0;
  virtual void setComputeRootSamplerTable(uint32_t rootIndex, uint32_t heapIndex) = 0;
  virtual void setDescriptorHeap() = 0;
  virtual void draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;
  virtual void drawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) = 0;
  virtual void drawIndexedIndirect(Buffer& args, uint64_t offset = 0) = 0;
  virtual void drawIndexedIndirectCount(Buffer& args, uint64_t argsOffset, Buffer& count, uint64_t countOffset,
                                        uint32_t maxCount) {
    (void)args;
    (void)argsOffset;
    (void)count;
    (void)countOffset;
    (void)maxCount;
  }
  virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
  virtual void dispatchMesh(uint32_t x, uint32_t y, uint32_t z) {
    (void)x;
    (void)y;
    (void)z;
  }
  virtual void flushBarriers() {}
  virtual void uavBarrier(Texture* resource) { (void)resource; }
  virtual void aliasingBarrier(Texture* before, Texture* after) {
    (void)before;
    (void)after;
  }
  virtual void copyTextureToBuffer(Texture& src, Buffer& dst, uint32_t width, uint32_t height,
                                   Format format) = 0;
  // Optional GPU copy (toroidal scroll, etc.). Default no-op.
  virtual void copyTextureRegion(Texture& dst, uint32_t dstX, uint32_t dstY, Texture& src, uint32_t srcX,
                                 uint32_t srcY, uint32_t width, uint32_t height) {
    (void)dst;
    (void)dstX;
    (void)dstY;
    (void)src;
    (void)srcX;
    (void)srcY;
    (void)width;
    (void)height;
  }

  // DXR: build bottom/top-level acceleration structures (no-op if unsupported).
  virtual void buildBottomLevelAS(Buffer& /*dest*/, Buffer& /*scratch*/, const BlasTriangleGeometry& /*geo*/) {}
  virtual void buildTopLevelAS(Buffer& /*dest*/, Buffer& /*scratch*/, Buffer& /*instanceDescs*/,
                               uint32_t /*instanceCount*/) {}
  // Root SRV GPU VA for RaytracingAccelerationStructure (compute root param).
  virtual void setComputeRootAccelerationStructure(uint32_t /*rootIndex*/, Buffer& /*asBuffer*/) {}
};

struct Buffer {
  virtual ~Buffer() = default;
  virtual uint64_t size() const = 0;
  virtual void* mapped() = 0;
  // Bindless heap indices (UINT32_MAX if none).
  virtual uint32_t srvIndex() const { return 0u; }
  virtual uint32_t uavIndex() const { return 0u; }
  BufferUsage usage = BufferUsage::Vertex;
  ResourceState state = ResourceState::Common;
};

struct Texture {
  virtual ~Texture() = default;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  virtual Format format() const = 0;
  // Bindless SRV heap index (UINT32_MAX if none).
  virtual uint32_t bindlessIndex() const { return 0u; }
  TextureUsage usage = TextureUsage::ShaderResource;
  ResourceState state = ResourceState::Common;
};

struct Sampler {
  virtual ~Sampler() = default;
};

struct RootSignature {
  virtual ~RootSignature() = default;
};

struct PipelineState {
  virtual ~PipelineState() = default;
};

} // namespace tucano::rhi
