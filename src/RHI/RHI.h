#pragma once

#include "RHI/RHIBackend.h"
#include "RHI/RHITypes.h"

#include <algorithm>
#include <cstddef>
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

/// One entry of a transient descriptor table, named by RHI resource rather than by a backend
/// descriptor handle.
///
/// Most passes address resources by bindless index and need none of this. The ones that do not —
/// meshlet culling, Hi-Z, vegetation, terrain — bind a small contiguous table at a root slot, and a
/// table may mix kinds (the meshlet cull table is a structured buffer SRV followed by a Hi-Z
/// texture SRV), which is why this is one variant type instead of one span per resource kind.
struct ResourceView {
  enum class Kind : uint8_t { TextureSrv, TextureUav, BufferSrv, BufferUav };

  Kind kind = Kind::TextureSrv;
  Texture* texture = nullptr;
  Buffer* buffer = nullptr;

  static ResourceView srv(Texture& t) { return {Kind::TextureSrv, &t, nullptr}; }
  static ResourceView uav(Texture& t) { return {Kind::TextureUav, &t, nullptr}; }
  static ResourceView srv(Buffer& b) { return {Kind::BufferSrv, nullptr, &b}; }
  static ResourceView uav(Buffer& b) { return {Kind::BufferUav, nullptr, &b}; }
};

/// Upper bound on entries in a single transient table. The largest in the engine today is 5
/// (the mesh-shader vertex-stream table); the margin is there so a new pass does not have to
/// touch this header, and the clamp keeps an overlong table from writing past the stack array.
inline constexpr size_t kMaxTransientTableEntries = 16;

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

class Device {
public:
  virtual ~Device() = default;

  static std::unique_ptr<Device> create(bool enableDebugLayer = true);

  /// Runs `release` at the very start of this device's destruction, while the device is still
  /// usable.
  ///
  /// This exists because process-lifetime caches of GPU resources — the default checker textures,
  /// a Lua-created mesh — are freed during *static* destruction, which happens after the device is
  /// gone. `~DX12Texture` guards on `if (!device || !device->device())`, but by then `device` is a
  /// dangling pointer rather than null, so the guard does not fire and the read faults. It is
  /// intermittent: it only crashes when the freed page has already been decommitted.
  ///
  /// This was fixed once by calling the release functions from a runtime shutdown hook, and the
  /// bug came back when that hook went away and the calls went with it. Registering here instead
  /// means the owner of a cache states its own requirement, and no `main()` has to remember —
  /// which is the part that failed. Registration is idempotent per callback owner by convention:
  /// register on first cache fill.
  void onBeforeDestroy(std::function<void()> release) {
    m_releaseCallbacks.push_back(std::move(release));
  }

protected:
  /// Call from the most-derived destructor, first thing. Not done in ~Device because by the time
  /// the base destructor runs the derived device is already torn down.
  void runBeforeDestroyCallbacks() {
    // Reverse order: a cache registered later may hold something an earlier one handed it.
    for (auto it = m_releaseCallbacks.rbegin(); it != m_releaseCallbacks.rend(); ++it) {
      try {
        (*it)();
      } catch (...) {
      }
    }
    m_releaseCallbacks.clear();
  }

public:

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

  /// Copies `views` into a contiguous transient descriptor table and returns the heap index of the
  /// first entry — the value to hand to setGraphicsRootSrvTable / setComputeRootSrvTable /
  /// setComputeRootUavTable.
  ///
  /// The result is valid for the frame it was produced in; the backend recycles the range once the
  /// GPU is past that frame. Prefer bindlessIndex() / srvIndex() / uavIndex() wherever the shader
  /// can take an index — this is for the root slots that are declared as tables.
  virtual uint32_t writeResourceTable(std::span<const ResourceView> views) {
    (void)views;
    return 0;
  }
  virtual uint32_t writeSamplerTable(std::span<Sampler* const> samplers) {
    (void)samplers;
    return 0;
  }

  // Homogeneous shorthands. No allocation: the table is built on the stack and clamped to
  // kMaxTransientTableEntries. A null entry becomes the backend's null descriptor, so a caller may
  // leave a slot of a fixed-width table unfilled (the vegetation cull table is sized for the
  // maximum LOD count and only the active LODs are written).
  uint32_t writeTextureTable(std::span<Texture* const> textures) {
    return writeTable(textures, [](Texture* t) { return t ? ResourceView::srv(*t) : ResourceView{}; });
  }
  uint32_t writeTextureUavTable(std::span<Texture* const> textures) {
    return writeTable(textures, [](Texture* t) { return t ? ResourceView::uav(*t) : ResourceView{}; });
  }
  uint32_t writeBufferSrvTable(std::span<Buffer* const> buffers) {
    return writeTable(buffers, [](Buffer* b) { return b ? ResourceView::srv(*b) : ResourceView{}; });
  }
  uint32_t writeBufferUavTable(std::span<Buffer* const> buffers) {
    return writeTable(buffers, [](Buffer* b) { return b ? ResourceView::uav(*b) : ResourceView{}; });
  }

