#include "RHI/Vulkan/VulkanDevice.h"

#include "Core/Memory.h"
#include "RHI/RHIBackend.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace tucano::rhi {
namespace {

std::atomic<uint32_t> g_vkErrors{0};
std::atomic<uint32_t> g_vkWarnings{0};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  tucano::core::memoryInitThreadHeap();
  const char* level = "INFO";
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    level = "ERROR";
    ++g_vkErrors;
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    level = "WARN";
    ++g_vkWarnings;
  }
  std::cerr << "[Vulkan][" << level << "] " << (data && data->pMessage ? data->pMessage : "") << "\n";
  return VK_FALSE;
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
  VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspect;
  // One VkImageLayout is tracked per texture. Every mip/layer must stay in that layout;
  // leaving extras UNDEFINED is what made IBL SampleLevel (roughness > 0) read garbage
  // and GPUVM-fault on RADV (SQC PERMISSION_FAULTS).
  barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
  barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dep);
}

void bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size, VkPipelineStageFlags2 srcStage,
                   VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.buffer = buffer;
  barrier.size = size;
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.bufferMemoryBarrierCount = 1;
  dep.pBufferMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

class VulkanCommandList final : public CommandList {
public:
  VulkanCommandList(VulkanDevice* device, VkCommandBuffer cmd) : m_device(device), m_cmd(cmd) {}

  VkCommandBuffer get() const { return m_cmd; }
  void setCommandBuffer(VkCommandBuffer cmd) { m_cmd = cmd; }

  void endRenderingIfNeeded() {
    if (m_rendering) {
      vkCmdEndRendering(m_cmd);
      m_rendering = false;
    }
  }
  void endRendering() override { endRenderingIfNeeded(); }

  void transition(Texture& texture, ResourceState state) override {
    auto& tex = static_cast<VulkanTexture&>(texture);
    VkImageLayout dst = VK_IMAGE_LAYOUT_GENERAL;
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    switch (state) {
    case ResourceState::RenderTarget:
      dst = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
      dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
      break;
    case ResourceState::Present:
      dst = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      dstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
      dstAccess = 0;
      break;
    case ResourceState::CopySrc:
      dst = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
      dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
      break;
    case ResourceState::CopyDst:
      dst = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
      dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      break;
    case ResourceState::ShaderResource:
      dst = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      // Rain particles sample the occluder from the VS. A FRAGMENT-only barrier
      // left the image unmapped for SQC in the vertex stage (PERMISSION_FAULTS=3).
      dstStage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
      dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
      break;
    case ResourceState::UnorderedAccess:
      dst = VK_IMAGE_LAYOUT_GENERAL;
      dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      dstAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
      break;
    default:
      break;
    }
    if (state == ResourceState::DepthWrite || state == ResourceState::DepthRead) {
      dst = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
      dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      if (state == ResourceState::DepthRead) {
        dst = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
      }
    }
    if (tex.layout == dst) {
      tex.state = state;
      return;
    }
    endRenderingIfNeeded();
    const VkImageLayout oldLayout = tex.layout;
    imageBarrier(m_cmd, tex.image, oldLayout, dst, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_WRITE_BIT, dstStage, dstAccess, tex.aspect);
    tex.layout = dst;
    tex.state = state;
  }

