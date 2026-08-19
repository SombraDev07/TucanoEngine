#include "Platform/FileSystem.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Runtime/Screenshot.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#ifndef TUCANO_SHADER_EXT
#define TUCANO_SHADER_EXT ".cso"
#endif

using namespace tucano;
using namespace tucano::rhi;

struct Vertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float uv[2];
  float color[4];
  uint32_t boneIndices = 0;
  float boneWeights[4] = {0, 0, 0, 0};
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

    TextureDesc texDesc{};
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = Format::R8G8B8A8_UNORM;
    texDesc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
    texDesc.debugName = "BindlessChecker";
    auto checker = device->createTexture(texDesc);

    SamplerDesc sampDesc{};
    sampDesc.filter = Filter::Linear;
    auto sampler = device->createSampler(sampDesc);

    auto root = device->createRootSignature(true);
    auto computeRoot = device->createComputeRootSignature();
    const std::string shaderDir = std::string(TUCANO_SHADER_DIR);
    GraphicsPipelineDesc psoDesc{};
    psoDesc.rootSignature = root;
    psoDesc.vs = ShaderBytecode::loadFromFile(joinPath(shaderDir, std::string("Triangle_VSMain") + TUCANO_SHADER_EXT));
    psoDesc.ps = ShaderBytecode::loadFromFile(joinPath(shaderDir, std::string("Triangle_PSMain") + TUCANO_SHADER_EXT));
    psoDesc.rtvFormats = {Format::R8G8B8A8_UNORM};
    psoDesc.depthEnable = false;
    psoDesc.cullMode = CullMode::None;
    auto pso = device->createGraphicsPipeline(psoDesc);

    ComputePipelineDesc csDesc{};
    csDesc.rootSignature = computeRoot;
    csDesc.cs = ShaderBytecode::loadFromFile(joinPath(shaderDir, std::string("BindlessFill_CSMain") + TUCANO_SHADER_EXT));
    auto csPso = device->createComputePipeline(csDesc);

    bool filled = false;

    int frame = 0;
    bool screenshotDone = screenshotPath.empty();
    while (!window.shouldClose()) {
      window.pollEvents();
      auto* cmd = device->beginFrame();
      if (!filled) {
        cmd->transition(*checker, ResourceState::UnorderedAccess);
        cmd->setRootSignature(*computeRoot);
        cmd->setPipeline(*csPso);
        Texture* uav = checker.get();
        const uint32_t uavTable = device->writeTextureUavTable({&uav, 1});
        cmd->setComputeRootUavTable(3, uavTable);
        cmd->dispatch(8, 8, 1);
        cmd->uavBarrier(checker.get());
        cmd->transition(*checker, ResourceState::ShaderResource);
        filled = true;
      }
      auto& bb = swapChain->backBuffer();

      cmd->transition(bb, ResourceState::RenderTarget);
      const float clear[4] = {0.08f, 0.09f, 0.12f, 1.0f};
      Texture* rt = &bb;
      cmd->setRenderTargets(std::span<Texture*>(&rt, 1), nullptr);
      cmd->clearRenderTarget(bb, clear);

      Viewport vp{0, 0, static_cast<float>(swapChain->width()), static_cast<float>(swapChain->height()), 0, 1};
      cmd->setViewport(vp);
      cmd->setScissor({0, 0, static_cast<int>(swapChain->width()), static_cast<int>(swapChain->height())});
      cmd->setRootSignature(*root);
      cmd->setDescriptorHeap();
      cmd->setPipeline(*pso);
      cmd->setGraphicsRootSrvTable(3, 0);
      Sampler* sampPtr = sampler.get();
      const uint32_t sampTable = device->writeSamplerTable({&sampPtr, 1});
      cmd->setGraphicsRootSamplerTable(4, sampTable);
      struct Push {
        float tint[4] = {1, 1, 1, 1};
        uint32_t texIndex = 0;
      } push;
      push.texIndex = checker->bindlessIndex();
      cmd->setGraphicsRootConstants(0, &push, 5);
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
