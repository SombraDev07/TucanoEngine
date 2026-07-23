#include "Renderer/DevTexture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace tucano::devtex {
namespace {

struct Rgba {
  uint8_t r, g, b, a;
};

// Dark neutral greys: bright enough to read the pattern under sun, dark enough that the surface
// never competes with real art, and neutral so it doesn't tint under coloured lighting.
constexpr Rgba kCheckDark{58, 61, 66, 255};
constexpr Rgba kCheckLight{78, 82, 88, 255};
constexpr Rgba kGridLine{206, 212, 218, 255};  // thin white cell grid
constexpr Rgba kMajorLine{236, 240, 244, 255}; // brighter every 4th line
constexpr Rgba kOutline{224, 228, 232, 255};   // border, reads as an edge highlight on a cube
constexpr Rgba kLabel{232, 236, 240, 255};

uint8_t mix8(uint8_t a, uint8_t b, float t) {
  return static_cast<uint8_t>(std::lround(a + (b - a) * std::clamp(t, 0.0f, 1.0f)));
}

Rgba mixRgba(const Rgba& a, const Rgba& b, float t) {
  return {mix8(a.r, b.r, t), mix8(a.g, b.g, t), mix8(a.b, b.b, t), 255};
}

// ── Minimal 5x7 bitmap font ────────────────────────────
// Only the glyphs a scale label needs. Each row is 5 bits, MSB on the left.
const uint8_t* glyphFor(char c) {
  static const uint8_t k0[7]  = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
  static const uint8_t k1[7]  = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static const uint8_t k2[7]  = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
  static const uint8_t k3[7]  = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
  static const uint8_t k4[7]  = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
  static const uint8_t k5[7]  = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
  static const uint8_t k6[7]  = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
  static const uint8_t k7[7]  = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
  static const uint8_t k8[7]  = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
  static const uint8_t k9[7]  = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
  static const uint8_t kDot[7]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
  static const uint8_t kM[7]    = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15};
  static const uint8_t kBlank[7] = {0, 0, 0, 0, 0, 0, 0};

  switch (c) {
    case '0': return k0;
    case '1': return k1;
    case '2': return k2;
    case '3': return k3;
    case '4': return k4;
    case '5': return k5;
    case '6': return k6;
    case '7': return k7;
    case '8': return k8;
    case '9': return k9;
    case '.': return kDot;
    case 'm': return kM;
    default:  return kBlank;
  }
}

// Draws `text` with its top-left at (ox, oy), scaled by `px` pixels per font pixel.
void drawText(std::vector<Rgba>& img, uint32_t size, int ox, int oy, const std::string& text,
              int px, const Rgba& colour) {
  constexpr int kGlyphW = 5, kGlyphH = 7, kSpacing = 1;
  int penX = ox;
  for (char c : text) {
    const uint8_t* rows = glyphFor(c);
    for (int gy = 0; gy < kGlyphH; ++gy) {
      for (int gx = 0; gx < kGlyphW; ++gx) {
        if ((rows[gy] & (1u << (kGlyphW - 1 - gx))) == 0) continue;
        for (int sy = 0; sy < px; ++sy) {
          for (int sx = 0; sx < px; ++sx) {
            const int x = penX + gx * px + sx;
            const int y = oy + gy * px + sy;
            if (x < 0 || y < 0 || x >= int(size) || y >= int(size)) continue;
            img[size_t(y) * size + size_t(x)] = colour;
          }
        }
      }
    }
    penX += (kGlyphW + kSpacing) * px;
  }
}

std::shared_ptr<Texture> upload(rhi::Device& device, const std::vector<Rgba>& pixels, uint32_t size,
                                bool srgb, const char* name) {
  rhi::TextureDesc desc{};
  desc.width = size;
  desc.height = size;
  desc.format = srgb ? rhi::Format::R8G8B8A8_UNORM_SRGB : rhi::Format::R8G8B8A8_UNORM;
  desc.usage = rhi::TextureUsage::ShaderResource;
  desc.debugName = name;
  return Texture::create(device, desc, pixels.data(), size * uint32_t(sizeof(Rgba)));
}

