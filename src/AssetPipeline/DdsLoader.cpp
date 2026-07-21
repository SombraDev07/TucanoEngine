#include "AssetPipeline/DdsLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace tucano {
namespace {

constexpr uint32_t kDxgiBC4Unorm = 72;
constexpr uint32_t kDxgiBC5Unorm = 84;
constexpr uint32_t kDxgiR8G8B8A8Unorm = 28;
constexpr uint32_t kDxgiB8G8R8A8Unorm = 87;

void decodeBC4Block(const uint8_t block[8], uint8_t out[4][4]) {
  const uint8_t r0 = block[0];
  const uint8_t r1 = block[1];
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) {
    bits |= uint64_t(block[2 + i]) << (8 * i);
  }
  uint8_t palette[8];
  palette[0] = r0;
  palette[1] = r1;
  if (r0 > r1) {
    for (int i = 2; i < 8; ++i) {
      palette[i] = uint8_t(((8 - i) * r0 + (i - 1) * r1) / 7);
    }
  } else {
    for (int i = 2; i < 6; ++i) {
      palette[i] = uint8_t(((6 - i) * r0 + (i - 1) * r1) / 5);
    }
    palette[6] = 0;
    palette[7] = 255;
  }
  for (int i = 0; i < 16; ++i) {
    const uint32_t idx = uint32_t((bits >> (3 * i)) & 0x7);
    out[i >> 2][i & 3] = palette[idx];
  }
}

ImageData decodeBc4(uint32_t w, uint32_t h, const uint8_t* data, size_t size) {
  const uint32_t bw = (w + 3) / 4;
  const uint32_t bh = (h + 3) / 4;
  if (size < size_t(bw) * bh * 8) {
    throw std::runtime_error("BC4 DDS truncated");
  }
  ImageData img;
  img.width = w;
  img.height = h;
  img.pixels.assign(size_t(w) * h * 4, 255);
  for (uint32_t by = 0; by < bh; ++by) {
    for (uint32_t bx = 0; bx < bw; ++bx) {
      uint8_t block[4][4];
      decodeBC4Block(data + (size_t(by) * bw + bx) * 8, block);
      for (uint32_t py = 0; py < 4; ++py) {
        for (uint32_t px = 0; px < 4; ++px) {
          const uint32_t x = bx * 4 + px;
          const uint32_t y = by * 4 + py;
          if (x >= w || y >= h)
            continue;
          const size_t o = (size_t(y) * w + x) * 4;
          const uint8_t v = block[py][px];
          img.pixels[o + 0] = v;
          img.pixels[o + 1] = v;
          img.pixels[o + 2] = v;
          img.pixels[o + 3] = 255;
        }
      }
    }
  }
  return img;
}

ImageData decodeBc5(uint32_t w, uint32_t h, const uint8_t* data, size_t size) {
  const uint32_t bw = (w + 3) / 4;
  const uint32_t bh = (h + 3) / 4;
  if (size < size_t(bw) * bh * 16) {
    throw std::runtime_error("BC5 DDS truncated");
  }
  ImageData img;
  img.width = w;
  img.height = h;
  img.pixels.assign(size_t(w) * h * 4, 255);
  for (uint32_t by = 0; by < bh; ++by) {
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint8_t* blk = data + (size_t(by) * bw + bx) * 16;
      uint8_t r[4][4], g[4][4];
      decodeBC4Block(blk, r);
      decodeBC4Block(blk + 8, g);
      for (uint32_t py = 0; py < 4; ++py) {
        for (uint32_t px = 0; px < 4; ++px) {
          const uint32_t x = bx * 4 + px;
          const uint32_t y = by * 4 + py;
          if (x >= w || y >= h)
            continue;
          const size_t o = (size_t(y) * w + x) * 4;
          img.pixels[o + 0] = r[py][px];
          img.pixels[o + 1] = g[py][px];
          // Reconstruct Z for normals
          const float nx = r[py][px] / 255.0f * 2.0f - 1.0f;
          const float ny = g[py][px] / 255.0f * 2.0f - 1.0f;
          const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
          img.pixels[o + 2] = uint8_t(std::clamp(nz * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
          img.pixels[o + 3] = 255;
        }
      }
    }
  }
  return img;
}

} // namespace

