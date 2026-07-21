#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

struct ImageData {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels; // RGBA8
};

ImageData loadImageRGBA8(const std::string& path);
ImageData loadImageRGBA8FromMemory(const uint8_t* data, size_t size);

} // namespace tucano
