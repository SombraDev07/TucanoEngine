#pragma once

#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

struct GBufferPassContext {
  rhi::Device& device;
  rhi::CommandList& cmd;
  rhi::RootSignature& root;
  rhi::PipelineState& gbufferPSO;
  rhi::Buffer& objectCB;
  rhi::Texture& albedo;
  rhi::Texture& normal;
  rhi::Texture& orm;
  rhi::Texture& emissive;
  rhi::Texture& depthColor;
  rhi::Texture& depth;
  rhi::Texture& defaultAlbedo;
  rhi::Texture& defaultNormal;
  rhi::Texture& defaultORM;
  rhi::Texture& defaultBlack;
  rhi::Sampler& linearSamp;
  Scene& scene;
  glm::mat4 viewProj{1.0f};
  rhi::Viewport viewport{};
  rhi::Scissor scissor{};
  bool clearDepth = true;
  uint32_t sampTable = 0;

  // Returns byte offset into objectCB for this draw's ObjectCB blob.
  std::function<uint64_t(const void* data, size_t size)> pushObjectCB;
  uint32_t* drawCalls = nullptr;
  uint32_t* meshletsTotal = nullptr;
  uint32_t* meshletsDrawn = nullptr;
  std::vector<uint32_t>* visibleMeshlets = nullptr;
  bool enableMeshlets = false;
  bool enableGpuMeshletCull = false; // CS wrote OutArgs; draw all slots (instanceCount 0/1)
  rhi::Buffer* indirectArgs = nullptr; // DRAW_INDEXED args (UAV|Indirect when GPU cull)
  uint32_t* indirectArgSlot = nullptr; // base slot; advances by meshletCount when GPU cull
};

// Fills GBuffer MRTs from scene meshes (bindless materials + fuzz/detail).
void executeGBufferPass(GBufferPassContext& ctx);

} // namespace tucano
