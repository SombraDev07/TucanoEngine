#include "AssetPipeline/ImageLoader.h"

#include <stb_image.h>

#include <stdexcept>

namespace tucano {

ImageData loadImageRGBA8(const std::string& path) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
  if (!data) {
    throw std::runtime_error(std::string("stbi_load failed: ") + path + " (" + stbi_failure_reason() + ")");
  }
  ImageData img;
  img.width = static_cast<uint32_t>(w);
  img.height = static_cast<uint32_t>(h);
  img.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return img;
}

ImageData loadImageRGBA8FromMemory(const uint8_t* data, size_t size) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
  if (!pixels) {
    throw std::runtime_error(std::string("stbi_load_from_memory failed: ") + stbi_failure_reason());
  }
  ImageData img;
  img.width = static_cast<uint32_t>(w);
  img.height = static_cast<uint32_t>(h);
  img.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
  stbi_image_free(pixels);
  return img;
}

} // namespace tucano
