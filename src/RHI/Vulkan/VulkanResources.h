#pragma once

#include "RHI/Vulkan/VulkanCommon.h"

#include <vk_mem_alloc.h>

namespace tucano::rhi {

class VulkanDevice;

inline constexpr uint32_t kBindlessSampled = 8192;
inline constexpr uint32_t kBindlessSampled3D = 256;
inline constexpr uint32_t kBindlessSamplers = 256;
inline constexpr uint32_t kBindlessStorage = 2048;
inline constexpr uint32_t kUavTableSlots = 8;
inline constexpr uint32_t kSamplerTableSlots = 8;
inline constexpr uint32_t kStorageTableSlots = 16;
inline constexpr uint32_t kUboBindings = 3;
inline constexpr uint32_t kTransientTables = 512;
// bindlessUavIndex() lives above the transient UAV table ring (0..kTransientTables-1).
inline constexpr uint32_t kUavBindlessBase = 1024;
inline constexpr uint32_t kPushConstantBytes = 128;
inline constexpr uint32_t kMaxColorAttachments = 8;
// LightingCB is ~784 B and LightCB ~1.6 KiB; 256 B truncated texIds (offset 464) to 0.
inline constexpr uint32_t kPushUboStride = 2048;
inline constexpr uint32_t kPushUboSlots = 8192;
inline constexpr uint32_t kUboSetRing = 4096;
static_assert(kUboSetRing % kMaxFramesInFlight == 0);
static_assert(kPushUboSlots % kMaxFramesInFlight == 0);
static_assert(kTransientTables % kMaxFramesInFlight == 0);
// beginFrame waits on this FIF slot, so these slices are GPU-idle. A global
// modulo wrap reused sets still recorded in the current command buffer
// (illegal without UPDATE_AFTER_BIND) and could GPUVM on RADV.
inline constexpr uint32_t kUboSetsPerFrame = kUboSetRing / kMaxFramesInFlight;
inline constexpr uint32_t kPushUboSlotsPerFrame = kPushUboSlots / kMaxFramesInFlight;
inline constexpr uint32_t kTablesPerFrame = kTransientTables / kMaxFramesInFlight;

inline constexpr uint32_t kSetSampled = 0;
inline constexpr uint32_t kSetSampler = 1;
inline constexpr uint32_t kSetStorage = 2;
inline constexpr uint32_t kSetUbo = 3;
inline constexpr uint32_t kSetUav = 4;
inline constexpr uint32_t kSetSampled3D = 5;
inline constexpr uint32_t kDescriptorSetCount = 6;

struct VulkanBuffer final : Buffer {
  VulkanDevice* owner = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  uint64_t byteSize = 0;
  void* mappedPtr = nullptr;
  uint32_t storageIndex = 0;
  VkDeviceSize storageSize = 0;

  ~VulkanBuffer() override;

  uint64_t size() const override { return byteSize; }
  void* mapped() override { return mappedPtr; }
  uint32_t srvIndex() const override { return storageIndex; }
  uint32_t uavIndex() const override { return storageIndex; }
};

struct VulkanTexture final : Texture {
  VulkanDevice* owner = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkImageView storageView = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  bool ownsImage = false;
  uint32_t w = 1;
  uint32_t h = 1;
  uint32_t d = 1;
  Format fmt = Format::R8G8B8A8_UNORM;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  uint32_t srvIndex = 0;
  uint32_t uavIndex = 0;
  bool is3D = false;

  ~VulkanTexture() override;

  uint32_t width() const override { return w; }
  uint32_t height() const override { return h; }
  Format format() const override { return fmt; }
  uint32_t bindlessIndex() const override { return srvIndex; }
  uint32_t bindlessUavIndex() const override { return uavIndex; }
};

struct VulkanSampler final : Sampler {
  VulkanDevice* owner = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  uint32_t index = 0;
  ~VulkanSampler() override;
};

struct VulkanRootSignature final : RootSignature {
  VkDevice device = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bool isCompute = false;
  ~VulkanRootSignature() override {
    if (device && layout) {
      vkDestroyPipelineLayout(device, layout, nullptr);
    }
  }
};

struct VulkanPipelineState final : PipelineState {
  VkDevice device = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  bool isCompute = false;
  ~VulkanPipelineState() override {
    if (device && pipeline) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
  }
};

} // namespace tucano::rhi
