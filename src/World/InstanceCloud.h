#pragma once

// WM-6: Instance Cloud — GPU-driven instancing.
//
// A cell can hold thousands of small, repeated props: grass, rocks, debris, foliage. Emitting one
// RenderObject each is exactly what blows the engine's 4096-draw ceiling (the ceiling WM-5 measured
// and this phase removes). An instance cloud is the answer: one mesh, N transforms in a GPU buffer,
// a compute pre-pass that frustum-culls and LOD-selects every instance and compacts the survivors,
// and a SINGLE ExecuteIndirect that draws them all.
//
// This header is the CPU half and, as in WM-4, the REFERENCE. Shaders/InstanceCull.hlsl is a
// transliteration of cullInstancesCPU below; the parity gate dispatches both on identical input and
// asserts the visible sets match. The struct here is byte-for-byte the shader's InstanceGpu.
//
// The module stays free of RHI: this file is plain math and data. The GPU dispatch + readback lives
// in InstanceCloudCuller, next to it, the same split WM-4 uses for cells.

#include "World/FrustumCull.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace tucano::world {

/// One instance, laid out to match InstanceCull.hlsl's InstanceGpu byte for byte (96 bytes).
/// `transform` is what the draw vertex shader reads; `center`/`radius` are what the cull reads.
struct InstanceGpu {
  glm::mat4 transform{1.0f}; // 64 B — world matrix
  glm::vec3 center{0.0f};    // world-space bounding-sphere centre
  float radius = 1.0f;       // world-space bounding-sphere radius
  uint32_t materialId = 0;
  uint32_t lodMask = ~0u;    // bit i set => instance may render at LOD i; ~0u = all bands
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;
};
static_assert(sizeof(InstanceGpu) == 96, "InstanceGpu must match the HLSL layout");

/// D3D12_DRAW_INDEXED_ARGUMENTS, mirrored so the cull can grow instanceCount in place and the CPU
/// reference can produce the same block for comparison.
struct DrawIndexedArgs {
  uint32_t indexCountPerInstance = 0;
  uint32_t instanceCount = 0;
  uint32_t startIndex = 0;
  uint32_t baseVertex = 0;
  uint32_t startInstance = 0;
};

/// Builds an instance's bounding sphere from a mesh's local bounds and the instance transform. The
/// centre is the transformed local centre; the radius is the local half-diagonal scaled by the
/// largest axis scale — conservative, never clips a rotated instance early.
InstanceGpu makeInstance(const glm::mat4& transform, const glm::vec3& localMin,
                         const glm::vec3& localMax, uint32_t materialId = 0, uint32_t lodMask = ~0u);

/// CPU reference cull. Fills `outVisible` with the ORIGINAL indices of the instances that pass the
/// frustum + distance + LOD-mask test, in ascending index order (the shader's order is compaction
/// order, so the gate compares as sets, not sequences). This is the truth the GPU must match.
void cullInstancesCPU(const std::vector<InstanceGpu>& instances, const glm::mat4& viewProj,
                      const glm::vec3& observer, const CullConfig& cfg,
                      std::vector<uint32_t>& outVisible);

} // namespace tucano::world
