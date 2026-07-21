#pragma once

#include "RHI/DX12/DX12Resource.h"

#include <array>
#include <memory>

namespace tucano::rhi {

class DX12Device;

class DX12SwapChain final : public SwapChain {
public:
  DX12SwapChain(DX12Device* device, void* hwnd, uint32_t width, uint32_t height, bool vsync);

  void resize(uint32_t width, uint32_t height) override;
  Texture& backBuffer() override;
  uint32_t width() const override { return m_width; }
  uint32_t height() const override { return m_height; }
  void present() override;
  uint32_t currentIndex() const { return m_frameIndex; }

private:
  void createBuffers();

  DX12Device* m_device = nullptr;
  ComPtr<IDXGISwapChain3> m_swapChain;
  std::array<std::shared_ptr<DX12Texture>, kBackBufferCount> m_backBuffers;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  uint32_t m_frameIndex = 0;
  bool m_vsync = true;
  HWND m_hwnd = nullptr;
};

} // namespace tucano::rhi
