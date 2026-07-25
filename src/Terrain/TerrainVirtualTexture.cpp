#include "Terrain/TerrainVirtualTexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tucano::terrain {
namespace {

// Minimal float→half for the page table (small values: slot indices and a flag, no precision worry).
uint16_t floatToHalf(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  const uint16_t sign = uint16_t((u >> 16) & 0x8000);
  int32_t exp = int32_t((u >> 23) & 0xFF) - 127;
  uint32_t mant = (u & 0x7FFFFF) >> 13;
  if (exp > 15) return uint16_t(sign | 0x7C00);
  if (exp < -14) return sign;
  return uint16_t(sign | uint16_t((exp + 15) << 10) | uint16_t(mant));
}

// Cheap hashed value noise for material detail. Deterministic, world-space.
float hash2(int x, int y) {
  uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return float(h & 0xFFFFFF) / float(0xFFFFFF);
}
float valueNoise(float x, float y) {
  const int xi = int(std::floor(x)), yi = int(std::floor(y));
  const float fx = x - float(xi), fy = y - float(yi);
  const float u = fx * fx * (3.0f - 2.0f * fx);
  const float v = fy * fy * (3.0f - 2.0f * fy);
  const float a = hash2(xi, yi), b = hash2(xi + 1, yi);
  const float c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

glm::vec3 mixv(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }
float smooth(float e0, float e1, float x) {
  const float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace

TerrainVirtualTexture::TerrainVirtualTexture(rhi::Device& device, const TerrainVtDesc& desc,
                                             std::function<float(float, float)> heightFn)
    : m_device(device), m_desc(desc), m_heightFn(std::move(heightFn)) {
  rhi::TextureDesc ad{};
  ad.width = kVtAtlasSize;
  ad.height = kVtAtlasSize;
  ad.format = rhi::Format::R8G8B8A8_UNORM;
  ad.usage = rhi::TextureUsage::ShaderResource;
  ad.debugName = "TerrainVtAtlas";
  m_atlasCpu.assign(size_t(kVtAtlasSize) * kVtAtlasSize * 4, 0);
  m_atlas = device.createTexture(ad, m_atlasCpu.data(), kVtAtlasSize * 4);

  rhi::TextureDesc pd{};
  pd.width = desc.pageTableRes;
  pd.height = desc.pageTableRes;
  pd.format = rhi::Format::R16G16B16A16_FLOAT;
  pd.usage = rhi::TextureUsage::ShaderResource;
  pd.debugName = "TerrainVtPageTable";
  // (slotX, slotY, valid, 0) per virtual page; all invalid to start.
  m_pageTableCpu.assign(size_t(desc.pageTableRes) * desc.pageTableRes * 4, 0);
  m_pageTable = device.createTexture(pd, m_pageTableCpu.data(),
                                     desc.pageTableRes * 4 * uint32_t(sizeof(uint16_t)));

  m_freeSlots.reserve(kVtMaxResident);
  for (uint32_t y = 0; y < kVtAtlasPagesPerRow; ++y)
    for (uint32_t x = 0; x < kVtAtlasPagesPerRow; ++x) m_freeSlots.emplace_back(x, y);

  rhi::BufferDesc fb{};
  fb.size = uint64_t(kVtFeedbackW) * kVtFeedbackH * sizeof(uint32_t);
  fb.usage = rhi::BufferUsage::Readback;
  fb.debugName = "TerrainVtFeedbackRB";
  m_feedbackReadback = device.createBuffer(fb);
}

void TerrainVirtualTexture::processFeedbackRequests() {
  const uint32_t* fb = static_cast<const uint32_t*>(m_feedbackReadback->mapped());
  if (!fb) return;
  const size_t count = size_t(kVtFeedbackW) * kVtFeedbackH;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t packed = fb[i];
    if ((packed & 0x80000000u) == 0) continue; // undrawn (cleared 0) — sky / outside the VT
    const uint32_t mip = (packed >> 24) & 0x7F;
    const uint32_t py = (packed >> 12) & 0xFFF;
    const uint32_t px = packed & 0xFFF;
    if (mip > m_desc.maxMip) continue;
    m_requested[PageKey{px, py, mip}] = 1;
  }
  m_lastFeedbackPages = uint32_t(m_requested.size());
}

uint32_t TerrainVirtualTexture::atlasBindlessIndex() const {
  return m_atlas ? m_atlas->bindlessIndex() : 0u;
}
uint32_t TerrainVirtualTexture::pageTableBindlessIndex() const {
  return m_pageTable ? m_pageTable->bindlessIndex() : 0u;
}

void TerrainVirtualTexture::generatePage(uint32_t slotX, uint32_t slotY, const PageKey& page) {
  const float pageWorld = m_desc.pageWorldSize * float(1u << page.mip);
  const float coreTexel = pageWorld / float(kVtCore);
  const glm::vec2 pageMin(float(page.x) * pageWorld, float(page.y) * pageWorld);
  // Detail frequency divided by 2^mip so a coarse page shows proportionally coarser detail: it
  // spans more world at the same 128 texels, and a fixed world frequency there would alias badly.
  const float nfreq = 1.0f / float(1u << page.mip);

  const glm::vec3 grass(0.20f, 0.32f, 0.12f);
  const glm::vec3 rock(0.30f, 0.27f, 0.23f);
  const glm::vec3 snow(0.82f, 0.85f, 0.90f);

  for (uint32_t j = 0; j < kVtPageSize; ++j) {
    for (uint32_t i = 0; i < kVtPageSize; ++i) {
      const float wx = pageMin.x + (float(i) - float(kVtBorder) + 0.5f) * coreTexel;
      const float wz = pageMin.y + (float(j) - float(kVtBorder) + 0.5f) * coreTexel;

      const float h = m_heightFn(wx, wz);
      const float e = std::max(1.0f, coreTexel);
      const float hx = m_heightFn(wx + e, wz);
      const float hz = m_heightFn(wx, wz + e);
      const float slope = std::sqrt((h - hx) * (h - hx) + (h - hz) * (h - hz)) / e;

      glm::vec3 c = grass;
      c = mixv(c, rock, smooth(40.0f, 110.0f, h));  // rise into rock
      c = mixv(c, snow, smooth(150.0f, 220.0f, h)); // peaks into snow
      c = mixv(c, rock, smooth(0.55f, 1.3f, slope)); // steep faces are rock regardless of height

      const float d = valueNoise(wx * 0.6f * nfreq, wz * 0.6f * nfreq) * 0.6f +
                      valueNoise(wx * 2.7f * nfreq, wz * 2.7f * nfreq) * 0.4f;
      c *= 0.7f + 0.6f * d;
      c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));

      const size_t atX = size_t(slotX) * kVtPageSize + i;
      const size_t atY = size_t(slotY) * kVtPageSize + j;
      const size_t o = (atY * kVtAtlasSize + atX) * 4;
      m_atlasCpu[o + 0] = uint8_t(c.r * 255.0f + 0.5f);
      m_atlasCpu[o + 1] = uint8_t(c.g * 255.0f + 0.5f);
      m_atlasCpu[o + 2] = uint8_t(c.b * 255.0f + 0.5f);
      m_atlasCpu[o + 3] = 255;
    }
  }
  m_atlasDirty = true;
  ++m_pagesGenerated;
}

