#include "AssetPipeline/GLTFLoader.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <iostream>

using namespace tucano;

int main(int argc, char** argv) {
  std::string scenePath = "Assets/Sponza/Sponza.gltf";
  std::string screenshotPath;
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--scene" && i + 1 < argc) {
      scenePath = argv[++i];
    } else if (a == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    }
  }

  try {
    Window window({1920, 1080, "Tucano — Sponza Viewer"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    Renderer renderer(*device, window.width(), window.height());
    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    Scene scene;
    if (!loadGLTFScene(*device, scenePath, scene)) {
      std::cerr << "Failed to load scene: " << scenePath << "\n";
      std::cerr << "Place Khronos Sponza glTF under Assets/Sponza/\n";
    }
    scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 300.0f);
    scene.camera.setPosition({0.0f, 2.0f, 0.0f});
    scene.camera.lookAt({1.0f, 2.0f, 0.0f});
    scene.addDirectional(glm::normalize(glm::vec3(-0.45f, -1.0f, 0.15f)), {1.0f, 0.96f, 0.9f}, 8.0f);
    scene.addPoint({0.0f, 2.5f, 0.0f}, {1.0f, 0.85f, 0.6f}, 20.0f, 12.0f);
    scene.addPoint({-4.0f, 1.5f, -1.0f}, {0.4f, 0.6f, 1.0f}, 12.0f, 8.0f);
    scene.addPoint({4.0f, 1.5f, 1.0f}, {1.0f, 0.5f, 0.3f}, 12.0f, 8.0f);
    scene.addPoint({0.0f, 4.0f, 3.0f}, {0.8f, 0.9f, 1.0f}, 10.0f, 10.0f);
    scene.addSpot({-2.0f, 5.0f, 0.0f}, glm::normalize(glm::vec3(0.35f, -1.0f, 0.1f)), {1.0f, 0.92f, 0.75f}, 40.0f,
                  18.0f, 15.0f, 35.0f);

    // Start with a rich storm so Cry-quality rain is visible immediately.
    renderer.rain().enabled = true;
    renderer.rain().amount = 0.95f;
    renderer.rain().streakIntensity = 1.15f;
    renderer.rain().streakSpeed = 1.85f;
    renderer.rain().streakLayers = 3.0f;
    renderer.rain().rainDropsAmount = 0.55f;
    renderer.rain().rainDropsLighting = 1.2f;
    renderer.rain().puddlesAmount = 1.45f;
    renderer.rain().splashesAmount = 0.9f;
    renderer.rain().diffuseDarkening = 0.75f;
    renderer.rain().glossBoost = 1.2f;
    renderer.rain().mistAmount = 0.35f;
    renderer.rain().wind = {0.2f, 0.0f, 0.05f};

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      swapChain->resize(w, h);
      renderer.resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 300.0f);
    });

    int frame = 0;
    bool shotDone = screenshotPath.empty();
    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();
      ui.beginFrame();
      ui.drawPerfHud(renderer.lastFrameMs(), renderer.drawCalls(), window.width(), window.height());
      ui.drawWeatherAndLights(renderer.rain(), scene, renderer.settings());

      if (!ui.wantCaptureKeyboard()) {
        if (input.keyPressed(GLFW_KEY_F12) && screenshotPath.empty()) {
          screenshotPath = "sponza_capture.png";
          shotDone = false;
        }
        if (input.keyPressed(GLFW_KEY_1)) {
          renderer.settings().enableShadows = !renderer.settings().enableShadows;
        }
        if (input.keyPressed(GLFW_KEY_2)) {
          renderer.settings().enableIBL = !renderer.settings().enableIBL;
        }
        if (input.keyPressed(GLFW_KEY_3)) {
          renderer.settings().enableBloom = !renderer.settings().enableBloom;
        }
        if (input.keyPressed(GLFW_KEY_4)) {
          renderer.settings().enableAO = !renderer.settings().enableAO;
        }
        if (input.keyPressed(GLFW_KEY_5)) {
          auto& t = renderer.settings().giTier;
          t = static_cast<GITier>((static_cast<uint32_t>(t) + 1) % 4);
        }
        if (input.keyPressed(GLFW_KEY_6)) {
          renderer.settings().enableVisibilityBuffer = !renderer.settings().enableVisibilityBuffer;
        }
        if (input.keyPressed(GLFW_KEY_7)) {
          renderer.settings().enableMeshlets = !renderer.settings().enableMeshlets;
        }
        if (input.keyPressed(GLFW_KEY_8)) {
          renderer.settings().enableSSR = !renderer.settings().enableSSR;
        }
        if (input.keyPressed(GLFW_KEY_9)) {
          renderer.rain().enabled = !renderer.rain().enabled;
        }
      }

      float dx = 0, dy = 0;
      input.mouseDelta(dx, dy);
      glm::vec3 move(0);
      if (!ui.wantCaptureKeyboard()) {
        if (input.keyDown(GLFW_KEY_W)) {
          move.z += 1;
        }
        if (input.keyDown(GLFW_KEY_S)) {
          move.z -= 1;
        }
        if (input.keyDown(GLFW_KEY_A)) {
          move.x -= 1;
        }
        if (input.keyDown(GLFW_KEY_D)) {
          move.x += 1;
        }
        if (input.keyDown(GLFW_KEY_E)) {
          move.y += 1;
        }
        if (input.keyDown(GLFW_KEY_Q)) {
          move.y -= 1;
        }
      }
      const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 12.0f : 4.0f;
      const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
      if (look) {
        scene.camera.fly(1.0f / 60.0f, move * speed, dx * 0.0025f, -dy * 0.0025f);
      } else {
        scene.camera.fly(1.0f / 60.0f, move * speed, 0, 0);
      }

      if (frame % 60 == 0) {
        const float fps = 1000.0f / std::max(1.0f, renderer.lastFrameMs());
        window.setTitle("Tucano Sponza | " + std::to_string(int(fps)) + " FPS | rain " +
                        (renderer.rain().enabled ? "on" : "off") + " | Tools panel | 9 rain | F12");
      }
      input.endFrame();

      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer.render(*cmd, bb, scene);
      ui.endFrame(*cmd, bb);

      ScreenshotPending shot;
      if (!shotDone && frame >= 5) {
        shot = beginScreenshot(*device, *cmd, bb);
      }
      cmd->transition(bb, rhi::ResourceState::Present);
      device->endFrame(*swapChain);
      if (shot.impl) {
        device->waitIdle();
        finalizeScreenshot(shot, screenshotPath);
        shotDone = true;
        std::cout << "Saved " << screenshotPath << "\n";
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
    ui.shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
