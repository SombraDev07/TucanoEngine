#include "Renderer/Shadows/VirtualShadowMaps.h"

#include <algorithm>
#include <cstring>

namespace tucano {

void VirtualShadowMapAtlas::init(rhi::Device& device) {
  rhi::TextureDesc phys{};
  phys.width = kPhysicalGrid * kPageSize;
  phys.height = kPhysicalGrid * kPageSize;
  phys.format = rhi::Format::R32_FLOAT;
  phys.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
  phys.debugName = "VSM_Physical";
  m_physical = device.createTexture(phys, nullptr, 0);

  rhi::TextureDesc table{};
  table.width = kPagesPerAxis;
  table.height = kPagesPerAxis;
  table.format = rhi::Format::R32_FLOAT;
  table.usage = rhi::TextureUsage::ShaderResource;
  table.debugName = "VSM_PageTable";
  m_pageTable = device.createTexture(table, nullptr, 0);

  m_tableCpu.assign(kPagesPerAxis * kPagesPerAxis, -1.0f);
}

void VirtualShadowMapAtlas::beginFrame() {
  std::fill(m_tableCpu.begin(), m_tableCpu.end(), -1.0f);
  m_mappedList.clear();
  m_mapped = 0;
}

void VirtualShadowMapAtlas::allocateForLightView(const glm::mat4& lightViewProj, const glm::vec3& /*lightDir*/,
                                                 float cascadeExtent) {
  m_lightViewProj = lightViewProj;
  const float extent = std::max(cascadeExtent, 8.0f);
  m_originExtent = glm::vec4(-extent * 0.5f, 0.0f, -extent * 0.5f, extent);

  const uint32_t start = (kPagesPerAxis - kAllocGrid) / 2;
  m_mapped = 0;
  m_mappedList.clear();
  for (uint32_t vy = 0; vy < kAllocGrid && m_mapped < kPhysicalPages; ++vy) {
    for (uint32_t vx = 0; vx < kAllocGrid && m_mapped < kPhysicalPages; ++vx) {
      const uint32_t px = start + vx;
      const uint32_t py = start + vy;
      const uint32_t idx = py * kPagesPerAxis + px;
      m_tableCpu[idx] = float(m_mapped);
      m_mappedList.push_back({px, py, m_mapped});
      ++m_mapped;
    }
  }
}

glm::mat4 VirtualShadowMapAtlas::pageLightViewProj(uint32_t vx, uint32_t vy) const {
  const float N = float(kPagesPerAxis);
  const float u0 = float(vx) / N;
  const float v0 = float(vy) / N;
  // Matches DeferredLighting: uv = ndc.xy * float2(0.5, -0.5) + 0.5
  const float ndcX0 = 2.0f * u0 - 1.0f;
  const float ndcY0 = 1.0f - 2.0f * (v0 + 1.0f / N);
  const float sx = N;
  const float sy = N;
  const float bx = -1.0f - ndcX0 * sx;
  const float by = -1.0f - ndcY0 * sy;
  glm::mat4 M(1.0f);
  M[0][0] = sx;
  M[1][1] = sy;
  M[3][0] = bx;
  M[3][1] = by;
  return M * m_lightViewProj;
}

void VirtualShadowMapAtlas::physicalViewport(uint32_t physical, uint32_t& outX, uint32_t& outY) const {
  outX = (physical % kPhysicalGrid) * kPageSize;
  outY = (physical / kPhysicalGrid) * kPageSize;
}

void VirtualShadowMapAtlas::uploadPageTable(rhi::Device& device) {
  if (!m_pageTable) {
    return;
  }
  device.uploadTexture(*m_pageTable, m_tableCpu.data(), kPagesPerAxis, kPagesPerAxis,
                       kPagesPerAxis * sizeof(float), 0, 0);
}

} // namespace tucano