ImageData loadDdsRGBA8(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open DDS: " + path);
  }
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (file.size() < 148 || file[0] != 'D' || file[1] != 'D' || file[2] != 'S' || file[3] != ' ') {
    throw std::runtime_error("not a DDS: " + path);
  }

  const uint32_t height = *reinterpret_cast<const uint32_t*>(file.data() + 12);
  const uint32_t width = *reinterpret_cast<const uint32_t*>(file.data() + 16);
  const uint32_t pfFlags = *reinterpret_cast<const uint32_t*>(file.data() + 80);
  const uint32_t fourCC = *reinterpret_cast<const uint32_t*>(file.data() + 84);

  uint32_t dxgi = 0;
  size_t dataOff = 128;
  const bool hasFourCC = (pfFlags & 0x4) != 0;
  if (hasFourCC && fourCC == 0x30315844) { // 'DX10'
    dxgi = *reinterpret_cast<const uint32_t*>(file.data() + 128);
    dataOff = 148;
  } else if (!hasFourCC && (pfFlags & 0x40) != 0) {
    // Uncompressed RGB(A)
    const uint32_t rgbBits = *reinterpret_cast<const uint32_t*>(file.data() + 88);
    const uint32_t rMask = *reinterpret_cast<const uint32_t*>(file.data() + 92);
    const uint32_t bMask = *reinterpret_cast<const uint32_t*>(file.data() + 100);
    if (rgbBits != 32) {
      throw std::runtime_error("only 32-bit uncompressed DDS supported: " + path);
    }
    ImageData img;
    img.width = width;
    img.height = height;
    img.pixels.resize(size_t(width) * height * 4);
    if (file.size() < dataOff + img.pixels.size()) {
      throw std::runtime_error("uncompressed DDS truncated: " + path);
    }
    const uint8_t* data = file.data() + dataOff;
    const bool bgra = (rMask == 0x00ff0000) || (bMask == 0x000000ff && rMask != 0x000000ff);
    for (size_t i = 0; i < size_t(width) * height; ++i) {
      if (bgra) {
        img.pixels[i * 4 + 0] = data[i * 4 + 2];
        img.pixels[i * 4 + 1] = data[i * 4 + 1];
        img.pixels[i * 4 + 2] = data[i * 4 + 0];
        img.pixels[i * 4 + 3] = data[i * 4 + 3];
      } else {
        img.pixels[i * 4 + 0] = data[i * 4 + 0];
        img.pixels[i * 4 + 1] = data[i * 4 + 1];
        img.pixels[i * 4 + 2] = data[i * 4 + 2];
        img.pixels[i * 4 + 3] = data[i * 4 + 3];
      }
    }
    return img;
  } else if (hasFourCC) {
    throw std::runtime_error("unsupported DDS fourCC in: " + path);
  } else {
    throw std::runtime_error("unsupported DDS pixel format: " + path);
  }

  const uint8_t* data = file.data() + dataOff;
  const size_t dataSize = file.size() - dataOff;

  if (dxgi == kDxgiBC4Unorm) {
    return decodeBc4(width, height, data, dataSize);
  }
  if (dxgi == kDxgiBC5Unorm) {
    return decodeBc5(width, height, data, dataSize);
  }
  if (dxgi == kDxgiR8G8B8A8Unorm || dxgi == kDxgiB8G8R8A8Unorm) {
    ImageData img;
    img.width = width;
    img.height = height;
    img.pixels.resize(size_t(width) * height * 4);
    if (dataSize < img.pixels.size()) {
      throw std::runtime_error("RGBA DDS truncated: " + path);
    }
    if (dxgi == kDxgiR8G8B8A8Unorm) {
      std::memcpy(img.pixels.data(), data, img.pixels.size());
    } else {
      for (size_t i = 0; i < size_t(width) * height; ++i) {
        img.pixels[i * 4 + 0] = data[i * 4 + 2];
        img.pixels[i * 4 + 1] = data[i * 4 + 1];
        img.pixels[i * 4 + 2] = data[i * 4 + 0];
        img.pixels[i * 4 + 3] = data[i * 4 + 3];
      }
    }
    return img;
  }
  throw std::runtime_error("unsupported DXGI format " + std::to_string(dxgi) + " in " + path);
}

} // namespace tucano
