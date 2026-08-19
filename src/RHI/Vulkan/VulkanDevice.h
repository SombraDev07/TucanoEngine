#pragma once

#include "RHI/BindlessManager.h"
#include "RHI/Vulkan/VulkanResources.h"

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace tucano::rhi {

class VulkanSwapChain;
class VulkanCommandList;

class VulkanDevice final : public Device {
public:
  explicit VulkanDevice(bool enableDebugLayer);
  ~VulkanDevice() override;

  std::unique_ptr<SwapChain> createSwapChain(void* nativeWindow, uint32_t width, uint32_t height,
                                             bool vsync) override;
  std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initialData) override;
  std::shared_ptr<Texture> createTexture(const TextureDesc& desc, const void* initialData,
                                         uint32_t rowPitch) override;
  std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) override;
  std::shared_ptr<RootSignature> createRootSignature(bool allowInputLayout) override;
  std::shared_ptr<RootSignature> createComputeRootSignature() override;
  std::shared_ptr<PipelineState> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
  std::shared_ptr<PipelineState> createComputePipeline(const ComputePipelineDesc& desc) override;

  CommandList* beginFrame() override;
  void endFrame(SwapChain& swapChain) override;
  void waitIdle() override;
  void submitAndWaitHeadless() override;
  uint32_t frameIndex() const override { return m_frameIndex; }
  uint64_t frameFenceValue() const override { return m_frameNumber; }

  void uploadBuffer(Buffer& buffer, const void* data, uint64_t size, uint64_t offset) override;
  void uploadTexture(Texture& texture, const void* data, uint32_t width, uint32_t height, uint32_t rowPitch,
                     uint32_t mip, uint32_t arraySlice) override;

  uint32_t writeResourceTable(std::span<const ResourceView> views) override;
  uint32_t writeSamplerTable(std::span<Sampler* const> samplers) override;

  void* nativeDevice() const override { return m_device; }

  VkInstance instance() const { return m_instance; }
  VkPhysicalDevice physicalDevice() const { return m_physical; }
  VkDevice device() const { return m_device; }
  VkQueue graphicsQueue() const { return m_queue; }
  uint32_t graphicsQueueFamily() const { return m_queueFamily; }
  VkCommandBuffer currentCommandBuffer() const;
  VmaAllocator allocator() const { return m_allocator; }

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

  VulkanSwapChain* activeSwapChain() { return m_activeSwapChain; }
  void setActiveSwapChain(VulkanSwapChain* sc) { m_activeSwapChain = sc; }

  VkSemaphore acquireSemaphore() const { return m_acquireSemaphores[m_frameIndex]; }
  VkSemaphore renderSemaphore() const { return m_renderSemaphores[m_frameIndex]; }
  VkFence inFlightFence() const { return m_fences[m_frameIndex]; }

  void bindGraphicsHeaps(VkCommandBuffer cmd, VkPipelineLayout layout);
  void bindSamplerTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId);
  void bindStorageTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId);
  void bindComputeUavTable(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t tableId);
  void writeUbo(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);
  void writePushUbo(const void* data, uint32_t bytes);
  void writeRingUbo(uint32_t binding, const void* data, uint32_t bytes);

  void releaseTexture(VulkanTexture& tex);
  void releaseBuffer(VulkanBuffer& buf);
  void releaseSampler(VulkanSampler& samp);

