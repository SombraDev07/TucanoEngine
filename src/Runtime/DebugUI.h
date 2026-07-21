#pragma once

#include "Renderer/Weather/RainParams.h"
#include "Renderer/Scene.h"
#include "Renderer/Renderer.h"
#include "RHI/RHI.h"
#include "Platform/Window.h"

#include <memory>

struct GLFWwindow;

namespace tucano {

class DebugUI {
public:
  void init(Window& window, rhi::Device& device);
  void shutdown();
  void beginFrame();
  void endFrame(rhi::CommandList& cmd, rhi::Texture& renderTarget);

  void drawWeatherAndLights(RainParams& rain, Scene& scene, RendererSettings& settings);
  void drawPerfHud(float frameMs, uint32_t drawCalls, uint32_t width, uint32_t height);
  bool wantCaptureMouse() const;
  bool wantCaptureKeyboard() const;

private:
  bool m_ready = false;
  void* m_srvHeap = nullptr; // ID3D12DescriptorHeap*
  uint64_t m_fence = 0;
};

} // namespace tucano
