#include "World/InstanceCloud.h"

#include <algorithm>

namespace tucano::world {

InstanceGpu makeInstance(const glm::mat4& transform, const glm::vec3& localMin,
                         const glm::vec3& localMax, uint32_t materialId, uint32_t lodMask) {
  InstanceGpu inst;
  inst.transform = transform;

  const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
  inst.center = glm::vec3(transform * glm::vec4(localCenter, 1.0f));

  // Largest axis scale, taken from the column lengths of the upper 3×3. Multiplying the local
  // half-diagonal by it gives a sphere that still encloses the box under non-uniform scale and
  // rotation — conservative, which is what culling wants.
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  const float maxScale = std::max({sx, sy, sz});
  const float localRadius = glm::length((localMax - localMin) * 0.5f);
  inst.radius = localRadius * maxScale;

  inst.materialId = materialId;
  inst.lodMask = lodMask;
  return inst;
}

namespace {

// The exact sphere-plane test the shader runs: outside plane i when the centre is behind it by more
// than the radius. Inward normals, so "behind" is a negative signed distance.
bool sphereInFrustum(const Frustum& f, const glm::vec3& c, float r) {
  for (int i = 0; i < Frustum::Count; ++i) {
    const glm::vec4& p = f.planes[i];
    if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
  }
  return true;
}

} // namespace

void cullInstancesCPU(const std::vector<InstanceGpu>& instances, const glm::mat4& viewProj,
                      const glm::vec3& observer, const CullConfig& cfg,
                      std::vector<uint32_t>& outVisible) {
  outVisible.clear();
  const Frustum f = extractFrustum(viewProj);

  for (uint32_t i = 0; i < instances.size(); ++i) {
    const InstanceGpu& inst = instances[i];
    const float dist = glm::length(inst.center - observer);

    if (cfg.maxDistance > 0.0f && dist > cfg.maxDistance) continue;
    if (!sphereInFrustum(f, inst.center, inst.radius)) continue;

    const uint32_t lod = selectLod(dist, cfg);
    if ((inst.lodMask & (1u << lod)) == 0u) continue;

    outVisible.push_back(i);
  }
}

} // namespace tucano::world
