#include "RHI/Null/NullDevice.h"

#include <cstring>
#include <vector>

// -----------------------------------------------------------------------------
// Null RHI backend implementation. All classes live in an anonymous namespace so
// the only public symbol is createNullDevice(). Every override is intentionally
// trivial; `// TODO(vulkan):` markers show where the Vulkan backend will differ.
// -----------------------------------------------------------------------------

namespace tucano::rhi {
namespace {

inline constexpr uint32_t kNoBindless = 0xFFFFFFFFu; // UINT32_MAX == "no descriptor"

// --- Resources -------------------------------------------------------------

struct NullBuffer final : Buffer {
  explicit NullBuffer(const BufferDesc& desc) : size_(desc.size), store_(desc.size) {
    usage = desc.usage;
    state = ResourceState::Common;
  }
  uint64_t size() const override { return size_; }
  // Mapped CPU pointer: real for Dynamic/Upload/Readback so client code that writes
  // instance/constant data into a mapped ring still works headlessly.
  // TODO(vulkan): return the vmaMapMemory() pointer for HOST_VISIBLE allocations.
  void* mapped() override { return store_.empty() ? nullptr : store_.data(); }
  uint32_t srvIndex() const override { return kNoBindless; }
  uint32_t uavIndex() const override { return kNoBindless; }

  uint64_t size_;
  std::vector<uint8_t> store_;
};

struct NullTexture final : Texture {
  explicit NullTexture(const TextureDesc& desc)
      : width_(desc.width), height_(desc.height), format_(desc.format) {
    usage = desc.usage;
    state = ResourceState::Common;
  }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  Format format() const override { return format_; }
  uint32_t bindlessIndex() const override { return kNoBindless; }

