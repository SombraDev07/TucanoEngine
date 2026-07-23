#include "AssetPipeline/ImageLoader.h"

#include <stb_image.h>
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace tucano {
namespace {

bool hasExtension(const std::string& path, const char* ext) {
  const size_t n = std::char_traits<char>::length(ext);
  if (path.size() < n) return false;
  return std::equal(path.end() - n, path.end(), ext, [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == b;
  });
}

/// OpenEXR via tinyexr. stb cannot decode EXR, and HDRI libraries hand it out as often as .hdr.
ImageDataF loadExrRGBA32F(const std::string& path) {
  float* rgba = nullptr;
  int w = 0, h = 0;
  const char* err = nullptr;

  // LoadEXR always returns 4 channels, which is exactly the layout we want.
  if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS) {
    const std::string message = err ? err : "unknown error";
    FreeEXRErrorMessage(err);
    throw std::runtime_error("LoadEXR failed: " + path + " (" + message + ")");
  }

  ImageDataF img;
  img.width = static_cast<uint32_t>(w);
  img.height = static_cast<uint32_t>(h);
  img.pixels.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
  free(rgba); // tinyexr allocates with malloc
  return img;
}

} // namespace

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

ImageDataF loadImageRGBA32F(const std::string& path) {
  if (hasExtension(path, ".exr")) return loadExrRGBA32F(path);

  int w = 0, h = 0, comp = 0;
  float* data = stbi_loadf(path.c_str(), &w, &h, &comp, 4);
  if (!data) {
    throw std::runtime_error(std::string("stbi_loadf failed: ") + path + " (" + stbi_failure_reason() + ")");
  }
  ImageDataF img;
  img.width = static_cast<uint32_t>(w);
  img.height = static_cast<uint32_t>(h);
  img.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return img;
}

} // namespace tucano