void TerrainVirtualTexture::rebuildPageTable() {
  const uint32_t res = m_desc.pageTableRes;
  std::fill(m_pageTableCpu.begin(), m_pageTableCpu.end(), uint16_t(0)); // all invalid (w=0)

  // Coarse pages first, so a finer page overwrites their finest-texel cells where they overlap —
  // the pixel gets the sharpest resident page covering it, with the coarse one as automatic fallback.
  std::vector<std::pair<PageKey, ResidentPage>> pages(m_resident.begin(), m_resident.end());
  std::sort(pages.begin(), pages.end(),
            [](const auto& a, const auto& b) { return a.first.mip > b.first.mip; });

  for (const auto& [key, rp] : pages) {
    const uint32_t span = 1u << key.mip; // finest texels per side this page covers
    const uint16_t hsx = floatToHalf(float(rp.slotX));
    const uint16_t hsy = floatToHalf(float(rp.slotY));
    const uint16_t hmip = floatToHalf(float(key.mip));
    const uint16_t hval = floatToHalf(1.0f);
    for (uint32_t dy = 0; dy < span; ++dy) {
      const uint32_t fy = key.y * span + dy;
      if (fy >= res) break;
      for (uint32_t dx = 0; dx < span; ++dx) {
        const uint32_t fx = key.x * span + dx;
        if (fx >= res) break;
        const size_t idx = (size_t(fy) * res + fx) * 4;
        m_pageTableCpu[idx + 0] = hsx;
        m_pageTableCpu[idx + 1] = hsy;
        m_pageTableCpu[idx + 2] = hmip;
        m_pageTableCpu[idx + 3] = hval;
      }
    }
  }
  m_tableDirty = true;
}