  void transition(Buffer& buffer, ResourceState state) override {
    auto& buf = static_cast<VulkanBuffer&>(buffer);
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    switch (state) {
    case ResourceState::CopySrc:
      dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
      dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT;
      break;
    case ResourceState::CopyDst:
      dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
      dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      break;
    case ResourceState::IndirectArgument:
      dstStage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
      dstAccess = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
      break;
    case ResourceState::UnorderedAccess:
      dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      dstAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
      break;
    case ResourceState::ShaderResource:
    case ResourceState::ConstantBuffer:
      dstStage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
      dstAccess = VK_ACCESS_2_SHADER_READ_BIT;
      break;
    default:
      break;
    }
    endRenderingIfNeeded();
    bufferBarrier(m_cmd, buf.buffer, buf.byteSize, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_WRITE_BIT, dstStage, dstAccess);
    buf.state = state;
  }
  void setPipeline(PipelineState& pso) override {
    auto& vk = static_cast<VulkanPipelineState&>(pso);
    if (vk.isCompute) {
      endRenderingIfNeeded();
      vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipeline);
    } else {
      vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);
    }
    m_layout = vk.layout;
    if (m_layout) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void setRootSignature(RootSignature& rs) override {
    auto& vk = static_cast<VulkanRootSignature&>(rs);
    m_layout = vk.layout;
    m_pushStages = vk.pushStages;
    if (m_layout) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void setViewport(const Viewport& vp) override {
    ensureRendering();
    // Negative height matches DX12 (Y-up, clockwise front) in Vulkan NDC.
    VkViewport v{vp.x, vp.y + vp.height, vp.width, -vp.height, vp.minDepth, vp.maxDepth};
    vkCmdSetViewport(m_cmd, 0, 1, &v);
  }
  void setScissor(const Scissor& sc) override {
    ensureRendering();
    VkRect2D r{};
    r.offset = {sc.left, sc.top};
    r.extent = {static_cast<uint32_t>(std::max(0, sc.right - sc.left)),
                static_cast<uint32_t>(std::max(0, sc.bottom - sc.top))};
    vkCmdSetScissor(m_cmd, 0, 1, &r);
  }
  void setRenderTargets(std::span<Texture*> rtvs, Texture* dsv) override {
    endRenderingIfNeeded();
    m_rtCount = 0;
    for (Texture* t : rtvs) {
      if (m_rtCount >= kMaxColorAttachments) {
        break;
      }
      m_rts[m_rtCount++] = static_cast<VulkanTexture*>(t);
    }
    m_dsv = dsv ? static_cast<VulkanTexture*>(dsv) : nullptr;
    m_clearColorMask = 0;
    m_clearDepthPending = false;
  }
  void clearRenderTarget(Texture& rtv, const float color[4]) override {
    auto* tex = static_cast<VulkanTexture*>(&rtv);
    for (uint32_t i = 0; i < m_rtCount; ++i) {
      if (m_rts[i] == tex) {
        if (m_rendering) {
          VkClearAttachment att{};
          att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          att.colorAttachment = i;
          std::memcpy(att.clearValue.color.float32, color, sizeof(float) * 4);
          VkClearRect rect{};
          rect.rect.extent = {tex->w, tex->h};
          rect.layerCount = 1;
          vkCmdClearAttachments(m_cmd, 1, &att, 1, &rect);
          return;
        }
        std::memcpy(m_clearColors[i], color, sizeof(float) * 4);
        m_clearColorMask |= (1u << i);
        return;
      }
    }
    if (m_rtCount == 0) {
      m_rts[0] = tex;
      m_rtCount = 1;
      std::memcpy(m_clearColors[0], color, sizeof(float) * 4);
      m_clearColorMask = 1;
    }
  }
  void clearRenderTargetRect(Texture& rtv, const float color[4], uint32_t x, uint32_t y, uint32_t w,
                             uint32_t h) override {
    auto* tex = static_cast<VulkanTexture*>(&rtv);
    uint32_t attach = UINT32_MAX;
    for (uint32_t i = 0; i < m_rtCount; ++i) {
      if (m_rts[i] == tex) {
        attach = i;
        break;
      }
    }
    if (attach == UINT32_MAX) {
      m_rts[0] = tex;
      m_rtCount = 1;
      m_dsv = nullptr;
      attach = 0;
    }
    ensureRendering();
    VkClearAttachment att{};
    att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    att.colorAttachment = attach;
    std::memcpy(att.clearValue.color.float32, color, sizeof(float) * 4);
    VkClearRect rect{};
    rect.rect.offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
    rect.rect.extent = {w, h};
    rect.layerCount = 1;
    vkCmdClearAttachments(m_cmd, 1, &att, 1, &rect);
  }
  void clearDepth(Texture& dsv, float depth) override {
    m_dsv = static_cast<VulkanTexture*>(&dsv);
    if (m_rendering) {
      VkClearAttachment att{};
      att.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      att.clearValue.depthStencil.depth = depth;
      VkClearRect rect{};
      rect.rect.extent = {m_dsv->w, m_dsv->h};
      rect.layerCount = 1;
      vkCmdClearAttachments(m_cmd, 1, &att, 1, &rect);
      return;
    }
    m_clearDepth = depth;
    m_clearDepthPending = true;
  }
  void setVertexBuffer(Buffer& vb, uint32_t) override {
    auto& buf = static_cast<VulkanBuffer&>(vb);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_cmd, 0, 1, &buf.buffer, &offset);
  }
  void setIndexBuffer(Buffer& ib, bool index32) override {
    auto& buf = static_cast<VulkanBuffer&>(ib);
    vkCmdBindIndexBuffer(m_cmd, buf.buffer, 0, index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
  }
  void setPrimitiveTopology(PrimitiveTopology) override {}
  void setGraphicsRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) override {
    auto& buf = static_cast<VulkanBuffer&>(buffer);
    const uint32_t binding = rootIndex >= 1 && rootIndex <= 2 ? rootIndex : 0u;
    // Copy into the UBO ring (stride fits LightingCB/LightCB). Binding the caller's
    // 64 KiB PostCB as a UBO with a huge range equals maxUniformBufferRange on RADV
    // and the dummy UBO stays bound (solid dummy color / "green screen").
    const VkDeviceSize remaining = buf.byteSize > offset ? buf.byteSize - offset : 16;
    const uint32_t copy = static_cast<uint32_t>(std::min<VkDeviceSize>(remaining, kPushUboStride));
    if (buf.mappedPtr && copy > 0) {
      m_device->writeRingUbo(binding, static_cast<const char*>(buf.mappedPtr) + offset, copy);
    } else {
      m_device->writeUbo(binding, buf.buffer, offset, copy);
    }
    if (m_layout) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void setGraphicsRootConstants(uint32_t, const void* data, uint32_t num32BitValues) override {
    if (!data || num32BitValues == 0) {
      return;
    }
    const uint32_t bytes = num32BitValues * 4u;
    if (m_layout) {
      vkCmdPushConstants(m_cmd, m_layout, m_pushStages, 0, bytes, data);
    }
    m_device->writePushUbo(data, bytes);
    if (m_layout) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void setGraphicsRootSRV(uint32_t, Texture&) override {}
  void setGraphicsRootSampler(uint32_t, Sampler&) override {}
  void setGraphicsRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) override {
    if (!m_layout) {
      return;
    }
    if (rootIndex == 3) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    } else if (rootIndex == 5) {
      m_device->bindStorageTable(m_cmd, m_layout, heapIndex);
    }
  }
  void setGraphicsRootSamplerTable(uint32_t, uint32_t heapIndex) override {
    if (m_layout) {
      m_device->bindSamplerTable(m_cmd, m_layout, heapIndex);
    }
  }
  void setComputeRootCBV(uint32_t rootIndex, Buffer& buffer, uint64_t offset) override {
    setGraphicsRootCBV(rootIndex, buffer, offset);
  }
  void setComputeRootConstants(uint32_t, const void* data, uint32_t num32BitValues) override {
    setGraphicsRootConstants(0, data, num32BitValues);
  }
  void setComputeRootSrvTable(uint32_t rootIndex, uint32_t heapIndex) override {
    if (!m_layout) {
      return;
    }
    if (rootIndex == 2 || rootIndex == 5) {
      m_device->bindStorageTable(m_cmd, m_layout, heapIndex);
    } else {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void setComputeRootUavTable(uint32_t, uint32_t heapIndex) override {
    endRenderingIfNeeded();
    if (m_layout) {
      m_device->bindComputeUavTable(m_cmd, m_layout, heapIndex);
    }
  }
  void setComputeRootSamplerTable(uint32_t, uint32_t heapIndex) override {
    if (m_layout) {
      m_device->bindSamplerTable(m_cmd, m_layout, heapIndex);
    }
  }
  void setDescriptorHeap() override {
    if (m_layout) {
      m_device->bindGraphicsHeaps(m_cmd, m_layout);
    }
  }
  void draw(uint32_t vertexCount, uint32_t startVertex) override {
    ensureRendering();
    vkCmdDraw(m_cmd, vertexCount, 1, startVertex, 0);
  }
  void drawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex) override {
    ensureRendering();
    vkCmdDrawIndexed(m_cmd, indexCount, 1, startIndex, baseVertex, 0);
  }
  void drawIndexedIndirect(Buffer& args, uint64_t offset) override {
    ensureRendering();
    auto& buf = static_cast<VulkanBuffer&>(args);
    vkCmdDrawIndexedIndirect(m_cmd, buf.buffer, offset, 1, sizeof(VkDrawIndexedIndirectCommand));
  }
  void drawIndexedIndirectCount(Buffer& args, uint64_t argsOffset, Buffer& count, uint64_t countOffset,
                                uint32_t maxCount) override {
    ensureRendering();
    auto& argBuf = static_cast<VulkanBuffer&>(args);
    auto& cntBuf = static_cast<VulkanBuffer&>(count);
    vkCmdDrawIndexedIndirectCount(m_cmd, argBuf.buffer, argsOffset, cntBuf.buffer, countOffset, maxCount,
                                  sizeof(VkDrawIndexedIndirectCommand));
  }
  void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
    endRenderingIfNeeded();
    vkCmdDispatch(m_cmd, x, y, z);
  }
  void flushBarriers() override {}
  void uavBarrier(Texture* resource) override {
    endRenderingIfNeeded();
    if (!resource) {
      VkMemoryBarrier2 mem{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
      mem.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
      mem.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
      mem.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
      VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
      dep.memoryBarrierCount = 1;
      dep.pMemoryBarriers = &mem;
      vkCmdPipelineBarrier2(m_cmd, &dep);
      return;
    }
    auto& tex = static_cast<VulkanTexture&>(*resource);
    imageBarrier(m_cmd, tex.image, tex.layout, tex.layout, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
  }
  void aliasingBarrier(Texture*, Texture*) override {
    endRenderingIfNeeded();
    VkMemoryBarrier2 mem{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mem.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mem.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mem.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mem.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mem;
    vkCmdPipelineBarrier2(m_cmd, &dep);
  }
  void copyTextureToBuffer(Texture& src, Buffer& dst, uint32_t width, uint32_t height, Format format) override {
    endRenderingIfNeeded();
    auto& tex = static_cast<VulkanTexture&>(src);
    auto& buf = static_cast<VulkanBuffer&>(dst);
    const uint32_t stride = formatStride(format);
    const uint32_t rowPitch = (formatRowPitch(format, width) + 255u) & ~255u;
    VkBufferImageCopy copy{};
    copy.bufferRowLength = stride ? rowPitch / stride : width;
    copy.bufferImageHeight = height;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(m_cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf.buffer, 1, &copy);
  }
  void copyBuffer(Buffer& dst, uint64_t dstOffset, Buffer& src, uint64_t srcOffset, uint64_t size) override {
    endRenderingIfNeeded();
    auto& d = static_cast<VulkanBuffer&>(dst);
    auto& s = static_cast<VulkanBuffer&>(src);
    bufferBarrier(m_cmd, s.buffer, s.byteSize, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    bufferBarrier(m_cmd, d.buffer, d.byteSize, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferCopy region{};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = size;
    vkCmdCopyBuffer(m_cmd, s.buffer, d.buffer, 1, &region);
  }
  void copyBufferToTexture(Buffer& src, Texture& dst, uint32_t width, uint32_t height, uint32_t depth,
                           Format format) override {
    endRenderingIfNeeded();
    auto& buf = static_cast<VulkanBuffer&>(src);
    auto& tex = static_cast<VulkanTexture&>(dst);
    const uint32_t stride = formatStride(format);
    const uint32_t rowPitch = (formatRowPitch(format, width) + 255u) & ~255u;
    VkBufferImageCopy copy{};
    copy.bufferRowLength = stride ? rowPitch / stride : width;
    copy.bufferImageHeight = height;
    copy.imageSubresource.aspectMask = tex.aspect;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, std::max(1u, depth)};
    vkCmdCopyBufferToImage(m_cmd, buf.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  }
  void copyTextureRegion(Texture& dst, uint32_t dstX, uint32_t dstY, Texture& src, uint32_t srcX, uint32_t srcY,
                         uint32_t width, uint32_t height) override {
    endRenderingIfNeeded();
    auto& d = static_cast<VulkanTexture&>(dst);
    auto& s = static_cast<VulkanTexture&>(src);
    VkImageCopy region{};
    region.srcSubresource.aspectMask = s.aspect;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {static_cast<int32_t>(srcX), static_cast<int32_t>(srcY), 0};
    region.dstSubresource.aspectMask = d.aspect;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {static_cast<int32_t>(dstX), static_cast<int32_t>(dstY), 0};
    region.extent = {width, height, 1};
    vkCmdCopyImage(m_cmd, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }

private:
  void ensureRendering() {
    if (m_rendering || m_rtCount == 0) {
      return;
    }
    for (uint32_t i = 0; i < m_rtCount; ++i) {
      if (m_rts[i]->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        transition(*m_rts[i], ResourceState::RenderTarget);
      }
    }
    if (m_dsv && m_dsv->layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL &&
        m_dsv->layout != VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL) {
      transition(*m_dsv, ResourceState::DepthWrite);
    }
    VkRenderingAttachmentInfo colors[kMaxColorAttachments]{};
    for (uint32_t i = 0; i < m_rtCount; ++i) {
      auto& color = colors[i];
      color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      color.imageView = m_rts[i]->view;
      color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      const bool clear = (m_clearColorMask & (1u << i)) != 0;
      color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
      color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      if (clear) {
        std::memcpy(color.clearValue.color.float32, m_clearColors[i], sizeof(float) * 4);
      }
    }
    m_clearColorMask = 0;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = {m_rts[0]->w, m_rts[0]->h};
    info.layerCount = 1;
    info.colorAttachmentCount = m_rtCount;
    info.pColorAttachments = colors;
    if (m_dsv) {
      depth.imageView = m_dsv->view;
      depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      depth.loadOp = m_clearDepthPending ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depth.clearValue.depthStencil.depth = m_clearDepth;
      m_clearDepthPending = false;
      info.pDepthAttachment = &depth;
    }
    vkCmdBeginRendering(m_cmd, &info);
    m_rendering = true;
  }

  VulkanDevice* m_device = nullptr;
  VkCommandBuffer m_cmd = VK_NULL_HANDLE;
  std::array<VulkanTexture*, kMaxColorAttachments> m_rts{};
  uint32_t m_rtCount = 0;
  VulkanTexture* m_dsv = nullptr;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;
  VkShaderStageFlags m_pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  float m_clearColors[kMaxColorAttachments][4]{};
  uint32_t m_clearColorMask = 0;
  float m_clearDepth = 1.0f;
  bool m_clearDepthPending = false;
  bool m_rendering = false;
};

class VulkanSwapChain final : public SwapChain {
public:
  VulkanSwapChain(VulkanDevice* device, GLFWwindow* window, uint32_t width, uint32_t height, bool vsync)
      : m_device(device), m_window(window), m_width(width), m_height(height), m_vsync(vsync) {
    vkCheck(glfwCreateWindowSurface(device->instance(), window, nullptr, &m_surface), "glfwCreateWindowSurface");
    create(width, height);
  }

  ~VulkanSwapChain() override { destroy(); }

  void resize(uint32_t width, uint32_t height) override {
    if (width == 0 || height == 0 || (width == m_width && height == m_height)) {
      return;
    }
    m_device->waitIdle();
    destroy();
    m_width = width;
    m_height = height;
    vkCheck(glfwCreateWindowSurface(m_device->instance(), m_window, nullptr, &m_surface),
            "glfwCreateWindowSurface");
    create(width, height);
  }

  Texture& backBuffer() override {
    vkCheck(vkAcquireNextImageKHR(m_device->device(), m_swapchain, UINT64_MAX, m_device->acquireSemaphore(),
                                  VK_NULL_HANDLE, &m_imageIndex),
            "vkAcquireNextImageKHR");
    return *m_images[m_imageIndex];
  }

  uint32_t width() const override { return m_width; }
  uint32_t height() const override { return m_height; }

  void present() override {
    VkSemaphore wait = m_device->renderSemaphore();
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &m_imageIndex;
    const VkResult r = vkQueuePresentKHR(m_device->graphicsQueue(), &info);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
      return;
    }
    vkCheck(r, "vkQueuePresentKHR");
  }

  uint32_t imageIndex() const { return m_imageIndex; }
  VkFormat surfaceFormat() const { return m_format; }

private:
  void create(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->physicalDevice(), m_surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->physicalDevice(), m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->physicalDevice(), m_surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (const auto& f : formats) {
      if (f.format == VK_FORMAT_R8G8B8A8_UNORM) {
        chosen = f;
        break;
      }
    }
    m_format = chosen.format;

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->physicalDevice(), m_surface, &presentCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->physicalDevice(), m_surface, &presentCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsync) {
      for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR || m == VK_PRESENT_MODE_IMMEDIATE_KHR) {
          presentMode = m;
          if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            break;
          }
        }
      }
    }

    VkExtent2D extent{width, height};
    if (caps.currentExtent.width != UINT32_MAX) {
      extent = caps.currentExtent;
    }
    m_width = extent.width;
    m_height = extent.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
      imageCount = caps.maxImageCount;
    }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_device->physicalDevice(), m_device->graphicsQueueFamily(), m_surface,
                                         &supported);
    if (!supported) {
      throw std::runtime_error("graphics queue cannot present to this surface");
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    vkCheck(vkCreateSwapchainKHR(m_device->device(), &info, nullptr, &m_swapchain), "vkCreateSwapchainKHR");

    uint32_t actual = 0;
    vkGetSwapchainImagesKHR(m_device->device(), m_swapchain, &actual, nullptr);
    std::vector<VkImage> images(actual);
    vkGetSwapchainImagesKHR(m_device->device(), m_swapchain, &actual, images.data());
    m_images.clear();
    m_images.reserve(actual);
    for (VkImage image : images) {
      auto tex = std::make_unique<VulkanTexture>();
      tex->owner = m_device;
      tex->device = m_device->device();
      tex->image = image;
      tex->ownsImage = false;
      tex->w = m_width;
      tex->h = m_height;
      tex->fmt = Format::R8G8B8A8_UNORM;
      tex->layout = VK_IMAGE_LAYOUT_UNDEFINED;
      tex->usage = TextureUsage::RenderTarget;
      tex->state = ResourceState::Present;
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = image;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = m_format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      vkCheck(vkCreateImageView(m_device->device(), &view, nullptr, &tex->view), "vkCreateImageView");
      m_images.push_back(std::move(tex));
    }
  }

  void destroy() {
    if (m_device && m_device->device()) {
      vkDeviceWaitIdle(m_device->device());
    }
    m_images.clear();
    if (m_swapchain) {
      vkDestroySwapchainKHR(m_device->device(), m_swapchain, nullptr);
      m_swapchain = VK_NULL_HANDLE;
    }
    if (m_surface) {
      vkDestroySurfaceKHR(m_device->instance(), m_surface, nullptr);
      m_surface = VK_NULL_HANDLE;
    }
  }

  VulkanDevice* m_device = nullptr;
  GLFWwindow* m_window = nullptr;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_R8G8B8A8_UNORM;
  std::vector<std::unique_ptr<VulkanTexture>> m_images;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  uint32_t m_imageIndex = 0;
  bool m_vsync = true;
};

