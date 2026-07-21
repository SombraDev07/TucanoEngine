#include "RHI/DX12/DX12SwapChain.h"
#include "RHI/DX12/DX12Device.h"

namespace tucano::rhi {

DX12SwapChain::DX12SwapChain(DX12Device* device, void* hwnd, uint32_t width, uint32_t height, bool vsync)
    : m_device(device), m_width(width), m_height(height), m_vsync(vsync), m_hwnd(static_cast<HWND>(hwnd)) {
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = kBackBufferCount;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  ComPtr<IDXGISwapChain1> sc1;
  throwIfFailed(device->factory()->CreateSwapChainForHwnd(device->queue(), m_hwnd, &desc, nullptr, nullptr, &sc1),
                "CreateSwapChainForHwnd");
  throwIfFailed(device->factory()->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
  throwIfFailed(sc1.As(&m_swapChain), "Query IDXGISwapChain3");
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
  createBuffers();
}

void DX12SwapChain::createBuffers() {
  for (uint32_t i = 0; i < kBackBufferCount; ++i) {
    auto tex = std::make_shared<DX12Texture>();
    throwIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&tex->resource)), "GetBuffer");
    tex->desc.width = m_width;
    tex->desc.height = m_height;
    tex->desc.format = Format::R8G8B8A8_UNORM;
    tex->usage = TextureUsage::RenderTarget;
    tex->state = ResourceState::Present;
    m_device->createRtv(*tex);
    m_backBuffers[i] = std::move(tex);
  }
}

void DX12SwapChain::resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || (width == m_width && height == m_height)) {
    return;
  }
  m_device->waitIdle();
  for (auto& bb : m_backBuffers) {
    bb.reset();
  }
  m_width = width;
  m_height = height;
  throwIfFailed(m_swapChain->ResizeBuffers(kBackBufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                           DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),
                "ResizeBuffers");
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
  createBuffers();
}

Texture& DX12SwapChain::backBuffer() {
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
  return *m_backBuffers[m_frameIndex];
}

void DX12SwapChain::present() {
  throwIfFailed(m_swapChain->Present(m_vsync ? 1 : 0, 0), "Present");
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

} // namespace tucano::rhi
