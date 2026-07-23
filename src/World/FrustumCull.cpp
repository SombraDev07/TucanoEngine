#include "World/FrustumCull.h"

#include <algorithm>
#include <cmath>

namespace tucano::world {
namespace {

/// A row of a column-major glm matrix. glm stores m[col][row], so row i is a slice across columns.
glm::vec4 row(const glm::mat4& m, int i) {
  return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
}

glm::vec4 normalizePlane(const glm::vec4& p) {
  const float len = glm::length(glm::vec3(p));
  return len > 1e-8f ? p / len : p;
}

} // namespace

Frustum extractFrustum(const glm::mat4& viewProj) {
  const glm::vec4 r0 = row(viewProj, 0);
  const glm::vec4 r1 = row(viewProj, 1);
  const glm::vec4 r2 = row(viewProj, 2);
  const glm::vec4 r3 = row(viewProj, 3);

  Frustum f;
  // Inward normals, so inside ⇔ dot(n, p) + w >= 0. Derived from the clip-space inequalities:
  //   x >= -w  (left)     x <= w  (right)
  //   y >= -w  (bottom)   y <= w  (top)
  //   z >= 0   (near, D3D zero-to-one)   z <= w  (far)
  f.planes[Frustum::Left] = normalizePlane(r3 + r0);
  f.planes[Frustum::Right] = normalizePlane(r3 - r0);
  f.planes[Frustum::Bottom] = normalizePlane(r3 + r1);
  f.planes[Frustum::Top] = normalizePlane(r3 - r1);
  f.planes[Frustum::Near] = normalizePlane(r2); // zero-to-one: near is row2 alone, not r3 + r2
  f.planes[Frustum::Far] = normalizePlane(r3 - r2);
  return f;
}

bool aabbInFrustum(const Frustum& f, const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
  for (const glm::vec4& plane : f.planes) {
    const glm::vec3 n(plane);
    // The "positive vertex": the box corner furthest along the inward normal. If even that corner
    // is behind the plane, every corner is, so the box is wholly outside.
    const glm::vec3 pv(n.x >= 0.0f ? boundsMax.x : boundsMin.x,
                       n.y >= 0.0f ? boundsMax.y : boundsMin.y,
                       n.z >= 0.0f ? boundsMax.z : boundsMin.z);
    if (glm::dot(n, pv) + plane.w < 0.0f) return false;
  }
  return true;
}

bool aabbInClipSpace(const glm::mat4& viewProj, const glm::vec3& boundsMin,
                     const glm::vec3& boundsMax) {
  // Independent derivation used only to verify extractFrustum: transform all eight corners and, in
  // clip space, check the box is not entirely off one side. A per-plane OR across corners is the
  // conservative equivalent of the plane test, so the two must agree.
  bool outLeft = true, outRight = true, outBottom = true, outTop = true, outNear = true,
       outFar = true;
  for (int c = 0; c < 8; ++c) {
    const glm::vec3 corner((c & 1) ? boundsMax.x : boundsMin.x, (c & 2) ? boundsMax.y : boundsMin.y,
                           (c & 4) ? boundsMax.z : boundsMin.z);
    const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
    if (clip.x >= -clip.w) outLeft = false;
    if (clip.x <= clip.w) outRight = false;
    if (clip.y >= -clip.w) outBottom = false;
    if (clip.y <= clip.w) outTop = false;
    if (clip.z >= 0.0f) outNear = false;
    if (clip.z <= clip.w) outFar = false;
  }
  return !(outLeft || outRight || outBottom || outTop || outNear || outFar);
}

uint32_t selectLod(float distance, const CullConfig& cfg) {
  if (cfg.lodStep <= 0.0f) return 0;
  const uint32_t lod = uint32_t(std::max(distance, 0.0f) / cfg.lodStep);
  return std::min(lod, cfg.maxLod);
}

void WorldCuller::cull(const WorldGrid& grid, const glm::mat4& viewProj, const glm::vec3& observer,
                       const CullConfig& cfg, std::vector<VisibleCell>& out) const {
  out.clear();
  const Frustum f = extractFrustum(viewProj);
  grid.forEach([&](const WorldCell& cell) {
    const float dist = cell.distanceTo(observer);
    if (cfg.maxDistance > 0.0f && dist > cfg.maxDistance) return;
    if (!aabbInFrustum(f, cell.boundsMin, cell.boundsMax)) return;
    out.push_back(VisibleCell{cell.id, selectLod(dist, cfg), dist});
  });
}

void WorldCuller::cullBoxes(const std::vector<CellId>& ids, const std::vector<glm::vec3>& mins,
                            const std::vector<glm::vec3>& maxs, const glm::mat4& viewProj,
                            const glm::vec3& observer, const CullConfig& cfg,
                            std::vector<VisibleCell>& out) const {
  out.clear();
  const Frustum f = extractFrustum(viewProj);
  const size_t n = std::min({ids.size(), mins.size(), maxs.size()});
  for (size_t i = 0; i < n; ++i) {
    const glm::vec3 d = glm::max(glm::max(mins[i] - observer, glm::vec3(0.0f)), observer - maxs[i]);
    const float dist = glm::length(d);
    if (cfg.maxDistance > 0.0f && dist > cfg.maxDistance) continue;
    if (!aabbInFrustum(f, mins[i], maxs[i])) continue;
    out.push_back(VisibleCell{ids[i], selectLod(dist, cfg), dist});
  }
}

} // namespace tucano::world