std::unique_ptr<Device> createVulkanDevice(bool enableDebugLayer) {
  return std::make_unique<VulkanDevice>(enableDebugLayer);
}

VulkanDevice::VulkanDevice(bool enableDebugLayer) : m_debug(enableDebugLayer) {
  createInstance(enableDebugLayer);
  pickDevice();
  createLogicalDevice();
  createAllocator();
  createFrameSync();
  createBindless();
}

VulkanDevice::~VulkanDevice() {
  runBeforeDestroyCallbacks();
  waitIdle();
  destroyFrameSync();
  destroyBindless();
  if (m_commandPool) {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  }
  if (m_allocator) {
    vmaDestroyAllocator(m_allocator);
    m_allocator = VK_NULL_HANDLE;
  }
  if (m_device) {
    vkDestroyDevice(m_device, nullptr);
  }
  if (m_messenger) {
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn) {
      fn(m_instance, m_messenger, nullptr);
    }
  }
  if (m_instance) {
    vkDestroyInstance(m_instance, nullptr);
  }
  if (m_debug) {
    std::cerr << "[Vulkan] validation errors=" << g_vkErrors.load() << " warnings=" << g_vkWarnings.load()
              << "\n";
  }
}

void VulkanDevice::createInstance(bool enableDebug) {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Tucano";
  app.apiVersion = VK_API_VERSION_1_3;

  uint32_t glfwCount = 0;
  const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
  std::vector<const char*> extensions(glfwExt, glfwExt + glfwCount);
  if (enableDebug) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  info.ppEnabledExtensionNames = extensions.data();
  if (enableDebug) {
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = layers;
  }
  vkCheck(vkCreateInstance(&info, nullptr, &m_instance), "vkCreateInstance");

  if (enableDebug) {
    VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debugCallback;
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (fn) {
      vkCheck(fn(m_instance, &dbg, nullptr, &m_messenger), "vkCreateDebugUtilsMessengerEXT");
    }
  }
}

void VulkanDevice::pickDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
  if (count == 0) {
    throw std::runtime_error("no Vulkan physical device");
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
  for (VkPhysicalDevice candidate : devices) {
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
    for (uint32_t i = 0; i < familyCount; ++i) {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        m_physical = candidate;
        m_queueFamily = i;
        return;
      }
    }
  }
  throw std::runtime_error("no graphics-capable Vulkan device");
}

void VulkanDevice::createLogicalDevice() {
  float prio = 1.0f;
  VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  q.queueFamilyIndex = m_queueFamily;
  q.queueCount = 1;
  q.pQueuePriorities = &prio;

  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = VK_TRUE;
  features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
  features.shaderStorageImageReadWithoutFormat = VK_TRUE;
  VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  v12.descriptorIndexing = VK_TRUE;
  v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  v12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
  v12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
  v12.runtimeDescriptorArray = VK_TRUE;
  v12.descriptorBindingPartiallyBound = VK_TRUE;
  v12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  v12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
  v12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
  v12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
  v12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
  v12.drawIndirectCount = VK_TRUE;
  VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  v13.pNext = &v12;
  v13.dynamicRendering = VK_TRUE;
  v13.synchronization2 = VK_TRUE;
  v13.shaderDemoteToHelperInvocation = VK_TRUE;
  features.geometryShader = VK_TRUE;
  features.shaderFloat64 = VK_TRUE;
  features.shaderInt64 = VK_TRUE;
  features.multiDrawIndirect = VK_TRUE;
  features.drawIndirectFirstInstance = VK_TRUE;

  const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  info.pNext = &v13;
  info.queueCreateInfoCount = 1;
  info.pQueueCreateInfos = &q;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = deviceExt;
  info.pEnabledFeatures = &features;
  vkCheck(vkCreateDevice(m_physical, &info, nullptr, &m_device), "vkCreateDevice");
  vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool.queueFamilyIndex = m_queueFamily;
  vkCheck(vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool), "vkCreateCommandPool");
}

