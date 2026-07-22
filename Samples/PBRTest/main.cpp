#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Runtime/Screenshot.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <vector>

using namespace tucano;

static std::shared_ptr<Mesh> makeSphere(rhi::Device& device, float radius, int segments) {
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  for (int y = 0; y <= segments; ++y) {
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / segments;
      const float v = float(y) / segments;
      const float theta = u * 6.2831853f;
      const float phi = v * 3.14159265f;
      Vertex vert{};
      vert.position = {radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi),
                       radius * std::sin(phi) * std::sin(theta)};
      vert.normal = glm::normalize(vert.position);
      vert.tangent = {1, 0, 0, 1};
      vert.uv = {u, v};
      vert.color = {1, 1, 1, 1};
      verts.push_back(vert);
    }
  }
  for (int y = 0; y < segments; ++y) {
    for (int x = 0; x < segments; ++x) {
      const uint32_t i0 = y * (segments + 1) + x;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + (segments + 1);
      const uint32_t i3 = i2 + 1;
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }
  SubMesh sub{};
  sub.indexCount = static_cast<uint32_t>(indices.size());
  return Mesh::create(device, verts, indices, {sub});
}

int main(int argc, char** argv) {
  std::string screenshotPath;
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (std::string(argv[i]) == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    }
  }

  try {
    std::cout << "PBRTest starting...\n" << std::flush;
    Window window({1600, 900, "Tucano — PBR Test"});
    std::cout << "Window OK\n" << std::flush;
    auto device = rhi::Device::create(true);
    std::cout << "Device OK\n" << std::flush;
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    std::cout << "SwapChain OK\n" << std::flush;
    Renderer renderer(*device, window.width(), window.height());
    std::cout << "Renderer OK\n" << std::flush;
    Input input(window.handle());

    Scene scene;
    scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    scene.camera.setPosition({0, 1.8f, 7.5f});
    scene.camera.lookAt({0, 1.5f, 0});
    scene.addDirectional(glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f)), {1, 0.97f, 0.92f}, 5.0f);
    scene.addPoint({-1.5f, 1.5f, 1.0f}, {1.0f, 0.6f, 0.3f}, 8.0f, 8.0f);
    scene.addPoint({1.5f, 1.2f, 0.5f}, {0.3f, 0.5f, 1.0f}, 6.0f, 8.0f);
    scene.addSpot({0.0f, 3.5f, 2.5f}, glm::normalize(glm::vec3(0.0f, -1.0f, -0.35f)), {1.0f, 0.95f, 0.85f}, 25.0f,
                  14.0f, 12.0f, 28.0f);

    auto sphere = makeSphere(*device, 0.45f, 24);
    for (int metal = 0; metal < 5; ++metal) {
      for (int rough = 0; rough < 5; ++rough) {
        auto mat = std::make_shared<Material>();
        mat->baseColorFactor = {0.9f, 0.7f, 0.5f, 1.0f};
        mat->metallicFactor = metal / 4.0f;
        mat->roughnessFactor = std::max(0.05f, rough / 4.0f);
        RenderObject obj;
        obj.mesh = sphere;
        obj.materials = {mat};
        obj.worldMatrix = glm::translate(glm::mat4(1.0f), {(metal - 2) * 1.1f, rough * 1.1f, 0.0f});
        scene.objects.push_back(std::move(obj));
      }
    }
    // Clearcoat dielectric row
    for (int i = 0; i < 5; ++i) {
      auto mat = std::make_shared<Material>();
      mat->baseColorFactor = {0.15f, 0.18f, 0.22f, 1.0f};
      mat->metallicFactor = 0.0f;
      mat->roughnessFactor = 0.35f;
      mat->reflectance = 0.5f;
      mat->clearcoat = 0.25f + i * 0.18f;
      mat->clearcoatRoughness = 0.08f;
      RenderObject obj;
      obj.mesh = sphere;
      obj.materials = {mat};
      obj.worldMatrix = glm::translate(glm::mat4(1.0f), {(i - 2) * 1.1f, -1.2f, 0.0f});
      scene.objects.push_back(std::move(obj));
    }

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      swapChain->resize(w, h);
      renderer.resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    });

    int frame = 0;
    bool shotDone = screenshotPath.empty();
    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();
      float dx = 0, dy = 0;
      input.mouseDelta(dx, dy);
      glm::vec3 move(0);
      if (input.keyDown(GLFW_KEY_W)) move.z += 1;
      if (input.keyDown(GLFW_KEY_S)) move.z -= 1;
      if (input.keyDown(GLFW_KEY_A)) move.x -= 1;
      if (input.keyDown(GLFW_KEY_D)) move.x += 1;
      if (input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        scene.camera.fly(1.0f / 60.0f, move * 3.0f, dx * 0.002f, -dy * 0.002f);
      } else {
        scene.camera.fly(1.0f / 60.0f, move * 3.0f, 0, 0);
      }
      input.endFrame();

      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer.render(cmd, bb, scene);
      if (frame == 0) {
        std::cout << "drawCalls=" << renderer.drawCalls() << " frameMs=" << renderer.lastFrameMs() << "\n"
                  << std::flush;
      }

      ScreenshotPending shot;
      if (!shotDone && frame >= 3) {
        shot = beginScreenshot(*device, *cmd, bb);
      }
      cmd->transition(bb, rhi::ResourceState::Present);
      device->endFrame(*swapChain);
      if (shot.impl) {
        device->waitIdle();
        finalizeScreenshot(shot, screenshotPath);
        shotDone = true;
        if (maxFrames < 0) maxFrames = frame + 1;
        std::cout << "Saved " << screenshotPath << "\n";
      }
      ++frame;
      if (maxFrames >= 0 && frame >= maxFrames) break;
    }
    device->waitIdle();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
