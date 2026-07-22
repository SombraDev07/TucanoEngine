#include "AssetPipeline/GLTFLoader.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

using namespace tucano;

int main(int argc, char** argv) {
  std::string scenePath = "Assets/Sponza/Sponza.gltf";
  int frames = 200;
  uint32_t width = 1920;
  uint32_t height = 1080;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--scene" && i + 1 < argc) scenePath = argv[++i];
    else if (a == "--frames" && i + 1 < argc) frames = std::stoi(argv[++i]);
    else if (a == "--resolution" && i + 1 < argc) {
      sscanf(argv[++i], "%ux%u", &width, &height);
    }
  }

  try {
    Window window({width, height, "TucanoBenchmark"});
    auto device = rhi::Device::create(false);
    auto swapChain = device->createSwapChain(window.nativeHandle(), width, height, false);
    Renderer renderer(*device, width, height);

    Scene scene;
    loadGLTFScene(*device, scenePath, scene);
    scene.camera.setPerspective(glm::radians(60.0f), float(width) / float(height), 0.1f, 300.0f);
    scene.camera.setPosition({8.0f, 2.0f, 0.0f});
    scene.camera.lookAt({0, 2, 0});
    scene.addDirectional(glm::normalize(glm::vec3(-0.45f, -1.0f, 0.15f)), {1, 0.96f, 0.9f}, 8.0f);
    scene.addPoint({0, 2.5f, 0}, {1, 0.85f, 0.6f}, 20.0f, 12.0f);

    std::vector<float> times;
    times.reserve(frames);
    for (int i = 0; i < frames; ++i) {
      window.pollEvents();
      const float t = i / float(frames);
      scene.camera.setPosition({std::cos(t * 6.28f) * 8.0f, 2.0f + std::sin(t * 3.14f) * 0.5f,
                                std::sin(t * 6.28f) * 8.0f});
      scene.camera.lookAt({0, 2, 0});

      const auto t0 = std::chrono::steady_clock::now();
      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer.render(cmd, bb, scene);
      cmd->transition(bb, rhi::ResourceState::Present);
      device->endFrame(*swapChain);
      const auto t1 = std::chrono::steady_clock::now();
      times.push_back(std::chrono::duration<float, std::milli>(t1 - t0).count());
    }
    device->waitIdle();

    std::sort(times.begin(), times.end());
    const float avg = std::accumulate(times.begin(), times.end(), 0.0f) / times.size();
    const float minMs = times.front();
    const float p99 = times[size_t(times.size() * 0.99)];
    std::cout << "TucanoBenchmark scene=" << scenePath << " frames=" << frames << "\n";
    std::cout << "avg_ms=" << avg << " min_ms=" << minMs << " p99_ms=" << p99
              << " avg_fps=" << (1000.0f / avg) << "\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