void VulkanDevice::createFrameSync() {
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = m_commandPool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = kMaxFramesInFlight;
  vkCheck(vkAllocateCommandBuffers(m_device, &alloc, m_cmdBuffers.data()), "vkAllocateCommandBuffers");

  VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkCheck(vkCreateSemaphore(m_device, &sem, nullptr, &m_acquireSemaphores[i]), "vkCreateSemaphore");
    vkCheck(vkCreateSemaphore(m_device, &sem, nullptr, &m_renderSemaphores[i]), "vkCreateSemaphore");
    vkCheck(vkCreateFence(m_device, &fence, nullptr, &m_fences[i]), "vkCreateFence");
    m_cmdLists[i] = std::make_unique<VulkanCommandList>(this, m_cmdBuffers[i]);
  }
}

void VulkanDevice::destroyFrameSync() {
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    m_cmdLists[i].reset();
    if (m_acquireSemaphores[i]) {
      vkDestroySemaphore(m_device, m_acquireSemaphores[i], nullptr);
    }
    if (m_renderSemaphores[i]) {
      vkDestroySemaphore(m_device, m_renderSemaphores[i], nullptr);
    }
    if (m_fences[i]) {
      vkDestroyFence(m_device, m_fences[i], nullptr);
    }
  }
}

VkCommandBuffer VulkanDevice::currentCommandBuffer() const { return m_cmdBuffers[m_frameIndex]; }

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties mem{};
  vkGetPhysicalDeviceMemoryProperties(m_physical, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((typeFilter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("no matching Vulkan memory type");
}

std::unique_ptr<SwapChain> VulkanDevice::createSwapChain(void* nativeWindow, uint32_t width, uint32_t height,
                                                         bool vsync) {
  auto* window = static_cast<GLFWwindow*>(nativeWindow);
  if (!window) {
    throw std::runtime_error("Vulkan createSwapChain requires a GLFWwindow*");
  }
  auto sc = std::make_unique<VulkanSwapChain>(this, window, width, height, vsync);
  m_activeSwapChain = sc.get();
  return sc;
}

std::shared_ptr<Buffer> VulkanDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
  auto buf = std::make_shared<VulkanBuffer>();
  buf->owner = this;
  buf->device = m_device;
  buf->byteSize = desc.size;
  buf->usage = desc.usage;

  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = std::max<uint64_t>(desc.size, 1);
  info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo alloc{};
  alloc.usage = VMA_MEMORY_USAGE_AUTO;
  alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VmaAllocationInfo ainfo{};
  vkCheck(vmaCreateBuffer(m_allocator, &info, &alloc, &buf->buffer, &buf->allocation, &ainfo), "vmaCreateBuffer");
  buf->mappedPtr = ainfo.pMappedData;
  if (initialData && desc.size > 0) {
    std::memcpy(buf->mappedPtr, initialData, static_cast<size_t>(desc.size));
    vmaFlushAllocation(m_allocator, buf->allocation, 0, desc.size);
  }
  if (any(desc.usage, BufferUsage::Structured | BufferUsage::UnorderedAccess)) {
    buf->storageIndex = m_storageSlots.allocate(1);
    if (buf->storageIndex != UINT32_MAX) {
      buf->storageSize = info.size;
      writeStorageBuffer(buf->storageIndex, buf->buffer, info.size);
    } else {
      buf->storageIndex = 0;
    }
  }
  return buf;
}

std::shared_ptr<Texture> VulkanDevice::createTexture(const TextureDesc& desc, const void* initialData,
                                                     uint32_t rowPitch) {
  auto tex = std::make_shared<VulkanTexture>();
  tex->owner = this;
  tex->device = m_device;
  tex->ownsImage = true;
  tex->w = desc.width;
  tex->h = desc.height;
  tex->d = std::max(1u, desc.depth);
  tex->fmt = desc.format;
  tex->usage = desc.usage;
  tex->aspect = any(desc.usage, TextureUsage::DepthStencil) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                            : VK_IMAGE_ASPECT_COLOR_BIT;
  tex->is3D = desc.depth > 1;
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
  info.format = toVk(desc.format);
  info.extent = {desc.width, desc.height, std::max(1u, desc.depth)};
  info.mipLevels = std::max(1u, desc.mipLevels);
  info.arrayLayers = std::max(1u, desc.arraySize);
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (any(desc.usage, TextureUsage::RenderTarget)) {
    info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (any(desc.usage, TextureUsage::DepthStencil)) {
    info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if (any(desc.usage, TextureUsage::UnorderedAccess)) {
    info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  VmaAllocationCreateInfo alloc{};
  alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  vkCheck(vmaCreateImage(m_allocator, &info, &alloc, &tex->image, &tex->allocation, nullptr), "vmaCreateImage");
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = tex->image;
  view.viewType = desc.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D
                                 : (desc.arraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
  view.format = info.format;
  view.subresourceRange.aspectMask = tex->aspect;
  view.subresourceRange.levelCount = info.mipLevels;
  view.subresourceRange.layerCount = desc.depth > 1 ? 1u : info.arrayLayers;
  if (view.viewType == VK_IMAGE_VIEW_TYPE_2D) {
    view.subresourceRange.layerCount = 1;
  }
  vkCheck(vkCreateImageView(m_device, &view, nullptr, &tex->view), "vkCreateImageView");
  if (any(desc.usage, TextureUsage::UnorderedAccess)) {
    vkCheck(vkCreateImageView(m_device, &view, nullptr, &tex->storageView), "vkCreateImageView UAV");
  }
  VkImageLayout initial = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if (any(desc.usage, TextureUsage::DepthStencil)) {
    initial = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  } else if (any(desc.usage, TextureUsage::UnorderedAccess) &&
             !any(desc.usage, TextureUsage::ShaderResource)) {
    initial = VK_IMAGE_LAYOUT_GENERAL;
  } else if (any(desc.usage, TextureUsage::RenderTarget) && !any(desc.usage, TextureUsage::ShaderResource)) {
    initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  // UAV+SRV (Hi-Z, probe atlas, histogram) rest in SHADER_READ_ONLY so the bindless
  // sampled descriptor matches. UAV passes transition to GENERAL before dispatch.
  // Leaving them GENERAL while the descriptor said READ_ONLY GPUVM'd RADV (SSR Hi-Z).
  submitOneTime([&](VkCommandBuffer cmd) {
    imageBarrier(cmd, tex->image, VK_IMAGE_LAYOUT_UNDEFINED, initial, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 tex->aspect);
  });
  tex->layout = initial;
  if (any(desc.usage, TextureUsage::ShaderResource) || initialData) {
    if (desc.format != Format::D32_FLOAT && desc.format != Format::D24_UNORM_S8_UINT) {
      if (tex->is3D) {
        tex->srvIndex = m_sampled3DSlots.allocate(1);
        if (tex->srvIndex == UINT32_MAX) {
          throw std::runtime_error("bindless sampled3D heap exhausted");
        }
        writeSampled3D(tex->srvIndex, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      } else {
        tex->srvIndex = m_sampledSlots.allocate(1);
        if (tex->srvIndex == UINT32_MAX) {
          throw std::runtime_error("bindless sampled heap exhausted");
        }
        // Bindless SAMPLED_IMAGE layout must match the layout at sample time. The renderer
        // transitions to ShaderResource before sampling; UAV-capable targets (HDR) start in
        // GENERAL, and a GENERAL descriptor + SHADER_READ_ONLY image is undefined on RADV
        // (constant green framebuffer).
        writeSampled(tex->srvIndex, tex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
    }
  }
  if (initialData) {
    uploadTexture(*tex, initialData, desc.width, desc.height,
                  rowPitch ? rowPitch : formatRowPitch(desc.format, desc.width), 0, 0);
  }
  if (any(desc.usage, TextureUsage::UnorderedAccess)) {
    tex->uavIndex = m_nextUavIndex++;
    m_uavTextures[tex->uavIndex] = tex.get();
  }
  return tex;
}

static VkFilter toVkFilter(Filter f) {
  return f == Filter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}
static VkSamplerAddressMode toVkAddress(AddressMode m) {
  switch (m) {
  case AddressMode::Clamp:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case AddressMode::Mirror:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case AddressMode::Border:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  default:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

std::shared_ptr<Sampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
  auto s = std::make_shared<VulkanSampler>();
  s->owner = this;
  s->device = m_device;
  VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = toVkFilter(desc.filter);
  info.minFilter = toVkFilter(desc.filter);
  info.mipmapMode = desc.filter == Filter::Point ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.addressModeU = toVkAddress(desc.addressU);
  info.addressModeV = toVkAddress(desc.addressV);
  info.addressModeW = toVkAddress(desc.addressW);
  info.maxAnisotropy = desc.filter == Filter::Anisotropic ? desc.maxAnisotropy : 1.0f;
  info.anisotropyEnable = desc.filter == Filter::Anisotropic ? VK_TRUE : VK_FALSE;
  info.maxLod = VK_LOD_CLAMP_NONE;
  vkCheck(vkCreateSampler(m_device, &info, nullptr, &s->sampler), "vkCreateSampler");
  s->index = m_samplerSlots.allocate(1);
  if (s->index == UINT32_MAX) {
    s->index = 0;
  } else {
    writeSamplerSlot(s->index, s->sampler);
  }
  return s;
}

std::shared_ptr<RootSignature> VulkanDevice::createPipelineLayout(VkShaderStageFlags pushStages, bool compute) {
  auto rs = std::make_shared<VulkanRootSignature>();
  rs->device = m_device;
  rs->pushStages = pushStages;
  rs->isCompute = compute;
  VkPushConstantRange pc{};
  pc.stageFlags = pushStages;
  pc.size = kPushConstantBytes;
  VkDescriptorSetLayout layouts[kDescriptorSetCount] = {
      m_sampledLayout, m_samplerTableLayout, m_storageTableLayout, m_uboLayout, m_uavTableLayout,
      m_sampled3DLayout};
  VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  info.setLayoutCount = kDescriptorSetCount;
  info.pSetLayouts = layouts;
  info.pushConstantRangeCount = 1;
  info.pPushConstantRanges = &pc;
  vkCheck(vkCreatePipelineLayout(m_device, &info, nullptr, &rs->layout), "vkCreatePipelineLayout");
  return rs;
}

std::shared_ptr<RootSignature> VulkanDevice::createRootSignature(bool) {
  return createPipelineLayout(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);
}

std::shared_ptr<RootSignature> VulkanDevice::createComputeRootSignature() {
  return createPipelineLayout(VK_SHADER_STAGE_COMPUTE_BIT, true);
}

std::shared_ptr<PipelineState> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
  auto out = std::make_shared<VulkanPipelineState>();
  out->device = m_device;
  auto* rs = static_cast<VulkanRootSignature*>(desc.rootSignature.get());
  out->layout = rs->layout;

  VkShaderModuleCreateInfo vsInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  vsInfo.codeSize = desc.vs.data.size();
  vsInfo.pCode = reinterpret_cast<const uint32_t*>(desc.vs.data.data());
  VkShaderModule vs = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(m_device, &vsInfo, nullptr, &vs), "vkCreateShaderModule VS");

  VkShaderModuleCreateInfo psInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  psInfo.codeSize = desc.ps.data.size();
  psInfo.pCode = reinterpret_cast<const uint32_t*>(desc.ps.data.data());
  VkShaderModule ps = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(m_device, &psInfo, nullptr, &ps), "vkCreateShaderModule PS");

  const std::string vsEntry = spirvEntryPoint(desc.vs.data);
  const std::string psEntry = spirvEntryPoint(desc.ps.data);
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = vsEntry.c_str();
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = ps;
  stages[1].pName = psEntry.c_str();

  VkVertexInputBindingDescription binding{};
  binding.stride = 84;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[7]{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
  attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24};
  attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, 40};
  attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48};
  attrs[5] = {5, 0, VK_FORMAT_R8G8B8A8_UINT, 64};
  attrs[6] = {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 68};
  VkVertexInputAttributeDescription usedAttrs[7]{};
  uint32_t usedCount = 0;
  const uint32_t locMask = spirvVertexLocationMask(desc.vs.data);
  for (uint32_t i = 0; i < 7; ++i) {
    if (locMask & (1u << i)) {
      usedAttrs[usedCount++] = attrs[i];
    }
  }
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  if (desc.useInputLayout) {
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = usedCount;
    vi.pVertexAttributeDescriptions = usedAttrs;
  }

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = toVk(desc.topology);

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rsState{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rsState.polygonMode = desc.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  rsState.cullMode = toVk(desc.cullMode);
  rsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rsState.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = desc.depthEnable ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = toVk(desc.depthFunc);

  const uint32_t colorCount = desc.rtvFormats.empty() ? 1u : static_cast<uint32_t>(desc.rtvFormats.size());
  VkPipelineColorBlendAttachmentState blend = toVkBlend(desc.blend);
  std::vector<VkPipelineColorBlendAttachmentState> blends(colorCount, blend);
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = colorCount;
  cb.pAttachments = blends.data();

  VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dynStates;

  VkFormat colorFmts[kMaxColorAttachments]{};
  if (desc.rtvFormats.empty()) {
    colorFmts[0] = VK_FORMAT_R8G8B8A8_UNORM;
  } else {
    for (uint32_t i = 0; i < colorCount && i < kMaxColorAttachments; ++i) {
      colorFmts[i] = toVk(desc.rtvFormats[i]);
    }
  }
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = colorCount;
  rendering.pColorAttachmentFormats = colorFmts;
  if (desc.dsvFormat != Format::Unknown) {
    rendering.depthAttachmentFormat = toVk(desc.dsvFormat);
  }

  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.pNext = &rendering;
  gp.stageCount = 2;
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rsState;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dyn;
  gp.layout = rs->layout;
  vkCheck(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &out->pipeline),
          "vkCreateGraphicsPipelines");

  vkDestroyShaderModule(m_device, vs, nullptr);
  vkDestroyShaderModule(m_device, ps, nullptr);
  return out;
}

std::shared_ptr<PipelineState> VulkanDevice::createComputePipeline(const ComputePipelineDesc& desc) {
  auto out = std::make_shared<VulkanPipelineState>();
  out->device = m_device;
  out->isCompute = true;
  auto* rs = static_cast<VulkanRootSignature*>(desc.rootSignature.get());
  out->layout = rs ? rs->layout : VK_NULL_HANDLE;
  if (desc.cs.data.empty() || !rs) {
    return out;
  }
  VkShaderModuleCreateInfo csInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  csInfo.codeSize = desc.cs.data.size();
  csInfo.pCode = reinterpret_cast<const uint32_t*>(desc.cs.data.data());
  VkShaderModule cs = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(m_device, &csInfo, nullptr, &cs), "vkCreateShaderModule CS");
  const std::string csEntry = spirvEntryPoint(desc.cs.data);
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = cs;
  stage.pName = csEntry.c_str();
  VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  info.stage = stage;
  info.layout = rs->layout;
  vkCheck(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &out->pipeline),
          "vkCreateComputePipelines");
  vkDestroyShaderModule(m_device, cs, nullptr);
  return out;
}

CommandList* VulkanDevice::beginFrame() {
  vkCheck(vkWaitForFences(m_device, 1, &m_fences[m_frameIndex], VK_TRUE, UINT64_MAX), "vkWaitForFences");
  m_sampledSlots.flushDeferredFrees(m_frameNumber);
  m_sampled3DSlots.flushDeferredFrees(m_frameNumber);
  m_samplerSlots.flushDeferredFrees(m_frameNumber);
  m_storageSlots.flushDeferredFrees(m_frameNumber);
  resetFrameRings();
  vkCheck(vkResetFences(m_device, 1, &m_fences[m_frameIndex]), "vkResetFences");
  vkCheck(vkResetCommandBuffer(m_cmdBuffers[m_frameIndex], 0), "vkResetCommandBuffer");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkCheck(vkBeginCommandBuffer(m_cmdBuffers[m_frameIndex], &begin), "vkBeginCommandBuffer");
  m_cmdLists[m_frameIndex]->setCommandBuffer(m_cmdBuffers[m_frameIndex]);
  return m_cmdLists[m_frameIndex].get();
}

void VulkanDevice::endFrame(SwapChain& swapChain) {
  auto* cmd = m_cmdLists[m_frameIndex].get();
  cmd->endRenderingIfNeeded();
  vkCheck(vkEndCommandBuffer(m_cmdBuffers[m_frameIndex]), "vkEndCommandBuffer");

  VkSemaphore waitSem = m_acquireSemaphores[m_frameIndex];
  VkSemaphore signalSem = m_renderSemaphores[m_frameIndex];
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &waitSem;
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &m_cmdBuffers[m_frameIndex];
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &signalSem;
  vkCheck(vkQueueSubmit(m_queue, 1, &submit, m_fences[m_frameIndex]), "vkQueueSubmit");

  static std::atomic<uint32_t> loggedUsage{0};
  if (loggedUsage.fetch_add(1) == 0) {
    const uint32_t usedSets = m_uboSetCursor - m_frameIndex * kUboSetsPerFrame;
    const uint32_t usedSlots = m_pushUboCursor - m_frameIndex * kPushUboSlotsPerFrame;
    std::cout << "[Vulkan] UBO frame usage sets=" << usedSets << "/" << kUboSetsPerFrame
              << " slots=" << usedSlots << "/" << kPushUboSlotsPerFrame << "\n";
  }

  swapChain.present();
  m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
  ++m_frameNumber;
}

void VulkanDevice::waitIdle() {
  if (m_device) {
    vkDeviceWaitIdle(m_device);
  }
}

void VulkanDevice::submitAndWaitHeadless() {
  auto* cmd = m_cmdLists[m_frameIndex].get();
  cmd->endRenderingIfNeeded();
  vkCheck(vkEndCommandBuffer(m_cmdBuffers[m_frameIndex]), "vkEndCommandBuffer headless");
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &m_cmdBuffers[m_frameIndex];
  vkCheck(vkQueueSubmit(m_queue, 1, &submit, m_fences[m_frameIndex]), "vkQueueSubmit headless");
  vkCheck(vkWaitForFences(m_device, 1, &m_fences[m_frameIndex], VK_TRUE, UINT64_MAX),
          "vkWaitForFences headless");
}

void VulkanDevice::uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset) {
  auto& buf = static_cast<VulkanBuffer&>(buffer);
  if (!buf.mappedPtr || !data || offset + size > buf.byteSize) {
    return;
  }
  std::memcpy(static_cast<uint8_t*>(buf.mappedPtr) + offset, data, static_cast<size_t>(size));
}

void VulkanDevice::uploadTexture(Texture& texture, const void* data, uint32_t width, uint32_t height,
                                 uint32_t rowPitch, uint32_t mip, uint32_t arraySlice) {
  if (!data || width == 0 || height == 0) {
    return;
  }
  auto& tex = static_cast<VulkanTexture&>(texture);
  const uint32_t stride = formatStride(tex.fmt);
  const uint32_t tightPitch = formatRowPitch(tex.fmt, width);
  const uint32_t pitch = rowPitch ? rowPitch : tightPitch;
  const uint32_t depth = tex.is3D ? std::max(1u, tex.d) : 1u;
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(pitch) * height * depth;
  VkBufferCreateInfo binfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  binfo.size = bytes;
  binfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VmaAllocationCreateInfo alloc{};
  alloc.usage = VMA_MEMORY_USAGE_AUTO;
  alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  VkBuffer staging = VK_NULL_HANDLE;
  VmaAllocation stagingAlloc = VK_NULL_HANDLE;
  VmaAllocationInfo ainfo{};
  vkCheck(vmaCreateBuffer(m_allocator, &binfo, &alloc, &staging, &stagingAlloc, &ainfo), "vmaCreateBuffer staging");
  std::memcpy(ainfo.pMappedData, data, static_cast<size_t>(bytes));
  submitOneTime([&](VkCommandBuffer cmd) {
    imageBarrier(cmd, tex.image, tex.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, tex.aspect);
    VkBufferImageCopy copy{};
    copy.bufferRowLength = stride ? pitch / stride : width;
    copy.bufferImageHeight = height;
    copy.imageSubresource.aspectMask = tex.aspect;
    copy.imageSubresource.mipLevel = mip;
    copy.imageSubresource.baseArrayLayer = arraySlice;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, depth};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    imageBarrier(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, tex.aspect);
  });
  tex.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  tex.state = ResourceState::ShaderResource;
  vmaDestroyBuffer(m_allocator, staging, stagingAlloc);
}

void VulkanDevice::createAllocator() {
  VmaAllocatorCreateInfo info{};
  info.physicalDevice = m_physical;
  info.device = m_device;
  info.instance = m_instance;
  info.vulkanApiVersion = VK_API_VERSION_1_3;
  vkCheck(vmaCreateAllocator(&info, &m_allocator), "vmaCreateAllocator");
}

VkDescriptorSetLayout VulkanDevice::makeBindlessLayout(VkDescriptorType type, uint32_t count,
                                                       VkShaderStageFlags stages) {
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = type;
  binding.descriptorCount = count;
  binding.stageFlags = stages;
  VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                   VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                   VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  flagInfo.bindingCount = 1;
  flagInfo.pBindingFlags = &flags;
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.pNext = &flagInfo;
  info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  info.bindingCount = 1;
  info.pBindings = &binding;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  vkCheck(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &layout), "vkCreateDescriptorSetLayout bindless");
  return layout;
}

