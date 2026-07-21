#include "Renderer/Texture.h"
#include "AssetPipeline/DdsLoader.h"
#include "AssetPipeline/ImageLoader.h"

#include <filesystem>

namespace tucano {

std::shared_ptr<Texture> Texture::create(rhi::Device& device, const rhi::TextureDesc& desc, const void* data,
                                         uint32_t rowPitch) {
  auto t = std::shared_ptr<Texture>(new Texture());
  t->m_texture = device.createTexture(desc, data, rowPitch);
  return t;
}

std::shared_ptr<Texture> Texture::loadFromFile(rhi::Device& device, const std::string& path, bool srgb) {
  ImageData img;
  const std::filesystem::path p(path);
  const std::string ext = p.extension().string();
  if (ext == ".dds" || ext == ".DDS") {
    img = loadDdsRGBA8(path);
  } else {
    try {
      img = loadImageRGBA8(path);
    } catch (const std::exception&) {
      // Cry .tif often fails with stb — try sibling .dds
      std::filesystem::path dds = p;
      dds.replace_extension(".dds");
      img = loadDdsRGBA8(dds.string());
    }
  }
  rhi::TextureDesc desc{};
  desc.width = img.width;
  desc.height = img.height;
  desc.mipLevels = 1;
  desc.format = srgb ? rhi::Format::R8G8B8A8_UNORM_SRGB : rhi::Format::R8G8B8A8_UNORM;
  desc.usage = rhi::TextureUsage::ShaderResource;
  desc.debugName = path;
  return create(device, desc, img.pixels.data(), img.width * 4);
}

} // namespace tucano
