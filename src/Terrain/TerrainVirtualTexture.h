#pragma once

// Terrain material Virtual Texturing — clean-room, from the published technique (Barrett's Sparse
// Virtual Textures + the adaptive-VT terrain talks). NOT ported from any engine.
//
// A virtual texture is far larger than VRAM: it covers the whole world's terrain material at high
// density, but only the PAGES the camera can see are physically resident, in a fixed-size atlas. A
// page table (indirection texture) maps a virtual page to its physical slot; the terrain shader
// samples the page table, then the atlas. VRAM is bounded by the atlas, independent of world size —
// that is the whole point.
//
// This is Phase 1: CPU-driven and single-layer (albedo). "CPU-driven" means the set of needed pages
// is computed from the camera on the CPU (a square around it, matching what the near clipmap levels
// show), not from a GPU feedback pass — that, plus a mip pyramid for far terrain, is Phase 2. Pages
// are GENERATED procedurally from the terrain height (a height/slope splat + detail noise), so no
// disk assets are needed to prove the pipeline; a disk-backed page would slot into loadPage() the
// same way. The atlas and page table are kept as CPU mirrors and re-uploaded when they change —
// simple and correct; a production build would copy only the changed page sub-rects.

#include "RHI/RHI.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tucano::terrain {

inline constexpr uint32_t kVtPageSize = 128;       ///< atlas page edge, texels (incl. border)
inline constexpr uint32_t kVtBorder = 2;           ///< replicated border for bilinear across pages
inline constexpr uint32_t kVtCore = kVtPageSize - 2 * kVtBorder; ///< 124, the sampled core
inline constexpr uint32_t kVtAtlasPagesPerRow = 16; ///< 16×16 = 256 pages
inline constexpr uint32_t kVtAtlasSize = kVtAtlasPagesPerRow * kVtPageSize; ///< 2048
inline constexpr uint32_t kVtMaxResident = kVtAtlasPagesPerRow * kVtAtlasPagesPerRow; ///< 256

// GPU feedback target size (Phase 2b). 384·4 = 1536 bytes/row is already 256-aligned, so the
// readback has no row padding to reason about. A fraction of screen res is plenty — page requests
// are coarse.
inline constexpr uint32_t kVtFeedbackW = 384;
inline constexpr uint32_t kVtFeedbackH = 216;

struct TerrainVtDesc {
  float pageWorldSize = 16.0f;   ///< world metres covered by one MIP-0 page's core
  uint32_t pageTableRes = 256;   ///< mip-0 virtual pages per side; world coverage = res × pageWorldSize
  uint32_t maxMip = 5;           ///< coarsest mip; a mip-m page covers pageWorldSize·2^m metres
};

class TerrainVirtualTexture {
public:
  /// `heightFn(worldX, worldZ)` supplies terrain height for the material splat. Kept simple: the VT
  /// material follows the terrain (grass low, rock on slopes, snow high) so the result reads as land.
  TerrainVirtualTexture(rhi::Device& device, const TerrainVtDesc& desc,
                        std::function<float(float, float)> heightFn);

  /// Chooses the pages the camera needs, generates any that are missing (evicting the least-recently
  /// Frame protocol: beginRequests(), one requestRegion() per visible clipmap ring (a coarser mip
  /// for the farther, larger rings), then commit(device). commit() generates the missing pages,
  /// evicts pages no longer requested, rebuilds the page table (a mip-m page fills the 2^m×2^m block
  /// of finest texels it covers, finest mip winning), and re-uploads what changed. Call commit()
  /// OUTSIDE a beginFrame()/endFrame() pair — it uploads textures.
  void beginRequests();
  void requestRegion(const glm::vec2& worldMin, const glm::vec2& worldMax, uint32_t mip);
  void commit(rhi::Device& device);

  // ── GPU feedback (Phase 2b) ──
  // The renderer copies its feedback render target into this buffer each frame; the next frame
  // processFeedbackRequests() decodes it into the request set. Occlusion- and angle-aware: only the
  // pages actually shaded are asked for, at the mip the pixels need.
  rhi::Buffer* feedbackReadback() const { return m_feedbackReadback.get(); }
  void processFeedbackRequests();
  uint32_t maxMip() const { return m_desc.maxMip; }
  uint32_t lastFeedbackPages() const { return m_lastFeedbackPages; }

  uint32_t atlasBindlessIndex() const;
  uint32_t pageTableBindlessIndex() const;
  float worldSize() const { return float(m_desc.pageTableRes) * m_desc.pageWorldSize; }
  glm::vec2 worldMin() const { return glm::vec2(0.0f); }
  uint32_t pageTableRes() const { return m_desc.pageTableRes; }
  float pageWorldSize() const { return m_desc.pageWorldSize; }

  size_t residentPages() const { return m_resident.size(); }
  uint64_t totalVirtualPages() const {
    return uint64_t(m_desc.pageTableRes) * m_desc.pageTableRes;
  }
  uint32_t pagesGenerated() const { return m_pagesGenerated; }

private:
  struct PageKey {
    uint32_t x, y, mip;
    bool operator==(const PageKey& o) const { return x == o.x && y == o.y && mip == o.mip; }
  };
  struct PageKeyHash {
    size_t operator()(const PageKey& k) const {
      return (uint64_t(k.mip) << 48) ^ (uint64_t(k.y) << 24) ^ uint64_t(k.x);
    }
  };
  struct ResidentPage {
    uint32_t slotX, slotY;               ///< position in the atlas
    std::list<PageKey>::iterator lruIt;  ///< position in the LRU order list
  };

  void generatePage(uint32_t slotX, uint32_t slotY, const PageKey& page);
  void rebuildPageTable();

  rhi::Device& m_device;
  TerrainVtDesc m_desc;
  std::function<float(float, float)> m_heightFn;

  std::shared_ptr<rhi::Texture> m_atlas;      ///< RGBA8, the physical page pool
  std::shared_ptr<rhi::Texture> m_pageTable;  ///< RGBA16F, (slotX, slotY, valid, 0) per virtual page
  std::vector<uint8_t> m_atlasCpu;            ///< CPU mirror, re-uploaded when dirty
  std::vector<uint16_t> m_pageTableCpu;       ///< CPU mirror (halfs), re-uploaded when dirty
  bool m_atlasDirty = false;
  bool m_tableDirty = false;

  std::unordered_map<PageKey, ResidentPage, PageKeyHash> m_resident;
  std::list<PageKey> m_lru;                   ///< front = most recent, back = least recent
  std::vector<std::pair<uint32_t, uint32_t>> m_freeSlots;
  uint32_t m_pagesGenerated = 0;

  std::unordered_map<PageKey, char, PageKeyHash> m_requested; ///< keys asked for this frame

  std::shared_ptr<rhi::Buffer> m_feedbackReadback; ///< renderer copies its feedback RT here
  uint32_t m_lastFeedbackPages = 0;
};

} // namespace tucano::terrain
