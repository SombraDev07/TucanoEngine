#pragma once

#include "RHI/RHI.h"

#include <memory>
#include <string>

namespace tucano {

class Texture {
public:
  static std::shared_ptr<Texture> create(rhi::Device& device, const rhi::TextureDesc& desc,
                                         const void* data = nullptr, uint32_t rowPitch = 0);
  static std::shared_ptr<Texture> loadFromFile(rhi::Device& device, const std::string& path, bool srgb);

  rhi::Texture& resource() { return *m_texture; }
  const rhi::Texture& resource() const { return *m_texture; }
  std::shared_ptr<rhi::Texture> shared() const { return m_texture; }

private:
  std::shared_ptr<rhi::Texture> m_texture;
};

} // namespace tucano
