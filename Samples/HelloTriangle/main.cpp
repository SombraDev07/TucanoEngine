#include "Platform/FileSystem.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Runtime/Screenshot.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace tucano;
using namespace tucano::rhi;

struct Vertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float uv[2];
  float color[4];
};

int main(int argc, char** argv) {
  std::string screenshotPath;
  int maxFrames = -1;
  bool vsync = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (arg == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    } else if (arg == "--no-vsync") {
      vsync = false;
    }
  }

  try {
    Window window({1280, 720, "Tucano — Hello Triangle"});
    auto device = Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), vsync);

    window.setResizeCallback([&](uint32_t w, uint32_t h) { swapChain->resize(w, h); });

    const Vertex vertices[] = {
        {{0.0f, 0.55f, 0.0f}, {0, 0, 1}, {1, 0, 0, 1}, {0.5f, 0.0f}, {1.0f, 0.2f, 0.2f, 1.0f}},
        {{0.55f, -0.55f, 0.0f}, {0, 0, 1}, {1, 0, 0, 1}, {1.0f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}},
        {{-0.55f, -0.55f, 0.0f}, {0, 0, 1}, {1, 0, 0, 1}, {0.0f, 1.0f}, {0.2f, 0.4f, 1.0f, 1.0f}},
    };
    const uint32_t indices[] = {0, 1, 2};

    BufferDesc vbDesc{};
    vbDesc.size = sizeof(vertices);
    vbDesc.usage = BufferUsage::Vertex;
    vbDesc.debugName = "TriangleVB";
    auto vb = device->createBuffer(vbDesc, vertices);

    BufferDesc ibDesc{};
    ibDesc.size = sizeof(indices);
    ibDesc.usage = BufferUsage::Index;
    ibDesc.debugName = "TriangleIB";
    auto ib = device->createBuffer(ibDesc, indices);

    auto root = device->createRootSignature(true);
    const std::string shaderDir = std::string(TUCANO_SHADER_DIR);
    GraphicsPipelineDesc psoDesc{};
    psoDesc.rootSignature = root;
    psoDesc.vs = ShaderBytecode::loadFromFile(joinPath(shaderDir, "Triangle_VSMain.cso"));
    psoDesc.ps = ShaderBytecode::loadFromFile(joinPath(shaderDir, "Triangle_PSMain.cso"));
    psoDesc.rtvFormats = {Format::R8G8B8A8_UNORM};
    psoDesc.depthEnable = false;
    psoDesc.cullMode = CullMode::None;
    auto pso = device->createGraphicsPipeline(psoDesc);

    int frame = 0;
    bool screenshotDone = screenshotPath.empty();
    while (!window.shouldClose()) {
      window.pollEvents();
      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();

      cmd->transition(bb, ResourceState::RenderTarget);
      const float clear[4] = {0.08f, 0.09f, 0.12f, 1.0f};
      Texture* rt = &bb;
      cmd->setRenderTargets(std::span<Texture*>(&rt, 1), nullptr);
      cmd->clearRenderTarget(bb, clear);

      Viewport vp{0, 0, static_cast<float>(swapChain->width()), static_cast<float>(swapChain->height()), 0, 1};
      cmd->setViewport(vp);
      cmd->setScissor({0, 0, static_cast<int>(swapChain->width()), static_cast<int>(swapChain->height())});
      cmd->setDescriptorHeap();
      cmd->setRootSignature(*root);
      cmd->setPipeline(*pso);
      const float tint[4] = {1, 1, 1, 1};
      cmd->setGraphicsRootConstants(0, tint, 4);
      cmd->setPrimitiveTopology(PrimitiveTopology::TriangleList);
      cmd->setVertexBuffer(*vb, sizeof(Vertex));
      cmd->setIndexBuffer(*ib, true);
      cmd->drawIndexed(3);

      ScreenshotPending shot;
      if (!screenshotDone && frame >= 2) {
        shot = beginScreenshot(*device, *cmd, bb);
      }

      cmd->transition(bb, ResourceState::Present);
      device->endFrame(*swapChain);

      if (shot.impl) {
        device->waitIdle();
        finalizeScreenshot(shot, screenshotPath);
        screenshotDone = true;
        std::cout << "Saved screenshot: " << screenshotPath << "\n";
        if (maxFrames < 0) {
          maxFrames = frame + 1;
        }
      }

      ++frame;
      if (maxFrames >= 0 && frame >= maxFrames) {
        break;
      }
    }

    device->waitIdle();
    std::cout << "HelloTriangle OK (" << frame << " frames)\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
