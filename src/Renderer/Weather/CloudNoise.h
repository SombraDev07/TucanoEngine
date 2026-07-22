#pragma once

#include <cstdint>
#include <vector>

namespace tucano {

// CPU bake of the two 3D noise volumes used by AAA volumetric clouds (Nubis / Hillaire style):
//  - Base 128^3 RGBA8: R = Perlin-Worley, GBA = Worley fbm at increasing frequency.
//  - Detail 32^3 RGBA8: RGB = high-frequency Worley fbm octaves (edge erosion).
// All noise is tileable so the volumes wrap seamlessly. Results are cached on disk.
namespace cloudnoise {

constexpr uint32_t kBaseSize = 128;
constexpr uint32_t kDetailSize = 32;

// Returns tightly packed RGBA8 voxels, slice-major (z * h * w). Uses/creates `cacheDir/<name>` when possible.
std::vector<uint8_t> bakeBase(const char* cacheDir);
std::vector<uint8_t> bakeDetail(const char* cacheDir);

} // namespace cloudnoise
} // namespace tucano