private:
  void createInstance(bool enableDebug);
  void pickDevice();
  void createLogicalDevice();
  void createAllocator();
  void createBindless();
  void destroyBindless();
  void createFrameSync();
  void destroyFrameSync();
  void createDummyResources();
  void submitOneTime(const std::function<void(VkCommandBuffer)>& record);
  void writeSampled(uint32_t slot, VkImageView view, VkImageLayout layout);
  void writeSampled3D(uint32_t slot, VkImageView view, VkImageLayout layout);
  void writeSamplerSlot(uint32_t slot, VkSampler sampler);
  void writeStorageBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize size);
  uint32_t writeStorageTable(std::span<const ResourceView> views);
  uint32_t writeUavTable(std::span<const ResourceView> views);
  void resetFrameRings();
  uint32_t takeFrameRingSlot(uint32_t& cursor, uint32_t perFrame, uint32_t ringSize, const char* name);
  VkDescriptorSetLayout makeBindlessLayout(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages);
  VkDescriptorSetLayout makeTableLayout(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages);
  std::shared_ptr<RootSignature> createPipelineLayout(VkShaderStageFlags pushStages, bool compute);

  bool m_debug = false;
  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
  VkPhysicalDevice m_physical = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_queue = VK_NULL_HANDLE;
  uint32_t m_queueFamily = 0;
  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  VmaAllocator m_allocator = VK_NULL_HANDLE;

  uint32_t m_sampledCount = kBindlessSampled;
  uint32_t m_sampled3DCount = kBindlessSampled3D;
  uint32_t m_samplerCount = kBindlessSamplers;
  uint32_t m_storageCount = kBindlessStorage;

  VkDescriptorPool m_descPool = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_sampledLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_sampled3DLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_samplerLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_storageLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_storageTableLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_uavTableLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_samplerTableLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_uboLayout = VK_NULL_HANDLE;
  VkDescriptorSet m_sampledSet = VK_NULL_HANDLE;
  VkDescriptorSet m_sampled3DSet = VK_NULL_HANDLE;
  VkDescriptorSet m_samplerSet = VK_NULL_HANDLE;
  VkDescriptorSet m_storageSet = VK_NULL_HANDLE;
  VkDescriptorSet m_uboSet = VK_NULL_HANDLE;
  std::array<VkDescriptorSet, kUboSetRing> m_uboSets{};
  uint32_t m_uboSetCursor = 0;
  struct UboBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = 256;
  };
  std::array<UboBinding, kUboBindings> m_uboCache{};
  std::array<VkDescriptorSet, kTransientTables> m_uavTables{};
  std::array<VkDescriptorSet, kTransientTables> m_samplerTables{};
  std::array<VkDescriptorSet, kTransientTables> m_storageTables{};
  uint32_t m_uavTableCursor = 0;
  uint32_t m_samplerTableCursor = 0;
  uint32_t m_storageTableCursor = 0;
  VkDescriptorSet m_boundSamplerSet = VK_NULL_HANDLE;
  VkDescriptorSet m_boundStorageSet = VK_NULL_HANDLE;

  BindlessManager m_sampledSlots;
  BindlessManager m_sampled3DSlots;
  BindlessManager m_samplerSlots;
  BindlessManager m_storageSlots;
  std::unordered_map<uint32_t, VulkanTexture*> m_uavTextures;
  uint32_t m_nextUavIndex = kUavBindlessBase;

  VkImage m_dummyImage = VK_NULL_HANDLE;
  VkImageView m_dummyView = VK_NULL_HANDLE;
  VmaAllocation m_dummyAlloc = VK_NULL_HANDLE;
  VkImage m_dummyImage3D = VK_NULL_HANDLE;
  VkImageView m_dummyView3D = VK_NULL_HANDLE;
  VmaAllocation m_dummyAlloc3D = VK_NULL_HANDLE;
  VkSampler m_dummySampler = VK_NULL_HANDLE;
  VkBuffer m_dummyUbo = VK_NULL_HANDLE;
  VmaAllocation m_dummyUboAlloc = VK_NULL_HANDLE;
  VkBuffer m_pushUbo = VK_NULL_HANDLE;
  VmaAllocation m_pushUboAlloc = VK_NULL_HANDLE;
  void* m_pushUboMapped = nullptr;
  uint32_t m_pushUboCursor = 0;

  uint32_t m_frameIndex = 0;
  uint64_t m_frameNumber = 0;
  VulkanSwapChain* m_activeSwapChain = nullptr;

  std::array<VkCommandBuffer, kMaxFramesInFlight> m_cmdBuffers{};
  std::array<std::unique_ptr<VulkanCommandList>, kMaxFramesInFlight> m_cmdLists{};
  std::array<VkSemaphore, kMaxFramesInFlight> m_acquireSemaphores{};
  std::array<VkSemaphore, kMaxFramesInFlight> m_renderSemaphores{};
  std::array<VkFence, kMaxFramesInFlight> m_fences{};
};

} // namespace tucano::rhi