VkDescriptorSetLayout VulkanDevice::makeTableLayout(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages) {
  std::vector<VkDescriptorSetLayoutBinding> bindings(count);
  std::vector<VkDescriptorBindingFlags> flags(count, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                                         VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                         VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);
  for (uint32_t i = 0; i < count; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = type;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = stages;
  }
  VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  flagInfo.bindingCount = count;
  flagInfo.pBindingFlags = flags.data();
  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.pNext = &flagInfo;
  info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  info.bindingCount = count;
  info.pBindings = bindings.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  vkCheck(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &layout), "vkCreateDescriptorSetLayout table");
  return layout;
}

void VulkanDevice::createBindless() {
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(m_physical, &props);
  VkPhysicalDeviceDescriptorIndexingProperties indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
  VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props2.pNext = &indexing;
  vkGetPhysicalDeviceProperties2(m_physical, &props2);
  auto clamp = [](uint32_t want, uint32_t limit) {
    return limit == 0 ? want : std::min(want, limit);
  };
  m_sampledCount = clamp(kBindlessSampled, indexing.maxDescriptorSetUpdateAfterBindSampledImages);
  m_samplerCount = clamp(kBindlessSamplers, indexing.maxDescriptorSetUpdateAfterBindSamplers);
  m_storageCount = clamp(kBindlessStorage, indexing.maxDescriptorSetUpdateAfterBindStorageBuffers);
  std::cerr << "[Vulkan] bindless sampled=" << m_sampledCount << " samplers=" << m_samplerCount
            << " storage=" << m_storageCount << "\n";

  m_sampledSlots.init(m_sampledCount);
  m_sampled3DSlots.init(m_sampled3DCount);
  m_samplerSlots.init(m_samplerCount);
  m_storageSlots.init(m_storageCount);

  const VkShaderStageFlags allGfx = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                    VK_SHADER_STAGE_COMPUTE_BIT;
  m_sampledLayout = makeBindlessLayout(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_sampledCount, allGfx);
  m_sampled3DLayout = makeBindlessLayout(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_sampled3DCount, allGfx);
  m_storageLayout = makeBindlessLayout(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_storageCount, allGfx);
  m_storageTableLayout = makeTableLayout(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kStorageTableSlots, allGfx);
  m_uavTableLayout = makeTableLayout(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kUavTableSlots, allGfx);
  m_samplerTableLayout = makeTableLayout(VK_DESCRIPTOR_TYPE_SAMPLER, kSamplerTableSlots, allGfx);
  m_uboLayout = makeTableLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kUboBindings, allGfx);
  m_samplerLayout = m_samplerTableLayout;

  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_sampledCount + m_sampled3DCount},
      {VK_DESCRIPTOR_TYPE_SAMPLER, kTransientTables * kSamplerTableSlots + kSamplerTableSlots},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_storageCount + kTransientTables * kStorageTableSlots},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kTransientTables * kUavTableSlots},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kUboBindings * kUboSetRing},
  };
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool.maxSets = 16 + kTransientTables * 3 + kUboSetRing;
  pool.poolSizeCount = 5;
  pool.pPoolSizes = sizes;
  vkCheck(vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descPool), "vkCreateDescriptorPool");

  auto allocSet = [&](VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    vkCheck(vkAllocateDescriptorSets(m_device, &ai, &set), "vkAllocateDescriptorSets");
    return set;
  };
  m_sampledSet = allocSet(m_sampledLayout);
  m_sampled3DSet = allocSet(m_sampled3DLayout);
  m_samplerSet = allocSet(m_samplerTableLayout);
  m_storageSet = allocSet(m_storageLayout);
  for (uint32_t i = 0; i < kUboSetRing; ++i) {
    m_uboSets[i] = allocSet(m_uboLayout);
  }
  m_uboSet = m_uboSets[0];
  for (uint32_t i = 0; i < kTransientTables; ++i) {
    m_uavTables[i] = allocSet(m_uavTableLayout);
    m_samplerTables[i] = allocSet(m_samplerTableLayout);
    m_storageTables[i] = allocSet(m_storageTableLayout);
  }
  createDummyResources();
}

