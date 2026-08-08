#pragma once

#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tucano {

struct ScreenshotPending {
  struct Impl;
  std::shared_ptr<Impl> impl;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowPitch = 0;
};

// Record GPU copy from rendered backbuffer (call before Present transition).
ScreenshotPending beginScreenshot(rhi::Device& device, rhi::CommandList& cmd, rhi::Texture& backbuffer);

// After device.waitIdle(), write PNG.
void finalizeScreenshot(const ScreenshotPending& pending, const std::string& path);

// After device.waitIdle(), read the frame back as tightly-packed RGBA8 (width*height*4).
// Lets a gate assert on pixels directly instead of round-tripping through a PNG and an
// external image library.
std::vector<uint8_t> readScreenshotPixels(const ScreenshotPending& pending);

} // namespace tucano