  uint32_t width_;
  uint32_t height_;
  Format format_;
};

struct NullSampler final : Sampler {};
struct NullRootSignature final : RootSignature {};
struct NullPipelineState final : PipelineState {};

// --- SwapChain -------------------------------------------------------------

class NullSwapChain final : public SwapChain {
public:
  NullSwapChain(uint32_t w, uint32_t h) : back_(makeBackDesc(w, h)) {}
  void resize(uint32_t w, uint32_t h) override { back_ = NullTexture(makeBackDesc(w, h)); }
  Texture& backBuffer() override { return back_; }
  uint32_t width() const override { return back_.width(); }
  uint32_t height() const override { return back_.height(); }
  void present() override {} // TODO(vulkan): vkQueuePresentKHR
  bool tryAcquireFrame() override { return true; }

private:
  static TextureDesc makeBackDesc(uint32_t w, uint32_t h) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = Format::R8G8B8A8_UNORM;
    d.usage = TextureUsage::RenderTarget;
    d.debugName = "NullBackBuffer";
    return d;
  }
  NullTexture back_;
};

// --- CommandList -----------------------------------------------------------
// Every method is a no-op. TODO(vulkan) tags the vkCmd* each will issue.

class NullCommandList final : public CommandList {
public:
  void transition(Texture&, ResourceState) override {}          // TODO(vulkan): vkCmdPipelineBarrier2
  void transition(Buffer&, ResourceState) override {}           // TODO(vulkan): buffer memory barrier
  void setPipeline(PipelineState&) override {}                  // TODO(vulkan): vkCmdBindPipeline
  void setRootSignature(RootSignature&) override {}             // TODO(vulkan): pipeline layout is bound with the pipeline
  void setViewport(const Viewport&) override {}                 // TODO(vulkan): vkCmdSetViewport
  void setScissor(const Scissor&) override {}                   // TODO(vulkan): vkCmdSetScissor
  void setRenderTargets(std::span<Texture*>, Texture*) override {} // TODO(vulkan): vkCmdBeginRendering (dynamic rendering)
  void clearRenderTarget(Texture&, const float[4]) override {}  // TODO(vulkan): VkRenderingAttachmentInfo loadOp CLEAR
  void clearRenderTargetRect(Texture&, const float[4], uint32_t, uint32_t, uint32_t, uint32_t) override {}
  void clearDepth(Texture&, float) override {}
  void setVertexBuffer(Buffer&, uint32_t) override {}           // TODO(vulkan): vkCmdBindVertexBuffers
  void setIndexBuffer(Buffer&, bool) override {}                // TODO(vulkan): vkCmdBindIndexBuffer
  void setPrimitiveTopology(PrimitiveTopology) override {}      // TODO(vulkan): baked into PSO or vkCmdSetPrimitiveTopology
  void setGraphicsRootCBV(uint32_t, Buffer&, uint64_t) override {}      // TODO(vulkan): descriptor / push
  void setGraphicsRootConstants(uint32_t, const void*, uint32_t) override {} // TODO(vulkan): vkCmdPushConstants
  void setGraphicsRootSRV(uint32_t, Texture&) override {}
  void setGraphicsRootSampler(uint32_t, Sampler&) override {}
  void setGraphicsRootSrvTable(uint32_t, uint32_t) override {}  // TODO(vulkan): bind bindless set (descriptor_indexing)
  void setGraphicsRootSamplerTable(uint32_t, uint32_t) override {}
  void setComputeRootCBV(uint32_t, Buffer&, uint64_t) override {}
  void setComputeRootConstants(uint32_t, const void*, uint32_t) override {}
  void setComputeRootSrvTable(uint32_t, uint32_t) override {}
  void setComputeRootUavTable(uint32_t, uint32_t) override {}
  void setComputeRootSamplerTable(uint32_t, uint32_t) override {}
  void setDescriptorHeap() override {}                         // TODO(vulkan): bind the global descriptor set(s)
  void draw(uint32_t, uint32_t) override {}                    // TODO(vulkan): vkCmdDraw
  void drawIndexed(uint32_t, uint32_t, int32_t) override {}    // TODO(vulkan): vkCmdDrawIndexed
  void drawIndexedIndirect(Buffer&, uint64_t) override {}      // TODO(vulkan): vkCmdDrawIndexedIndirect
  void dispatch(uint32_t, uint32_t, uint32_t) override {}      // TODO(vulkan): vkCmdDispatch
  void copyTextureToBuffer(Texture&, Buffer&, uint32_t, uint32_t, Format) override {}
  void copyBufferToTexture(Buffer&, Texture&, uint32_t, uint32_t, uint32_t, Format) override {}
};

// --- Device ----------------------------------------------------------------

class NullDevice final : public Device {
public:
  std::unique_ptr<SwapChain> createSwapChain(void*, uint32_t width, uint32_t height, bool) override {
    return std::make_unique<NullSwapChain>(width, height);
  }
  std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initialData) override {
    auto buf = std::make_shared<NullBuffer>(desc);
    if (initialData && desc.size > 0) {
      std::memcpy(buf->store_.data(), initialData, static_cast<size_t>(desc.size));
    }
    return buf;
  }
  std::shared_ptr<Texture> createTexture(const TextureDesc& desc, const void*, uint32_t) override {
    return std::make_shared<NullTexture>(desc);
  }
  std::shared_ptr<Sampler> createSampler(const SamplerDesc&) override {
    return std::make_shared<NullSampler>();
  }
  std::shared_ptr<RootSignature> createRootSignature(bool) override {
    return std::make_shared<NullRootSignature>();
  }
  std::shared_ptr<RootSignature> createComputeRootSignature() override {
    return std::make_shared<NullRootSignature>();
  }
  std::shared_ptr<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc&) override {
    return std::make_shared<NullPipelineState>();
  }
  std::shared_ptr<PipelineState> createComputePipeline(const ComputePipelineDesc&) override {
    return std::make_shared<NullPipelineState>();
  }

  CommandList* beginFrame() override { return &cmd_; }
  void endFrame(SwapChain&) override { ++frame_; }
  void waitIdle() override {}
  uint32_t frameIndex() const override { return static_cast<uint32_t>(frame_ % kMaxFramesInFlight); }
  uint64_t frameFenceValue() const override { return frame_; }

  void uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset) override {
    if (auto* nb = dynamic_cast<NullBuffer*>(&buffer)) {
      if (data && offset + size <= nb->store_.size()) {
        std::memcpy(nb->store_.data() + offset, data, static_cast<size_t>(size));
      }
    }
  }
  void uploadTexture(Texture&, const void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}

  void* nativeDevice() const override { return nullptr; }

  // TODO(vulkan): allocate `views.size()` slots from the frame's descriptor arena, write one
  // VkDescriptorImageInfo / VkDescriptorBufferInfo per view, and return the first slot index.
  // Slot 0 here means "the table base", which is what every setRootSrvTable no-op expects.
  uint32_t writeResourceTable(std::span<const ResourceView>) override { return 0; }
  // TODO(vulkan): same, against the sampler set (set 1).
  uint32_t writeSamplerTable(std::span<Sampler* const>) override { return 0; }

private:
  NullCommandList cmd_;
  uint64_t frame_ = 0;
};

} // namespace

std::unique_ptr<Device> createNullDevice() { return std::make_unique<NullDevice>(); }

} // namespace tucano::rhi