void VulkanDevice::createDummyResources() {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.extent = {1, 1, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VmaAllocationCreateInfo alloc{};
  alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  vkCheck(vmaCreateImage(m_allocator, &info, &alloc, &m_dummyImage, &m_dummyAlloc, nullptr), "dummy image");
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = m_dummyImage;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = info.format;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.layerCount = 1;
  vkCheck(vkCreateImageView(m_device, &view, nullptr, &m_dummyView), "dummy view");
  VkSamplerCreateInfo samp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samp.magFilter = VK_FILTER_LINEAR;
  samp.minFilter = VK_FILTER_LINEAR;
  samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samp.addressModeU = samp.addressModeV = samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samp.maxLod = VK_LOD_CLAMP_NONE;
  vkCheck(vkCreateSampler(m_device, &samp, nullptr, &m_dummySampler), "dummy sampler");
  submitOneTime([&](VkCommandBuffer cmd) {
    imageBarrier(cmd, m_dummyImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue clear{};
    clear.float32[3] = 1.0f;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, m_dummyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    imageBarrier(cmd, m_dummyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
  });
  writeSampled(0, m_dummyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  for (uint32_t i = 1; i < m_sampledCount; ++i) {
    writeSampled(i, m_dummyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  {
    info.imageType = VK_IMAGE_TYPE_3D;
    vkCheck(vmaCreateImage(m_allocator, &info, &alloc, &m_dummyImage3D, &m_dummyAlloc3D, nullptr), "dummy 3D image");
    view.image = m_dummyImage3D;
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vkCheck(vkCreateImageView(m_device, &view, nullptr, &m_dummyView3D), "dummy 3D view");
    submitOneTime([&](VkCommandBuffer cmd) {
      imageBarrier(cmd, m_dummyImage3D, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                   VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
    });
    writeSampled3D(0, m_dummyView3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t i = 1; i < m_sampled3DCount; ++i) {
      writeSampled3D(i, m_dummyView3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }
  {
    VkBufferCreateInfo binfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    binfo.size = 256;
    binfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaAllocationCreateInfo balloc{};
    balloc.usage = VMA_MEMORY_USAGE_AUTO;
    balloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    balloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vkCheck(vmaCreateBuffer(m_allocator, &binfo, &balloc, &m_dummyUbo, &m_dummyUboAlloc, nullptr), "dummy UBO");
    binfo.size = uint64_t(kPushUboStride) * kPushUboSlots;
    VmaAllocationInfo ainfo{};
    vkCheck(vmaCreateBuffer(m_allocator, &binfo, &balloc, &m_pushUbo, &m_pushUboAlloc, &ainfo), "push UBO ring");
    m_pushUboMapped = ainfo.pMappedData;
    for (uint32_t b = 0; b < kUboBindings; ++b) {
      m_uboCache[b] = {m_dummyUbo, 0, 256};
    }
    writeUbo(0, m_dummyUbo, 0, 256);
  }
  {
    VkDescriptorImageInfo img{m_dummySampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = m_samplerSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  writeStorageBuffer(0, VK_NULL_HANDLE, 0);
  for (uint32_t t = 0; t < kTransientTables; ++t) {
    for (uint32_t b = 0; b < kUavTableSlots; ++b) {
      VkDescriptorImageInfo img{VK_NULL_HANDLE, m_dummyView, VK_IMAGE_LAYOUT_GENERAL};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = m_uavTables[t];
      w.dstBinding = b;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      w.pImageInfo = &img;
      vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
    for (uint32_t b = 0; b < kSamplerTableSlots; ++b) {
      VkDescriptorImageInfo img{m_dummySampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = m_samplerTables[t];
      w.dstBinding = b;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
      w.pImageInfo = &img;
      vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
    for (uint32_t b = 0; b < kStorageTableSlots; ++b) {
      VkDescriptorBufferInfo buf{m_dummyUbo, 0, 256};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = m_storageTables[t];
      w.dstBinding = b;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w.pBufferInfo = &buf;
      vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
  }
}

void VulkanDevice::destroyBindless() {
  if (m_dummyView3D) {
    vkDestroyImageView(m_device, m_dummyView3D, nullptr);
    m_dummyView3D = VK_NULL_HANDLE;
  }
  if (m_dummyImage3D) {
    vmaDestroyImage(m_allocator, m_dummyImage3D, m_dummyAlloc3D);
    m_dummyImage3D = VK_NULL_HANDLE;
    m_dummyAlloc3D = VK_NULL_HANDLE;
  }
  if (m_dummyView) {
    vkDestroyImageView(m_device, m_dummyView, nullptr);
    m_dummyView = VK_NULL_HANDLE;
  }
  if (m_dummyImage) {
    vmaDestroyImage(m_allocator, m_dummyImage, m_dummyAlloc);
    m_dummyImage = VK_NULL_HANDLE;
    m_dummyAlloc = VK_NULL_HANDLE;
  }
  if (m_dummySampler) {
    vkDestroySampler(m_device, m_dummySampler, nullptr);
    m_dummySampler = VK_NULL_HANDLE;
  }
  if (m_pushUbo) {
    vmaDestroyBuffer(m_allocator, m_pushUbo, m_pushUboAlloc);
    m_pushUbo = VK_NULL_HANDLE;
    m_pushUboAlloc = VK_NULL_HANDLE;
    m_pushUboMapped = nullptr;
  }
  if (m_dummyUbo) {
    vmaDestroyBuffer(m_allocator, m_dummyUbo, m_dummyUboAlloc);
    m_dummyUbo = VK_NULL_HANDLE;
    m_dummyUboAlloc = VK_NULL_HANDLE;
  }
  if (m_descPool) {
    vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    m_descPool = VK_NULL_HANDLE;
  }
  auto destroyLayout = [&](VkDescriptorSetLayout& l) {
    if (l) {
      vkDestroyDescriptorSetLayout(m_device, l, nullptr);
      l = VK_NULL_HANDLE;
    }
  };
  destroyLayout(m_sampledLayout);
  destroyLayout(m_sampled3DLayout);
  destroyLayout(m_storageLayout);
  destroyLayout(m_storageTableLayout);
  destroyLayout(m_uavTableLayout);
  destroyLayout(m_samplerTableLayout);
  destroyLayout(m_uboLayout);
  m_samplerLayout = VK_NULL_HANDLE;
}

void VulkanDevice::submitOneTime(const std::function<void(VkCommandBuffer)>& record) {
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = m_commandPool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkCheck(vkAllocateCommandBuffers(m_device, &alloc, &cmd), "vkAllocateCommandBuffers one-time");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkCheck(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer one-time");
  record(cmd);
  vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer one-time");
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkCheck(vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit one-time");
  vkDeviceWaitIdle(m_device);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

void VulkanDevice::writeSampled(uint32_t slot, VkImageView view, VkImageLayout layout) {
  VkDescriptorImageInfo img{VK_NULL_HANDLE, view, layout};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = m_sampledSet;
  w.dstBinding = 0;
  w.dstArrayElement = slot;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  w.pImageInfo = &img;
  vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void VulkanDevice::writeSampled3D(uint32_t slot, VkImageView view, VkImageLayout layout) {
  VkDescriptorImageInfo img{VK_NULL_HANDLE, view, layout};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = m_sampled3DSet;
  w.dstBinding = 0;
  w.dstArrayElement = slot;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  w.pImageInfo = &img;
  vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void VulkanDevice::resetFrameRings() {
  m_uboSetCursor = m_frameIndex * kUboSetsPerFrame;
  m_pushUboCursor = m_frameIndex * kPushUboSlotsPerFrame;
  m_uavTableCursor = m_frameIndex * kTablesPerFrame;
  m_samplerTableCursor = m_frameIndex * kTablesPerFrame;
  m_storageTableCursor = m_frameIndex * kTablesPerFrame;
  m_boundSamplerSet = m_samplerSet;
  m_boundStorageSet = m_storageTables[m_frameIndex * kTablesPerFrame];
  m_uboSet = m_uboSets[m_frameIndex * kUboSetsPerFrame];
}

uint32_t VulkanDevice::takeFrameRingSlot(uint32_t& cursor, uint32_t perFrame, uint32_t ringSize,
                                         const char* name) {
  const uint32_t base = m_frameIndex * perFrame;
  const uint32_t end = std::min(base + perFrame, ringSize);
  if (cursor < base) {
    cursor = base;
  }
  if (cursor >= end) {
    static std::atomic<uint32_t> logged{0};
    if (logged.fetch_add(1) == 0) {
      std::cerr << "[Vulkan] " << name << " ring exhausted this frame (" << perFrame
                << " slots). Increase the ring; wrapping would GPUVM on RADV.\n";
    }
    return end - 1;
  }
  return cursor++;
}

void VulkanDevice::writeUbo(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
  if (binding >= kUboBindings || !buffer) {
    return;
  }
  m_uboCache[binding] = {buffer, offset, range};
  // Fresh set each update. Rebinding the same VkDescriptorSet after vkUpdateDescriptorSets
  // is a no-op on RADV: the dummy UBO from setPipeline stays bound and texIds read as 0.
  const uint32_t idx = takeFrameRingSlot(m_uboSetCursor, kUboSetsPerFrame, kUboSetRing, "UBO descriptor");
  m_uboSet = m_uboSets[idx];
  VkDescriptorBufferInfo infos[kUboBindings];
  VkWriteDescriptorSet writes[kUboBindings];
  for (uint32_t b = 0; b < kUboBindings; ++b) {
    const UboBinding& c = m_uboCache[b];
    const VkBuffer src = c.buffer ? c.buffer : m_dummyUbo;
    infos[b] = {src, c.buffer ? c.offset : 0, c.buffer ? c.range : 256};
    writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[b].dstSet = m_uboSet;
    writes[b].dstBinding = b;
    writes[b].descriptorCount = 1;
    writes[b].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[b].pBufferInfo = &infos[b];
  }
  vkUpdateDescriptorSets(m_device, kUboBindings, writes, 0, nullptr);
}

void VulkanDevice::writeRingUbo(uint32_t binding, const void* data, uint32_t bytes) {
  if (!m_pushUboMapped || !data || bytes == 0 || binding >= kUboBindings) {
    return;
  }
  const uint32_t slot =
      takeFrameRingSlot(m_pushUboCursor, kPushUboSlotsPerFrame, kPushUboSlots, "UBO bytes");
  const uint32_t copy = std::min(bytes, kPushUboStride);
  uint8_t* dst = static_cast<uint8_t*>(m_pushUboMapped) + slot * kPushUboStride;
  std::memset(dst, 0, kPushUboStride);
  std::memcpy(dst, data, copy);
  vmaFlushAllocation(m_allocator, m_pushUboAlloc, uint64_t(slot) * kPushUboStride, kPushUboStride);
  writeUbo(binding, m_pushUbo, uint64_t(slot) * kPushUboStride, kPushUboStride);
}

void VulkanDevice::writePushUbo(const void* data, uint32_t bytes) {
  writeRingUbo(0, data, bytes);
}

void VulkanDevice::writeSamplerSlot(uint32_t slot, VkSampler sampler) {
  if (slot >= kSamplerTableSlots) {
    return;
  }
  VkDescriptorImageInfo img{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = m_samplerSet;
  w.dstBinding = slot;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  w.pImageInfo = &img;
  vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void VulkanDevice::writeStorageBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize size) {
  if (!buffer) {
    return;
  }
  VkDescriptorBufferInfo buf{buffer, 0, size};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = m_storageSet;
  w.dstBinding = 0;
  w.dstArrayElement = slot;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  w.pBufferInfo = &buf;
  vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void VulkanDevice::bindGraphicsHeaps(VkCommandBuffer cmd, VkPipelineLayout layout) {
  if (!m_boundSamplerSet) {
    m_boundSamplerSet = m_samplerSet;
  }
  if (!m_boundStorageSet) {
    m_boundStorageSet = m_storageTables[0];
  }
  // Keep the last sampler/storage tables. Rebinding m_samplerSet here used to undo
  // setGraphicsRootSamplerTable after every CBV (GBuffer draws sampled with the dummy).
  VkDescriptorSet sets[] = {m_sampledSet, m_boundSamplerSet, m_boundStorageSet, m_uboSet,
                            m_uavTables[m_frameIndex * kTablesPerFrame], m_sampled3DSet};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, kDescriptorSetCount, sets, 0, nullptr);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, kDescriptorSetCount, sets, 0, nullptr);
}

void VulkanDevice::bindSamplerTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId) {
  VkDescriptorSet set = tableId < kTransientTables ? m_samplerTables[tableId] : m_samplerSet;
  m_boundSamplerSet = set;
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, kSetSampler, 1, &set, 0, nullptr);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, kSetSampler, 1, &set, 0, nullptr);
}

void VulkanDevice::bindStorageTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId) {
  VkDescriptorSet set = tableId < kTransientTables ? m_storageTables[tableId]
                                                   : m_storageTables[m_frameIndex * kTablesPerFrame];
  m_boundStorageSet = set;
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, kSetStorage, 1, &set, 0, nullptr);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, kSetStorage, 1, &set, 0, nullptr);
}

void VulkanDevice::bindComputeUavTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId) {
  if (tableId >= kUavBindlessBase) {
    auto it = m_uavTextures.find(tableId);
    if (it != m_uavTextures.end() && it->second) {
      ResourceView views[] = {ResourceView::uav(*it->second)};
      tableId = writeUavTable(views);
    } else {
      tableId = 0;
    }
  }
  VkDescriptorSet set = tableId < kTransientTables ? m_uavTables[tableId] : m_uavTables[0];
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, kSetUav, 1, &set, 0, nullptr);
}

uint32_t VulkanDevice::writeStorageTable(std::span<const ResourceView> views) {
  const uint32_t id =
      takeFrameRingSlot(m_storageTableCursor, kTablesPerFrame, kTransientTables, "storage table");
  VkDescriptorSet set = m_storageTables[id];
  const uint32_t n = static_cast<uint32_t>(std::min(views.size(), static_cast<size_t>(kStorageTableSlots)));
  for (uint32_t i = 0; i < kStorageTableSlots; ++i) {
    VkBuffer buffer = m_dummyUbo;
    VkDeviceSize size = 256;
    if (i < n && views[i].buffer) {
      auto* b = static_cast<VulkanBuffer*>(views[i].buffer);
      buffer = b->buffer;
      size = b->byteSize;
    }
    VkDescriptorBufferInfo buf{buffer, 0, size};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = i;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &buf;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  return id;
}

uint32_t VulkanDevice::writeUavTable(std::span<const ResourceView> views) {
  const uint32_t id =
      takeFrameRingSlot(m_uavTableCursor, kTablesPerFrame, kTransientTables, "UAV table");
  VkDescriptorSet set = m_uavTables[id];
  const uint32_t n = static_cast<uint32_t>(std::min(views.size(), static_cast<size_t>(kUavTableSlots)));
  bool any3D = false;
  for (uint32_t i = 0; i < n; ++i) {
    if (views[i].texture && static_cast<VulkanTexture*>(views[i].texture)->is3D) {
      any3D = true;
      break;
    }
  }
  for (uint32_t i = 0; i < kUavTableSlots; ++i) {
    VkImageView view = any3D ? m_dummyView3D : m_dummyView;
    if (i < n) {
      const ResourceView& v = views[i];
      if (v.kind == ResourceView::Kind::TextureUav && v.texture) {
        auto* t = static_cast<VulkanTexture*>(v.texture);
        view = t->storageView ? t->storageView : t->view;
      } else if (v.kind == ResourceView::Kind::TextureSrv && v.texture) {
        view = static_cast<VulkanTexture*>(v.texture)->view;
      }
    }
    VkDescriptorImageInfo img{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = i;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  return id;
}

uint32_t VulkanDevice::writeResourceTable(std::span<const ResourceView> views) {
  for (const ResourceView& v : views) {
    if (v.kind == ResourceView::Kind::BufferSrv || v.kind == ResourceView::Kind::BufferUav) {
      return writeStorageTable(views);
    }
  }
  return writeUavTable(views);
}

uint32_t VulkanDevice::writeSamplerTable(std::span<Sampler* const> samplers) {
  const uint32_t id =
      takeFrameRingSlot(m_samplerTableCursor, kTablesPerFrame, kTransientTables, "sampler table");
  VkDescriptorSet set = m_samplerTables[id];
  const uint32_t n = static_cast<uint32_t>(std::min(samplers.size(), static_cast<size_t>(kSamplerTableSlots)));
  for (uint32_t i = 0; i < kSamplerTableSlots; ++i) {
    VkSampler samp = m_dummySampler;
    if (i < n && samplers[i]) {
      samp = static_cast<VulkanSampler*>(samplers[i])->sampler;
    }
    VkDescriptorImageInfo img{samp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = i;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
  }
  return id;
}

void VulkanDevice::releaseTexture(VulkanTexture& tex) {
  if (tex.uavIndex >= kUavBindlessBase) {
    m_uavTextures.erase(tex.uavIndex);
    tex.uavIndex = 0;
  }
  if (tex.srvIndex != 0) {
    if (tex.is3D) {
      writeSampled3D(tex.srvIndex, m_dummyView3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_sampled3DSlots.deferFree(tex.srvIndex, 1, m_frameNumber + kMaxFramesInFlight);
    } else {
      writeSampled(tex.srvIndex, m_dummyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_sampledSlots.deferFree(tex.srvIndex, 1, m_frameNumber + kMaxFramesInFlight);
    }
    tex.srvIndex = 0;
  }
  if (tex.storageView) {
    vkDestroyImageView(m_device, tex.storageView, nullptr);
    tex.storageView = VK_NULL_HANDLE;
  }
  if (tex.view) {
    vkDestroyImageView(m_device, tex.view, nullptr);
    tex.view = VK_NULL_HANDLE;
  }
  if (tex.ownsImage && tex.image) {
    vmaDestroyImage(m_allocator, tex.image, tex.allocation);
    tex.image = VK_NULL_HANDLE;
    tex.allocation = VK_NULL_HANDLE;
  }
  tex.owner = nullptr;
}

void VulkanDevice::releaseBuffer(VulkanBuffer& buf) {
  if (buf.storageIndex != 0) {
    m_storageSlots.deferFree(buf.storageIndex, 1, m_frameNumber + kMaxFramesInFlight);
    buf.storageIndex = 0;
  }
  if (buf.buffer) {
    vmaDestroyBuffer(m_allocator, buf.buffer, buf.allocation);
    buf.buffer = VK_NULL_HANDLE;
    buf.allocation = VK_NULL_HANDLE;
    buf.mappedPtr = nullptr;
  }
  buf.owner = nullptr;
}

void VulkanDevice::releaseSampler(VulkanSampler& samp) {
  if (samp.index != 0) {
    writeSamplerSlot(samp.index, m_dummySampler);
    m_samplerSlots.deferFree(samp.index, 1, m_frameNumber + kMaxFramesInFlight);
    samp.index = 0;
  }
  if (samp.sampler) {
    vkDestroySampler(m_device, samp.sampler, nullptr);
    samp.sampler = VK_NULL_HANDLE;
  }
  samp.owner = nullptr;
}

VulkanTexture::~VulkanTexture() {
  if (owner) {
    owner->releaseTexture(*this);
    return;
  }
  if (device && view) {
    vkDestroyImageView(device, view, nullptr);
    view = VK_NULL_HANDLE;
  }
}

VulkanBuffer::~VulkanBuffer() {
  if (owner) {
    owner->releaseBuffer(*this);
  }
}

VulkanSampler::~VulkanSampler() {
  if (owner) {
    owner->releaseSampler(*this);
  }
}

} // namespace tucano::rhi
