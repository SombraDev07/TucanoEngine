#pragma once

// WM-4: cell culling.
//
// The scheduler decides what to KEEP RESIDENT; culling decides what, of the resident set, is
// actually visible this frame. With thousands of cells that is a per-frame sweep, and the roadmap
// puts it on the GPU: upload every cell's box, run one compute dispatch, get back a compact list
// of visible indices.
//
// This header is the CPU half — and it is not just a fallback. It is the REFERENCE. The compute
// shader (Shaders/CellCull.hlsl) is a transliteration of exactly this math, and a parity test
// dispatches both on the same input and asserts the GPU visible set equals this one. Getting the
// frustum-plane convention wrong is the classic GPU-cull bug (things vanish at screen edges); the
// reference plus the parity test is how that is caught instead of shipped.
//
// Conventions match the engine's production shaders: left-handed, glm column-major viewProj, and a
// zero-to-one depth clip (D3D, not GL). Plane normals point INWARD, so a point is inside the
// frustum when dot(plane.xyz, p) + plane.w >= 0 for all six.

#include "World/CellId.h"
#include "World/WorldGrid.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace tucano::world {

/// Six inward-facing planes, in the order the compute shader expects.
struct Frustum {
  enum Plane { Left = 0, Right, Bottom, Top, Near, Far, Count };
  glm::vec4 planes[Count]; ///< xyz = unit normal (inward), w = signed distance
};

/// Gribb-Hartmann extraction from a column-major, left-handed, zero-to-one-depth viewProj.
/// The planes are normalized, so plane.xyz is a unit normal and the signed distance is metric.
Frustum extractFrustum(const glm::mat4& viewProj);

/// AABB-vs-frustum, symmetric-diagonal ("p-vertex") test. Returns false only when the box is
/// wholly outside — it is conservative at the corners, which is correct for culling (never hide
/// something that might be visible).
bool aabbInFrustum(const Frustum& f, const glm::vec3& boundsMin, const glm::vec3& boundsMax);

/// Reference clip-space test, used to VERIFY extractFrustum in the gate. Transforms the eight box
/// corners by viewProj and checks whether any lands inside the clip volume. Slower than the plane
/// test but derived independently, so agreement between the two proves the plane extraction.
bool aabbInClipSpace(const glm::mat4& viewProj, const glm::vec3& boundsMin,
                     const glm::vec3& boundsMax);

/// One visible cell and the LOD chosen for it.
struct VisibleCell {
  CellId id;
  uint32_t lod = 0;    ///< 0 = highest detail; grows with distance
  float distance = 0.0f;
};

struct CullConfig {
  /// Distance at which LOD steps up one level. LOD n covers [n*lodStep, (n+1)*lodStep).
  float lodStep = 128.0f;
  uint32_t maxLod = 4;
  /// Cells beyond this are culled entirely, whatever the frustum says. 0 disables the cap.
  float maxDistance = 0.0f;
};

/// Selects the LOD for a cell at `distance`, by the same rule the shader uses.
uint32_t selectLod(float distance, const CullConfig& cfg);

/// CPU cell cull: every resident cell tested against the frustum and the distance cap, with a LOD
/// assigned. This is the reference the GPU path must match.
class WorldCuller {
public:
  /// Culls every cell currently in `grid`. `observer` is the point LOD is measured from (usually
  /// the camera). Output is unsorted; the caller sorts if it needs front-to-back.
  void cull(const WorldGrid& grid, const glm::mat4& viewProj, const glm::vec3& observer,
            const CullConfig& cfg, std::vector<VisibleCell>& out) const;

  /// Culls an explicit box list. The GPU path uploads boxes, not the live grid, so the parity test
  /// drives this overload to compare against an identical input.
  void cullBoxes(const std::vector<CellId>& ids, const std::vector<glm::vec3>& mins,
                 const std::vector<glm::vec3>& maxs, const glm::mat4& viewProj,
                 const glm::vec3& observer, const CullConfig& cfg,
                 std::vector<VisibleCell>& out) const;
};

} // namespace tucano::world
