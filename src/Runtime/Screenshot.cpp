#include "Runtime/Screenshot.h"

#include <stb_image_write.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace tucano {

// The readback resource is an ordinary RHI buffer: createBuffer() keeps a Readback buffer
// persistently mapped, and copyTextureToBuffer() already applies the backend's row alignment, so
// nothing here needs to know which backend is running.
struct ScreenshotPending::Impl {
  std::shared_ptr<rhi::Buffer> readback;
};

ScreenshotPending beginScreenshot(rhi::Device& device, rhi::CommandList& cmd, rhi::Texture& backbuffer) {
  using namespace rhi;

  ScreenshotPending out;
  out.impl = std::make_shared<ScreenshotPending::Impl>();
  out.width = backbuffer.width();
  out.height = backbuffer.height();
  out.rowPitch = (formatRowPitch(Format::R8G8B8A8_UNORM, out.width) + 255u) & ~255u;

  BufferDesc bd{};
  bd.size = static_cast<uint64_t>(out.rowPitch) * out.height;
  bd.usage = BufferUsage::Readback;
  bd.debugName = "ScreenshotReadback";
  out.impl->readback = device.createBuffer(bd, nullptr);

  // Flush explicitly: transition() only queues the barrier. Without this the copy could be recorded
  // ahead of its own barrier and read the target in the wrong state — which is how screenshots
  // ended up missing the last draws of the frame.
  cmd.transition(backbuffer, ResourceState::CopySrc);
  cmd.flushBarriers();

  cmd.copyTextureToBuffer(backbuffer, *out.impl->readback, out.width, out.height,
                          Format::R8G8B8A8_UNORM);
  return out;
}

std::vector<uint8_t> readScreenshotPixels(const ScreenshotPending& pending) {
  if (!pending.impl || !pending.impl->readback) {
    throw std::runtime_error("Invalid screenshot pending");
  }
  const void* mapped = pending.impl->readback->mapped();
  if (!mapped) {
    throw std::runtime_error("Screenshot readback buffer is not CPU-mapped");
  }

  // The readback buffer is row-pitch aligned to 256B; callers want it tightly packed.
  std::vector<uint8_t> rgba(static_cast<size_t>(pending.width) * pending.height * 4);
  const auto* src = static_cast<const uint8_t*>(mapped);
  for (uint32_t y = 0; y < pending.height; ++y) {
    std::memcpy(rgba.data() + static_cast<size_t>(y) * pending.width * 4,
                src + static_cast<size_t>(y) * pending.rowPitch, static_cast<size_t>(pending.width) * 4);
  }
  return rgba;
}

void finalizeScreenshot(const ScreenshotPending& pending, const std::string& path) {
  const std::vector<uint8_t> rgba = readScreenshotPixels(pending);
  if (!stbi_write_png(path.c_str(), static_cast<int>(pending.width), static_cast<int>(pending.height), 4,
                      rgba.data(), static_cast<int>(pending.width * 4))) {
    throw std::runtime_error("stbi_write_png failed: " + path);
  }
}

} // namespace tucano
