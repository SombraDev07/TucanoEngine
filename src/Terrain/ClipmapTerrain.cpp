#include "Terrain/ClipmapTerrain.h"

#include <cmath>

namespace tucano::terrain {
namespace {

// The hole in a hollow ring: the central half of the grid, which the finer level's full extent
// covers exactly. [holeLo, holeHi) in quad coordinates.
constexpr uint32_t kHoleLo = kClipmapGridN / 4;      // 16
constexpr uint32_t kHoleHi = kClipmapGridN - kHoleLo; // 48

void emitQuad(std::vector<uint32_t>& idx, uint32_t qx, uint32_t qz) {
  const uint32_t i00 = qz * kClipmapGridStride + qx;
  const uint32_t i10 = i00 + 1;
  const uint32_t i01 = i00 + kClipmapGridStride;
  const uint32_t i11 = i01 + 1;
  idx.push_back(i00);
  idx.push_back(i01);
  idx.push_back(i10);
  idx.push_back(i10);
  idx.push_back(i01);
  idx.push_back(i11);
}

} // namespace

ClipmapTerrain::ClipmapTerrain(rhi::Device& device, const ClipmapTerrainDesc& desc) : m_desc(desc) {
  m_levels.resize(desc.levelCount);
  buildIndexBuffers(device);
}

void ClipmapTerrain::buildIndexBuffers(rhi::Device& device) {
  std::vector<uint32_t> full;
  std::vector<uint32_t> ring;
  full.reserve(kClipmapGridN * kClipmapGridN * 6);
  ring.reserve((kClipmapGridN * kClipmapGridN - (kHoleHi - kHoleLo) * (kHoleHi - kHoleLo)) * 6);

  for (uint32_t qz = 0; qz < kClipmapGridN; ++qz) {
    for (uint32_t qx = 0; qx < kClipmapGridN; ++qx) {
      emitQuad(full, qx, qz);
      const bool inHole = qx >= kHoleLo && qx < kHoleHi && qz >= kHoleLo && qz < kHoleHi;
      if (!inHole) emitQuad(ring, qx, qz);
    }
  }

  m_fullCount = uint32_t(full.size());
  m_ringCount = uint32_t(ring.size());

  rhi::BufferDesc fd{};
  fd.size = full.size() * sizeof(uint32_t);
  fd.usage = rhi::BufferUsage::Index;
  fd.debugName = "ClipmapFullIB";
  m_fullIB = device.createBuffer(fd, full.data());

  rhi::BufferDesc rd{};
  rd.size = ring.size() * sizeof(uint32_t);
  rd.usage = rhi::BufferUsage::Index;
  rd.debugName = "ClipmapRingIB";
  m_ringIB = device.createBuffer(rd, ring.data());
}

void ClipmapTerrain::setHeightmap(uint32_t bindlessIndex, const glm::vec2& worldMin, float worldSize,
                                  float heightScale) {
  m_hmIndex = bindlessIndex;
  m_worldMin = worldMin;
  m_invWorldSize = worldSize > 0.0f ? 1.0f / worldSize : 1.0f;
  m_heightScale = heightScale;
}

void ClipmapTerrain::update(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
  // Gribb-Hartmann planes for a rough ring cull. glm is COLUMN-major, so the clip-space rows are
  // gathered across the columns (row i = the i-th component of each column), NOT viewProj[i] (which
  // is a whole column). Getting this wrong culls everything — which it did on the first pass. Depth
  // is zero-to-one (D3D), so the near plane is row 2 alone, not row3+row2.
  auto row = [&](int i) {
    return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]);
  };
  const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  glm::vec4 planes[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
  for (auto& p : planes) p /= glm::length(glm::vec3(p));

  for (uint32_t L = 0; L < m_levels.size(); ++L) {
    ClipmapLevel& lv = m_levels[L];
    lv.spacing = m_desc.baseSpacing * std::exp2f(float(L));
    const float extent = float(kClipmapGridN) * lv.spacing;
    lv.extentHalf = extent * 0.5f;
    lv.ring = (L > 0);

    // Snap the ring to twice its spacing so vertices land on stable world positions frame to frame
    // and the hollow centre lines up with the finer level inside it. extentHalf = 32*spacing is a
    // multiple of 2*spacing, so subtracting it keeps the corner on the snap grid.
    const float snap = lv.spacing * 2.0f;
    const float cx = std::floor(cameraPos.x / snap) * snap;
    const float cz = std::floor(cameraPos.z / snap) * snap;
    lv.origin = glm::vec2(cx - lv.extentHalf, cz - lv.extentHalf);

    // Frustum-cull the ring's AABB (generous vertical span; terrain height is unknown here).
    const glm::vec3 mn(lv.origin.x, -1000.0f, lv.origin.y);
    const glm::vec3 mx(lv.origin.x + extent, 1000.0f, lv.origin.y + extent);
    bool visible = true;
    for (const glm::vec4& pl : planes) {
      const glm::vec3 pv(pl.x >= 0 ? mx.x : mn.x, pl.y >= 0 ? mx.y : mn.y, pl.z >= 0 ? mx.z : mn.z);
      if (glm::dot(glm::vec3(pl), pv) + pl.w < 0.0f) { visible = false; break; }
    }
    lv.visible = visible;
  }
}

} // namespace tucano::terrain