// Shared generator. `label` is drawn in the corners when non-empty; the floor variant leaves it
// off because its tile spans many metres and any fixed number would be a lie.
std::vector<Rgba> buildChecker(uint32_t size, uint32_t cells, const std::string& label,
                               bool drawOutline) {
  const uint32_t cell = std::max(1u, size / cells);
  const float lineHalf = std::max(1.0f, float(size) / 512.0f);
  std::vector<Rgba> pixels(size_t(size) * size);

  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const uint32_t cx = x / cell;
      const uint32_t cy = y / cell;
      Rgba c = (((cx + cy) & 1u) != 0) ? kCheckDark : kCheckLight;

      const float fx = float(x % cell);
      const float fy = float(y % cell);
      const float dEdge = std::min({fx, fy, float(cell) - 1.0f - fx, float(cell) - 1.0f - fy});

      // Thin white grid on the cell boundaries; every 4th line is brighter for counting.
      if (dEdge <= lineHalf) {
        const bool major = ((cx % 4u) == 0 && fx <= lineHalf) || ((cy % 4u) == 0 && fy <= lineHalf);
        const float t = 1.0f - std::clamp(dEdge / lineHalf, 0.0f, 1.0f);
        c = mixRgba(c, major ? kMajorLine : kGridLine, (major ? 0.9f : 0.55f) * t);
      }

      pixels[size_t(y) * size + x] = c;
    }
  }

  // Border highlight: on a cube this reads as a lit edge and makes faces easy to tell apart.
  if (drawOutline) {
    const int w = std::max(1, int(size) / 256);
    for (uint32_t i = 0; i < size; ++i) {
      for (int k = 0; k < w; ++k) {
        pixels[size_t(k) * size + i] = kOutline;
        pixels[size_t(size - 1 - k) * size + i] = kOutline;
        pixels[size_t(i) * size + k] = kOutline;
        pixels[size_t(i) * size + (size - 1 - k)] = kOutline;
      }
    }
  }

  if (!label.empty()) {
    const int px = std::max(1, int(size) / 128);
    const int margin = int(size) / 24;
    drawText(pixels, size, margin, margin, label, px, kLabel);
  }
  return pixels;
}

} // namespace

std::shared_ptr<Texture> checkerAlbedo(rhi::Device& device, uint32_t size, uint32_t cells) {
  size = std::max(size, 32u);
  cells = std::max(cells, 1u);
  // One UV tile spans one world unit on a unit primitive, so the tile is the metre marker.
  auto pixels = buildChecker(size, cells, "1 m", /*drawOutline=*/true);
  return upload(device, pixels, size, /*srgb=*/true, "DevChecker_Albedo");
}

std::shared_ptr<Texture> floorAlbedo(rhi::Device& device, uint32_t size, uint32_t cells) {
  size = std::max(size, 32u);
  cells = std::max(cells, 1u);
  auto pixels = buildChecker(size, cells, /*label=*/"", /*drawOutline=*/false);
  return upload(device, pixels, size, /*srgb=*/true, "DevChecker_Floor");
}

std::shared_ptr<Texture> checkerNormal(rhi::Device& device, uint32_t size, uint32_t cells) {
  size = std::max(size, 32u);
  cells = std::max(cells, 1u);

  const uint32_t cell = std::max(1u, size / cells);
  std::vector<Rgba> pixels(size_t(size) * size);

  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const float fx = float(x % cell) / float(cell);
      const float fy = float(y % cell) / float(cell);

      // Shallow bevel near the cell edges so lighting picks the grid up without looking embossed.
      constexpr float kEdge = 0.10f;
      float nx = 0.0f, ny = 0.0f;
      if (fx < kEdge) nx = -(1.0f - fx / kEdge);
      else if (fx > 1.0f - kEdge) nx = (1.0f - (1.0f - fx) / kEdge);
      if (fy < kEdge) ny = -(1.0f - fy / kEdge);
      else if (fy > 1.0f - kEdge) ny = (1.0f - (1.0f - fy) / kEdge);

      constexpr float kStrength = 0.25f;
      nx *= kStrength;
      ny *= kStrength;
      const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));

      pixels[size_t(y) * size + x] = {
          static_cast<uint8_t>(std::lround((nx * 0.5f + 0.5f) * 255.0f)),
          static_cast<uint8_t>(std::lround((ny * 0.5f + 0.5f) * 255.0f)),
          static_cast<uint8_t>(std::lround((nz * 0.5f + 0.5f) * 255.0f)),
          255};
    }
  }

  // Normal maps are data, never sRGB.
  return upload(device, pixels, size, /*srgb=*/false, "DevChecker_Normal");
}

std::shared_ptr<Texture> defaultAlbedo(rhi::Device& device) {
  static std::shared_ptr<Texture> cached;
  static std::once_flag once;
  std::call_once(once, [&] { cached = checkerAlbedo(device); });
  return cached;
}

std::shared_ptr<Texture> defaultFloor(rhi::Device& device) {
  static std::shared_ptr<Texture> cached;
  static std::once_flag once;
  std::call_once(once, [&] { cached = floorAlbedo(device); });
  return cached;
}

std::shared_ptr<Texture> defaultNormal(rhi::Device& device) {
  static std::shared_ptr<Texture> cached;
  static std::once_flag once;
  std::call_once(once, [&] { cached = checkerNormal(device); });
  return cached;
}

} // namespace tucano::devtex
