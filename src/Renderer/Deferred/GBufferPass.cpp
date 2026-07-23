#include "Renderer/Deferred/GBufferPass.h"
#include "Renderer/Mesh.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12Resource.h"

#include <cmath>
#include <cstring>
#include <span>

namespace tucano {
namespace {

struct ObjectCBData {
  glm::mat4 worldInvTranspose;
  glm::vec4 baseColorFactor;
  glm::vec4 materialParams;
  glm::vec4 emissiveFactor;
  glm::uvec4 textureIndices;
  glm::vec4 materialExt;
  glm::uvec4 textureIndices2;
  glm::vec4 fuzzColor;
  // Must mirror ObjectCBData in Renderer.cpp and ObjectCB in GBuffer.hlsl.
  glm::uvec4 skinInfo{0, 0, 0, 0}; // first matrix, bone count (0 = not skinned)
};

struct RootXform {
  glm::mat4 viewProj;
  glm::mat4 world;
};

struct DrawIndexedArgs {
  uint32_t indexCountPerInstance;
  uint32_t instanceCount;
  uint32_t startIndexLocation;
  int32_t baseVertexLocation;
  uint32_t startInstanceLocation;
};

void bindAndDrawMeshlet(GBufferPassContext& ctx, RenderObject& obj, uint32_t materialIndex, rhi::Buffer& ib,
                        uint32_t indexCount, uint32_t indexOffset, rhi::Buffer* indirectArgs, uint64_t indirectOff,
                        size_t objectIndex = SIZE_MAX) {
  auto mat = (materialIndex < obj.materials.size()) ? obj.materials[materialIndex] : nullptr;
  auto* albedo = mat && mat->albedo ? &mat->albedo->resource() : &ctx.defaultAlbedo;
  auto* normal = mat && mat->normal ? &mat->normal->resource() : &ctx.defaultNormal;
  auto* orm = mat && mat->metallicRoughness ? &mat->metallicRoughness->resource() : &ctx.defaultORM;
  auto* emissive = mat && mat->emissive ? &mat->emissive->resource() : &ctx.defaultBlack;
  auto* detailAlb = mat && mat->detailAlbedo ? &mat->detailAlbedo->resource() : &ctx.defaultAlbedo;
  auto* detailNrm = mat && mat->detailNormal ? &mat->detailNormal->resource() : &ctx.defaultNormal;

  ctx.cmd.transition(*albedo, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(*normal, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(*orm, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(*emissive, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(*detailAlb, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(*detailNrm, rhi::ResourceState::ShaderResource);

  ObjectCBData ocb{};
  // Avoid singular-matrix NaNs from non-uniform scales / zero axes in glTF nodes.
  const glm::mat3 nrm = glm::mat3(obj.worldMatrix);
  ocb.worldInvTranspose = glm::mat4(glm::transpose(glm::inverse(nrm)));
  if (!std::isfinite(ocb.worldInvTranspose[0][0])) {
    ocb.worldInvTranspose = obj.worldMatrix;
  }
  // Stable inverse-transpose for uniform scale: n' = normalize(R * n) without 1/s blow-up.
  {
    const float sx = glm::length(glm::vec3(obj.worldMatrix[0]));
    const float sy = glm::length(glm::vec3(obj.worldMatrix[1]));
    const float sz = glm::length(glm::vec3(obj.worldMatrix[2]));
    if (sx > 1e-8f && sy > 1e-8f && sz > 1e-8f) {
      glm::mat3 r;
      r[0] = glm::vec3(obj.worldMatrix[0]) / sx;
      r[1] = glm::vec3(obj.worldMatrix[1]) / sy;
      r[2] = glm::vec3(obj.worldMatrix[2]) / sz;
      ocb.worldInvTranspose = glm::mat4(glm::transpose(glm::inverse(r)));
    }
  }
  if (mat) {
    const float dielectricF0 = 0.16f * mat->reflectance * mat->reflectance;
    ocb.baseColorFactor = mat->baseColorFactor;
    ocb.materialParams = {mat->metallicFactor, mat->roughnessFactor, mat->aoFactor,
                          mat->alphaMask ? mat->alphaCutoff : 0.0f};
    ocb.emissiveFactor = glm::vec4(mat->emissiveFactor, dielectricF0);
    ocb.materialExt = {mat->clearcoat, mat->clearcoatRoughness, mat->fuzz, mat->detailScale};
    ocb.fuzzColor = glm::vec4(mat->fuzzColor, 0.0f);
  } else {
    ocb.baseColorFactor = glm::vec4(1);
    ocb.materialParams = {0, 1, 1, 0};
    ocb.emissiveFactor = glm::vec4(0, 0, 0, 0.04f);
    ocb.materialExt = glm::vec4(0);
    ocb.fuzzColor = glm::vec4(1);
  }
  ocb.textureIndices = glm::uvec4(albedo->bindlessIndex(), normal->bindlessIndex(), orm->bindlessIndex(),
                                  emissive->bindlessIndex());
  ocb.textureIndices2 = glm::uvec4(detailAlb->bindlessIndex(), detailNrm->bindlessIndex(), 0, 0);
  // Bindless texture table is [0, 8192); never write OOB / ~0u indices into ObjectCB.
  auto clampBindless = [](uint32_t i) { return (i < 8192u) ? i : 0u; };
  ocb.textureIndices = glm::uvec4(clampBindless(ocb.textureIndices.x), clampBindless(ocb.textureIndices.y),
                                  clampBindless(ocb.textureIndices.z), clampBindless(ocb.textureIndices.w));
  ocb.textureIndices2 =
      glm::uvec4(clampBindless(ocb.textureIndices2.x), clampBindless(ocb.textureIndices2.y), 0, 0);

  // Skinning: point the shader at this object's slice of the frame palette. Left at zero when the
  // object has no skin, which is what makes the vertex shader take the rigid path.
  if (ctx.skinningBuffer && ctx.objectSkinBase && objectIndex < ctx.objectSkinBase->size()) {
    const uint32_t base = (*ctx.objectSkinBase)[objectIndex];
    if (base != UINT32_MAX && !obj.skinningMatrices.empty()) {
      ocb.skinInfo = glm::uvec4(base, static_cast<uint32_t>(obj.skinningMatrices.size()), 0, 0);
    }
  }

  const uint64_t cbOff = ctx.pushObjectCB(&ocb, sizeof(ocb));
  RootXform xform{ctx.viewProj, obj.worldMatrix};
  ctx.cmd.setGraphicsRootConstants(0, &xform, 32);
  ctx.cmd.setGraphicsRootCBV(2, ctx.objectCB, cbOff);
  ctx.cmd.setPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
  ctx.cmd.setVertexBuffer(obj.mesh->vertexBuffer(), sizeof(Vertex));
  ctx.cmd.setIndexBuffer(ib, true);
  if (indirectArgs) {
    ctx.cmd.transition(*indirectArgs, rhi::ResourceState::IndirectArgument);
    ctx.cmd.drawIndexedIndirect(*indirectArgs, indirectOff);
  } else {
    ctx.cmd.drawIndexed(indexCount, indexOffset);
  }
  if (ctx.drawCalls) {
    ++(*ctx.drawCalls);
  }
}

} // namespace

void executeGBufferPass(GBufferPassContext& ctx) {
  ctx.cmd.transition(ctx.albedo, rhi::ResourceState::RenderTarget);
  ctx.cmd.transition(ctx.normal, rhi::ResourceState::RenderTarget);
  ctx.cmd.transition(ctx.orm, rhi::ResourceState::RenderTarget);
  ctx.cmd.transition(ctx.emissive, rhi::ResourceState::RenderTarget);
  ctx.cmd.transition(ctx.depthColor, rhi::ResourceState::RenderTarget);
  ctx.cmd.transition(ctx.depth, rhi::ResourceState::DepthWrite);

  rhi::Texture* gbufferRTs[] = {&ctx.albedo, &ctx.normal, &ctx.orm, &ctx.emissive, &ctx.depthColor};
  ctx.cmd.setRenderTargets(gbufferRTs, &ctx.depth);
  const float zclear[4] = {0, 0, 0, 0};
  ctx.cmd.clearRenderTarget(ctx.albedo, zclear);
  ctx.cmd.clearRenderTarget(ctx.normal, zclear);
  ctx.cmd.clearRenderTarget(ctx.orm, zclear);
  ctx.cmd.clearRenderTarget(ctx.emissive, zclear);
  ctx.cmd.clearRenderTarget(ctx.depthColor, zclear);
  if (ctx.clearDepth) {
    ctx.cmd.clearDepth(ctx.depth, 1.0f);
  }
  ctx.cmd.setViewport(ctx.viewport);
  ctx.cmd.setScissor(ctx.scissor);
  ctx.cmd.setDescriptorHeap();
  ctx.cmd.setRootSignature(ctx.root);
  ctx.cmd.setPipeline(ctx.gbufferPSO);
  ctx.cmd.setGraphicsRootSrvTable(3, 0);
  ctx.cmd.setGraphicsRootSamplerTable(4, ctx.sampTable);

  // space1 t0: the frame's skinning palette. Bound once — every skinned draw indexes into it.
  if (ctx.skinningBuffer) {
    auto& dx = static_cast<rhi::DX12Device&>(ctx.device);
    D3D12_CPU_DESCRIPTOR_HANDLE skinSrv[] = {
        static_cast<rhi::DX12Buffer&>(*ctx.skinningBuffer).srvCpu};
    ctx.cmd.setGraphicsRootSrvTable(5, dx.writeSrvTable(skinSrv, 1));
  }

  for (size_t objectIndex = 0; objectIndex < ctx.scene.objects.size(); ++objectIndex) {
    auto& obj = ctx.scene.objects[objectIndex];
    if (!obj.mesh || !obj.visible) {
      continue;
    }
    if (ctx.meshletsTotal) {
      *ctx.meshletsTotal += obj.mesh->meshletCount();
    }

    auto drawSub = [&](uint32_t materialIndex, uint32_t indexCount, uint32_t indexOffset, rhi::Buffer& ib) {
      bindAndDrawMeshlet(ctx, obj, materialIndex, ib, indexCount, indexOffset, nullptr, 0, objectIndex);
    };

    const bool meshletPath = ctx.enableMeshlets && obj.mesh->meshletIndexBuffer() && obj.mesh->meshletCount() > 0;

    if (meshletPath && ctx.enableGpuMeshletCull && ctx.indirectArgs && ctx.indirectArgSlot &&
        obj.mesh->meshletGpuBuffer()) {
      const uint32_t count = obj.mesh->meshletCount();
      const uint32_t base = *ctx.indirectArgSlot;
      *ctx.indirectArgSlot += count;
      if (ctx.meshletsDrawn) {
        // Exact surviving count needs readback; report full set (culled draws are instanceCount=0).
        *ctx.meshletsDrawn += count;
      }
      for (uint32_t mi = 0; mi < count; ++mi) {
        const auto& ml = obj.mesh->meshlets()[mi];
        const auto& sub = obj.mesh->submeshes()[ml.submeshIndex];
        const uint64_t off = uint64_t(base + mi) * sizeof(DrawIndexedArgs);
        bindAndDrawMeshlet(ctx, obj, sub.materialIndex, *obj.mesh->meshletIndexBuffer(), ml.indexCount,
                           ml.indexOffset, ctx.indirectArgs, off);
      }
    } else if (meshletPath && ctx.visibleMeshlets) {
      cullMeshletsCPU(*obj.mesh, obj.worldMatrix, ctx.viewProj, *ctx.visibleMeshlets);
      if (ctx.meshletsDrawn) {
        *ctx.meshletsDrawn += static_cast<uint32_t>(ctx.visibleMeshlets->size());
      }
      if (!ctx.visibleMeshlets->empty()) {
        for (uint32_t mi : *ctx.visibleMeshlets) {
          const auto& ml = obj.mesh->meshlets()[mi];
          const auto& sub = obj.mesh->submeshes()[ml.submeshIndex];
          if (ctx.indirectArgs && ctx.indirectArgSlot && ctx.indirectArgs->mapped()) {
            const uint32_t slot = (*ctx.indirectArgSlot)++;
            const uint64_t off = uint64_t(slot) * sizeof(DrawIndexedArgs);
            DrawIndexedArgs args{ml.indexCount, 1u, ml.indexOffset, 0, 0};
            std::memcpy(static_cast<uint8_t*>(ctx.indirectArgs->mapped()) + off, &args, sizeof(args));
            bindAndDrawMeshlet(ctx, obj, sub.materialIndex, *obj.mesh->meshletIndexBuffer(), ml.indexCount,
                               ml.indexOffset, ctx.indirectArgs, off);
          } else {
            drawSub(sub.materialIndex, ml.indexCount, ml.indexOffset, *obj.mesh->meshletIndexBuffer());
          }
        }
      } else {
        for (const auto& sub : obj.mesh->submeshes()) {
          drawSub(sub.materialIndex, sub.indexCount, sub.indexOffset, obj.mesh->indexBuffer());
        }
      }
    } else {
      for (const auto& sub : obj.mesh->submeshes()) {
        drawSub(sub.materialIndex, sub.indexCount, sub.indexOffset, obj.mesh->indexBuffer());
      }
    }
  }

  ctx.cmd.transition(ctx.albedo, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.normal, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.orm, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.emissive, rhi::ResourceState::ShaderResource);
  ctx.cmd.transition(ctx.depthColor, rhi::ResourceState::ShaderResource);
}

} // namespace tucano
