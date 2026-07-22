#include "Common/SkyScene.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <string>

using namespace tucano;

namespace {

struct CamBookmark {
  const char* name;
  glm::vec3 pos;
  glm::vec3 look;
};

const CamBookmark kBookmarks[] = {
    {"Horizon wide", {0.0f, 6.0f, 28.0f}, {0.0f, 8.0f, 0.0f}},
    {"Low ground", {0.0f, 1.6f, 12.0f}, {0.0f, 4.0f, -20.0f}},
    {"Up into clouds", {0.0f, 40.0f, 10.0f}, {0.0f, 80.0f, -40.0f}},
    {"Golden look", {-20.0f, 5.0f, 8.0f}, {10.0f, 12.0f, -30.0f}},
};

void drawCameraEditor(Scene& scene, float aspect) {
  ImGui::SetNextWindowSize(ImVec2(340, 280), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Camera Editor")) {
    ImGui::End();
    return;
  }
  glm::vec3 pos = scene.camera.position();
  if (ImGui::DragFloat3("Position", &pos.x, 0.1f)) {
    scene.camera.setPosition(pos);
  }
  static float fovDeg = 65.0f;
  static float nearP = 0.2f;
  static float farP = 4000.0f;
  bool projDirty = false;
  projDirty |= ImGui::SliderFloat("FOV", &fovDeg, 30.0f, 110.0f);
  projDirty |= ImGui::DragFloat("Near", &nearP, 0.05f, 0.05f, 10.0f);
  projDirty |= ImGui::DragFloat("Far", &farP, 10.0f, 100.0f, 8000.0f);
  if (projDirty) {
    scene.camera.setPerspective(glm::radians(fovDeg), aspect, nearP, farP);
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Bookmarks");
  for (const auto& b : kBookmarks) {
    if (ImGui::Button(b.name)) {
      scene.camera.setPosition(b.pos);
      scene.camera.lookAt(b.look);
    }
  }
  ImGui::Separator();
  if (ImGui::Button("Reset default view")) {
    scene.camera.setPosition({0.0f, 4.0f, 18.0f});
    scene.camera.lookAt({0.0f, 3.0f, 0.0f});
    fovDeg = 65.0f;
    nearP = 0.2f;
    farP = 4000.0f;
    scene.camera.setPerspective(glm::radians(fovDeg), aspect, nearP, farP);
  }
  ImGui::End();
}

void drawSkyQuickBar(RendererSettings& settings) {
  ImGui::SetNextWindowPos(ImVec2(12, 90), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.7f);
  if (ImGui::Begin("Sky Quick", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Checkbox("Atmosphere", &settings.enableAtmosphere);
    ImGui::Checkbox("Clouds", &settings.enableClouds);
    ImGui::Checkbox("Cloud shadows", &settings.enableCloudShadows);
    ImGui::Checkbox("God rays", &settings.enableCloudGodRays);
    ImGui::SliderFloat("Time of day", &settings.timeOfDay, 0.0f, 1.0f);
    ImGui::SliderFloat("Coverage", &settings.cloudCoverage, 0.0f, 1.0f);
    ImGui::SliderFloat("Storminess", &settings.cloudStorminess, 0.0f, 1.0f);
    if (ImGui::Button("Noon clear")) {
      settings.timeOfDay = 0.5f;
      settings.cloudCoverage = 0.2f;
      settings.cloudStorminess = 0.05f;
      settings.turbidity = 2.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Storm")) {
      settings.timeOfDay = 0.4f;
      settings.cloudCoverage = 0.92f;
      settings.cloudStorminess = 0.9f;
      settings.turbidity = 6.0f;
      settings.fogDensity = 0.03f;
    }
  }
  ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
  std::string screenshotPath;
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    }
  }

  try {
    Window window({1920, 1080, "Tucano — Test Editor (sky / clouds / camera)"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
    skylab::configureCleanRenderer(*renderer);

    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    Scene scene;
    skylab::buildCleanScene(*device, scene);
    scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);

    device->setDeviceLostCallback([&]() {
      const Camera cam = scene.camera;
      const RendererSettings settings = renderer->settings();
      ui.shutdown();
      renderer.reset();
      swapChain.reset();
      swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
      renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
      renderer->settings() = settings;
      skylab::buildCleanScene(*device, scene);
      scene.camera = cam;
      ui.init(window, *device);
    });

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      if (!swapChain || !renderer) {
        return;
      }
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
      ui.drawPerfHud(renderer->lastFrameMs(), renderer->drawCalls(), window.width(), window.height());
      ui.drawWeatherAndLights(renderer->rain(), scene, renderer->settings());
      drawSkyQuickBar(renderer->settings());
      drawCameraEditor(scene, window.aspect());

      if (!ui.wantCaptureKeyboard() && input.keyPressed(GLFW_KEY_F12) && screenshotPath.empty()) {
        screenshotPath = "test_editor_capture.png";
        shotDone = false;
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
      const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 40.0f : 12.0f;
      const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
      scene.camera.fly(1.0f / 60.0f, move * speed, look ? dx * 0.0025f : 0.0f, look ? -dy * 0.0025f : 0.0f);

      if (frame % 60 == 0) {
        const float fps = 1000.0f / std::max(1.0f, renderer->lastFrameMs());
        window.setTitle("Tucano TestEditor | " + std::to_string(int(fps)) +
                        " FPS | sky editor | WASD+RMB | F12");
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
