#include "Renderer/Weather/CloudNoise.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace tucano {
namespace cloudnoise {
namespace {

// --- Hashing (deterministic, seedless) ---

uint32_t hash3(int32_t x, int32_t y, int32_t z) {
  uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

float hashToUnit(uint32_t h) { return float(h & 0xffffffu) / float(0x1000000); }

// --- Tileable Worley (inverted: 1 at feature points, 0 far) ---

float worley(float px, float py, float pz, int period) {
  const int cx = int(std::floor(px));
  const int cy = int(std::floor(py));
  const int cz = int(std::floor(pz));
  float minD2 = 1e9f;
  for (int oz = -1; oz <= 1; ++oz) {
    for (int oy = -1; oy <= 1; ++oy) {
      for (int ox = -1; ox <= 1; ++ox) {
        const int ix = cx + ox;
        const int iy = cy + oy;
        const int iz = cz + oz;
        const int wx = ((ix % period) + period) % period;
        const int wy = ((iy % period) + period) % period;
        const int wz = ((iz % period) + period) % period;
        const uint32_t h = hash3(wx, wy, wz);
        const float fx = float(ix) + hashToUnit(h);
        const float fy = float(iy) + hashToUnit(h * 0x9e3779b9u + 1u);
        const float fz = float(iz) + hashToUnit(h * 0x85ebca6bu + 2u);
        const float dx = fx - px;
        const float dy = fy - py;
        const float dz = fz - pz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        minD2 = std::min(minD2, d2);
      }
    }
  }
  return std::clamp(1.0f - std::sqrt(minD2), 0.0f, 1.0f);
}

// u,v,w in [0,1); freq = cells along each axis (tileable).
float worleyFbm(float u, float v, float w, int freq) {
  const float a = worley(u * float(freq), v * float(freq), w * float(freq), freq);
  const float b = worley(u * float(freq * 2), v * float(freq * 2), w * float(freq * 2), freq * 2);
  const float c = worley(u * float(freq * 4), v * float(freq * 4), w * float(freq * 4), freq * 4);
  return a * 0.625f + b * 0.25f + c * 0.125f;
}

// --- Tileable Perlin ---

float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

float grad(uint32_t h, float x, float y, float z) {
  switch (h & 15u) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return -x + z;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return -y + z;
    case 10: return y - z;
    case 11: return -y - z;
    case 12: return y + x;
    case 13: return -y + z;
    case 14: return y - x;
    default: return -y - z;
  }
}

float perlin(float px, float py, float pz, int period) {
  const int x0 = int(std::floor(px));
  const int y0 = int(std::floor(py));
  const int z0 = int(std::floor(pz));
  const float fx = px - float(x0);
  const float fy = py - float(y0);
  const float fz = pz - float(z0);
  const float u = fade(fx);
  const float v = fade(fy);
  const float w = fade(fz);
  auto g = [&](int ix, int iy, int iz, float dx, float dy, float dz) {
    const int wx = (((x0 + ix) % period) + period) % period;
    const int wy = (((y0 + iy) % period) + period) % period;
    const int wz = (((z0 + iz) % period) + period) % period;
    return grad(hash3(wx, wy, wz), fx - dx, fy - dy, fz - dz);
  };
  const float n000 = g(0, 0, 0, 0, 0, 0);
  const float n100 = g(1, 0, 0, 1, 0, 0);
  const float n010 = g(0, 1, 0, 0, 1, 0);
  const float n110 = g(1, 1, 0, 1, 1, 0);
  const float n001 = g(0, 0, 1, 0, 0, 1);
  const float n101 = g(1, 0, 1, 1, 0, 1);
  const float n011 = g(0, 1, 1, 0, 1, 1);
  const float n111 = g(1, 1, 1, 1, 1, 1);
  const float nx00 = n000 + u * (n100 - n000);
  const float nx10 = n010 + u * (n110 - n010);
  const float nx01 = n001 + u * (n101 - n001);
  const float nx11 = n011 + u * (n111 - n011);
  const float nxy0 = nx00 + v * (nx10 - nx00);
  const float nxy1 = nx01 + v * (nx11 - nx01);
  return nxy0 + w * (nxy1 - nxy0); // ~[-1,1]
}

float perlinFbm(float u, float v, float w, int baseFreq, int octaves) {
  float sum = 0.f;
  float amp = 1.f;
  float norm = 0.f;
  int freq = baseFreq;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * perlin(u * float(freq), v * float(freq), w * float(freq), freq);
    norm += amp;
    amp *= 0.5f;
    freq *= 2;
  }
  return sum / std::max(norm, 1e-4f); // ~[-1,1]
}

float remap(float x, float a, float b, float c, float d) {
  return c + (x - a) / std::max(b - a, 1e-5f) * (d - c);
}

uint8_t toU8(float x) { return uint8_t(std::clamp(x, 0.f, 1.f) * 255.f + 0.5f); }

using SliceFn = void (*)(uint32_t z, uint32_t size, uint8_t* out);

void bakeVolume(uint32_t size, uint8_t* out, SliceFn fn) {
  const uint32_t hw = std::max(2u, std::thread::hardware_concurrency());
  std::vector<std::future<void>> jobs;
  std::atomic<uint32_t> next{0};
  for (uint32_t t = 0; t < hw; ++t) {
    jobs.push_back(std::async(std::launch::async, [&]() {
      for (;;) {
        const uint32_t z = next.fetch_add(1);
        if (z >= size) {
          return;
        }
        fn(z, size, out + size_t(z) * size * size * 4);
      }
    }));
  }
  for (auto& j : jobs) {
    j.get();
  }
}

void baseSlice(uint32_t z, uint32_t size, uint8_t* slice) {
  const float inv = 1.0f / float(size);
  const float w = (float(z) + 0.5f) * inv;
  for (uint32_t y = 0; y < size; ++y) {
    const float v = (float(y) + 0.5f) * inv;
    for (uint32_t x = 0; x < size; ++x) {
      const float u = (float(x) + 0.5f) * inv;
      float pn = perlinFbm(u, v, w, 4, 7) * 0.5f + 0.5f;
      const float w4 = worleyFbm(u, v, w, 4);
      const float w8 = worleyFbm(u, v, w, 8);
      const float w16 = worleyFbm(u, v, w, 16);
      // Perlin-Worley: dilate perlin billows with worley (Schneider / Nubis)
      const float pw = remap(pn, 0.f, 1.f, w4, 1.f);
      uint8_t* px = slice + size_t(y * size + x) * 4;
      px[0] = toU8(pw);
      px[1] = toU8(w4);
      px[2] = toU8(w8);
      px[3] = toU8(w16);
    }
  }
}

void detailSlice(uint32_t z, uint32_t size, uint8_t* slice) {
  const float inv = 1.0f / float(size);
  const float w = (float(z) + 0.5f) * inv;
  for (uint32_t y = 0; y < size; ++y) {
    const float v = (float(y) + 0.5f) * inv;
    for (uint32_t x = 0; x < size; ++x) {
      const float u = (float(x) + 0.5f) * inv;
      uint8_t* px = slice + size_t(y * size + x) * 4;
      px[0] = toU8(worleyFbm(u, v, w, 2));
      px[1] = toU8(worleyFbm(u, v, w, 4));
      px[2] = toU8(worleyFbm(u, v, w, 8));
      px[3] = 255;
    }
  }
}

std::vector<uint8_t> bakeOrLoad(const char* cacheDir, const char* name, uint32_t size, SliceFn fn) {
  const size_t bytes = size_t(size) * size * size * 4;
  std::filesystem::path path;
  if (cacheDir && *cacheDir) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    path = std::filesystem::path(cacheDir) / name;
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) == bytes) {
      std::vector<uint8_t> data(bytes);
      std::ifstream f(path, std::ios::binary);
      if (f.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes))) {
        std::cout << "[CloudNoise] loaded cache " << path.string() << "\n";
        return data;
      }
    }
  }
  std::cout << "[CloudNoise] baking " << name << " (" << size << "^3)...\n";
  std::vector<uint8_t> data(bytes);
  bakeVolume(size, data.data(), fn);
  if (!path.empty()) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(bytes));
  }
  return data;
}

} // namespace

std::vector<uint8_t> bakeBase(const char* cacheDir) {
  return bakeOrLoad(cacheDir, "cloud_noise_base_128_v1.raw", kBaseSize, baseSlice);
}

std::vector<uint8_t> bakeDetail(const char* cacheDir) {
  return bakeOrLoad(cacheDir, "cloud_noise_detail_32_v1.raw", kDetailSize, detailSlice);
}

} // namespace cloudnoise
} // namespace tucano