void TerrainVirtualTexture::beginRequests() { m_requested.clear(); }

void TerrainVirtualTexture::requestRegion(const glm::vec2& worldMin, const glm::vec2& worldMax,
                                          uint32_t mip) {
  mip = std::min(mip, m_desc.maxMip);
  const float pageWorld = m_desc.pageWorldSize * float(1u << mip);
  const int pagesAtMip = int(m_desc.pageTableRes >> mip);
  const int px0 = int(std::floor(worldMin.x / pageWorld));
  const int py0 = int(std::floor(worldMin.y / pageWorld));
  const int px1 = int(std::floor(worldMax.x / pageWorld));
  const int py1 = int(std::floor(worldMax.y / pageWorld));
  for (int py = py0; py <= py1; ++py) {
    if (py < 0 || py >= pagesAtMip) continue;
    for (int px = px0; px <= px1; ++px) {
      if (px < 0 || px >= pagesAtMip) continue;
      m_requested[PageKey{uint32_t(px), uint32_t(py), mip}] = 1;
    }
  }
}

void TerrainVirtualTexture::commit(rhi::Device& device) {
  // Free the slots of pages no longer requested first, so this frame's requests always fit when
  // they number under the atlas capacity — eviction never touches a page still on screen.
  for (auto it = m_resident.begin(); it != m_resident.end();) {
    if (m_requested.find(it->first) == m_requested.end()) {
      m_freeSlots.emplace_back(it->second.slotX, it->second.slotY);
      m_lru.erase(it->second.lruIt);
      it = m_resident.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto& [key, _] : m_requested) {
    auto it = m_resident.find(key);
    if (it != m_resident.end()) {
      m_lru.erase(it->second.lruIt);
      m_lru.push_front(key);
      it->second.lruIt = m_lru.begin();
      continue;
    }
    uint32_t sx, sy;
    if (!m_freeSlots.empty()) {
      std::tie(sx, sy) = m_freeSlots.back();
      m_freeSlots.pop_back();
    } else {
      const PageKey victim = m_lru.back();
      m_lru.pop_back();
      auto vit = m_resident.find(victim);
      sx = vit->second.slotX;
      sy = vit->second.slotY;
      m_resident.erase(vit);
    }
    generatePage(sx, sy, key);
    m_lru.push_front(key);
    m_resident[key] = ResidentPage{sx, sy, m_lru.begin()};
  }

  rebuildPageTable();

  if (m_atlasDirty) {
    device.uploadTexture(*m_atlas, m_atlasCpu.data(), kVtAtlasSize, kVtAtlasSize, kVtAtlasSize * 4);
    m_atlasDirty = false;
  }
  if (m_tableDirty) {
    device.uploadTexture(*m_pageTable, m_pageTableCpu.data(), m_desc.pageTableRes, m_desc.pageTableRes,
                         m_desc.pageTableRes * 4 * uint32_t(sizeof(uint16_t)));
    m_tableDirty = false;
  }
}

} // namespace tucano::terrain
