#pragma once

#include "RHI/RHI.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace tucano {

// Directional VSM: virtual page grid + physical R32 depth atlas + R32 page table.
// Pages render with scale-bias on cascade-0 lightViewProj so sampling uses the same depth compare.
class VirtualShadowMapAtlas {
public:
  static constexpr uint32_t kPageSize = 128;
  static constexpr uint32_t kPagesPerAxis = 32;   // virtual 4096
  static constexpr uint32_t kPhysicalPages = 64;  // 8x8 physical
  static constexpr uint32_t kPhysicalGrid = 8;
  static constexpr uint32_t kAllocGrid = 8; // pages allocated around center

  void init(rhi::Device& device);
  void beginFrame();
  void allocateForLightView(const glm::mat4& lightViewProj, const glm::vec3& lightDir, float cascadeExtent);
  void uploadPageTable(rhi::Device& device);

  rhi::Texture* physicalAtlas() const { return m_physical.get(); }
  rhi::Texture* pageTable() const { return m_pageTable.get(); }

  uint32_t pageSize() const { return kPageSize; }
  uint32_t pagesPerAxis() const { return kPagesPerAxis; }
  uint32_t physicalGrid() const { return kPhysicalGrid; }
  uint32_t mappedCount() const { return m_mapped; }

  glm::vec4 virtualOriginExtent() const { return m_originExtent; }
  glm::mat4 lightViewProj() const { return m_lightViewProj; }

  struct MappedPage {
    uint32_t vx = 0;
    uint32_t vy = 0;
    uint32_t physical = 0;
  };
  const std::vector<MappedPage>& mappedPages() const { return m_mappedList; }

  // Scale-bias * cascade0 VP so one virtual page fills clip XY.
  glm::mat4 pageLightViewProj(uint32_t vx, uint32_t vy) const;

  void physicalViewport(uint32_t physical, uint32_t& outX, uint32_t& outY) const;

private:
  std::shared_ptr<rhi::Texture> m_physical;
  std::shared_ptr<rhi::Texture> m_pageTable;
  std::vector<float> m_tableCpu; // -1 empty, else physical index as float
  std::vector<MappedPage> m_mappedList;
  uint32_t m_mapped = 0;
  glm::vec4 m_originExtent{0, 0, 0, 1};
  glm::mat4 m_lightViewProj{1.0f};
};

} // namespace tucano
