#include "Common/WaterScene.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

using namespace tucano;

int main(int argc, char** argv) {
  std::string screenshotPath;
  int maxFrames = -1;
  bool noClouds = false;
  bool noGodRays = false;
  bool withRain = false;
  bool noFoam = false;
  bool noUI = false;
  bool noSSR = false;
  float rainAmount = -1.0f;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    } else if (a == "--noclouds") {
      noClouds = true;
    } else if (a == "--nogodrays") {
      noGodRays = true;
    } else if (a == "--rain") {
      withRain = true;
    } else if (a == "--nofoam") {
      noFoam = true;
    } else if (a == "--noui") {
      noUI = true; // clean frames for before/after comparison shots
    } else if (a == "--nossr") {
      noSSR = true;
    } else if (a == "--rainamount" && i + 1 < argc) {
      rainAmount = std::stof(argv[++i]);
    }
  }

  try {
    Window window({1920, 1080, "Tucano — WaterLab (ocean + sky + clouds + rain)"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
    waterlab::configureWaterRenderer(*renderer);

    if (noClouds) {
      renderer->settings().enableClouds = false;
    }
    if (noGodRays) {
      renderer->settings().enableCloudGodRays = false;
    }
    if (withRain) {
      renderer->rain().enabled = true;
    }
    if (rainAmount >= 0.0f) {
      renderer->rain().enabled = rainAmount > 0.0f;
      renderer->rain().amount = rainAmount;
    }

    auto& waterParams = renderer->water();
    waterParams.enabled = true;
    waterParams.waterLevel = 0.0f;
    waterParams.waveAmplitude = 0.8f;
    waterParams.waveFrequency = 0.12f;
    waterParams.waveSpeed = 0.6f;
    waterParams.waveChoppy = 0.5f;
    waterParams.foamAmount = 0.5f;
    waterParams.sssIntensity = 0.35f;
    if (noFoam) {
      waterParams.foamAmount = 0.0f;
    }
    if (noSSR) {
      waterParams.enableSSR = false;
    }

    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    Scene scene;
    waterlab::buildWaterScene(*device, scene);
    scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);

    device->setDeviceLostCallback([&]() {
      const Camera cam = scene.camera;
      const RendererSettings settings = renderer->settings();
      const WaterParams wp = renderer->water();
      const RainParams rp = renderer->rain();
      ui.shutdown();
      renderer.reset();
      swapChain.reset();
      swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
      renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
      renderer->settings() = settings;
      renderer->water() = wp;
      renderer->rain() = rp;
      waterlab::buildWaterScene(*device, scene);
      scene.camera = cam;
      ui.init(window, *device);
    });

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      if (!swapChain || !renderer) return;
      swapChain->resize(w, h);
      renderer->resize(w, h);
      scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);
    });

    int frame = 0;
    bool shotDone = screenshotPath.empty();
    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();
      ui.beginFrame();
      if (!noUI) {
        ui.drawPerfHud(renderer->lastFrameMs(), renderer->drawCalls(), window.width(), window.height());
        ui.drawWeatherAndLights(renderer->rain(), scene, renderer->settings());
      }

      if (!ui.wantCaptureKeyboard() && input.keyPressed(GLFW_KEY_F12) && screenshotPath.empty()) {
        screenshotPath = "waterlab_capture.png";
        shotDone = false;
      }

      float dx = 0, dy = 0;
      input.mouseDelta(dx, dy);
      glm::vec3 move(0);
      if (!ui.wantCaptureKeyboard()) {
        if (input.keyDown(GLFW_KEY_W)) move.z += 1;
        if (input.keyDown(GLFW_KEY_S)) move.z -= 1;
        if (input.keyDown(GLFW_KEY_A)) move.x -= 1;
        if (input.keyDown(GLFW_KEY_D)) move.x += 1;
        if (input.keyDown(GLFW_KEY_E)) move.y += 1;
        if (input.keyDown(GLFW_KEY_Q)) move.y -= 1;
      }
      const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 40.0f : 12.0f;
      const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
      scene.camera.fly(1.0f / 60.0f, move * speed, look ? dx * 0.0025f : 0.0f, look ? -dy * 0.0025f : 0.0f);

      if (frame % 60 == 0) {
        float fps = 1000.0f / std::max(1.0f, renderer->lastFrameMs());
        window.setTitle("Tucano WaterLab | " + std::to_string(int(fps)) +
                        " FPS | ocean+sky+clouds | WASD+RMB | F12");
      }
      input.endFrame();

      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer->render(cmd, bb, scene);
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
        if (maxFrames < 0) maxFrames = frame + 1;
      }
      ++frame;
      if (maxFrames >= 0 && frame >= maxFrames) break;
    }

    device->waitIdle();
    ui.shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