  /// Number of bytes one TLAS instance descriptor occupies in the instance buffer that
  /// buildTopLevelAS() reads, and the packing of `instances` into `dst` (which must be at least
  /// raytracingInstanceDescSize() * instances.size() bytes).
  ///
  /// The layout is the backend's — D3D12_RAYTRACING_INSTANCE_DESC, VkAccelerationStructureInstanceKHR
  /// — so the caller allocates and fills through these two calls rather than declaring the struct.
  /// Zero / no-op when raytracing is unsupported.
  virtual uint32_t raytracingInstanceDescSize() const { return 0; }
  virtual void writeRaytracingInstanceDescs(std::span<const TlasInstance> instances, void* dst) {
    (void)instances;
    (void)dst;
  }

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

  // Close, execute and wait on the current beginFrame() command list WITHOUT presenting or using
  // the mid-frame checkpoint machinery. For headless compute + readback (no swapchain), and unlike
  // submitGraphicsCheckpoint it leaves no dangling late-list state, so it can be called once per
  // beginFrame() repeatedly. Default no-op for non-DX12 backends.
  virtual void submitAndWaitHeadless() {}

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

private:
  template <class T, class MakeView>
  uint32_t writeTable(std::span<T* const> resources, MakeView makeView) {
    ResourceView views[kMaxTransientTableEntries];
    const size_t n = std::min(resources.size(), kMaxTransientTableEntries);
    for (size_t i = 0; i < n; ++i) {
      views[i] = makeView(resources[i]);
    }
    return writeResourceTable({views, n});
  }

  std::vector<std::function<void()>> m_releaseCallbacks;
};

class SwapChain {
public:
  virtual ~SwapChain() = default;
  virtual void resize(uint32_t width, uint32_t height) = 0;
  virtual Texture& backBuffer() = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  virtual void present() = 0;

  /// Non-blocking check for "the presentation engine will accept another frame now".
  ///
  /// backBuffer() otherwise waits indefinitely for this, which is fine when the render loop owns
  /// its thread. It is not fine when the loop shares a thread with a host UI: the wait parks that
  /// thread for most of a refresh interval every frame, and the UI only gets to run in whatever
  /// is left. Hosts should poll this and skip the tick when it returns false.
  ///
  /// Consumes the readiness signal when it returns true, exactly as the blocking wait does, so a
  /// true result must be followed by a rendered frame.
  virtual bool tryAcquireFrame() { return true; }
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
  // Clears only the given rect (incremental shadow/cache invalidation).
  virtual void clearRenderTargetRect(Texture& rtv, const float color[4], uint32_t x, uint32_t y, uint32_t w,
                                     uint32_t h) = 0;
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
  // Ends an open render pass / dynamic rendering. Vulkan needs this before ImGui begins its own.
  virtual void endRendering() {}
  virtual void uavBarrier(Texture* resource) { (void)resource; }
  virtual void aliasingBarrier(Texture* before, Texture* after) {
    (void)before;
    (void)after;
  }
  virtual void copyTextureToBuffer(Texture& src, Buffer& dst, uint32_t width, uint32_t height,
                                   Format format) = 0;
  // Buffer-to-buffer region copy. The workhorse for GPU→CPU readback of a compute result: dispatch
  // into a DEFAULT-heap UAV, copy it here into a READBACK-heap buffer, then map that buffer.
  // Default no-op so non-DX12 backends need no change; DX12 overrides it.
  virtual void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset,
                          uint64_t size) {
    (void)dst;
    (void)dstOffset;
    (void)src;
    (void)srcOffset;
    (void)size;
  }
  // Copy an upload buffer (256-aligned rows) into a texture (2D or 3D) on this command list —
  // avoids the shared upload-arena reset that uploadTexture() does mid-frame.
  virtual void copyBufferToTexture(Buffer& src, Texture& dst, uint32_t width, uint32_t height, uint32_t depth,
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
  // Bindless UAV heap index, for compute passes that write this texture.
  virtual uint32_t bindlessUavIndex() const { return 0u; }
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
