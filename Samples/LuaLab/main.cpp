#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Lua/LuaVM.h"
#include "ECS/World.h"
#include "Vegetation/VegetationSystem.h"
#include "Vegetation/VegetationRenderer.h"
#include "Vegetation/VegetationDispatch.h"
#include "Vegetation/WindSystem.h"
#include "Vegetation/SeasonSystem.h"
#include "Vegetation/GrowthSystem.h"
#include "Vegetation/VegetationInteraction.h"
#include "Vegetation/VegetationEditor.h"
#include "Vegetation/LODManager.h"
#include "RHI/DX12/DX12Device.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>

using namespace tucano;

int main() {
  try {
    Window window({1920, 1080, "Tucano — LuaLab Veg-1"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    Scene scene;
    scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 500.0f);
    scene.camera.setPosition({0, 10, 20});
    scene.camera.lookAt({0, 0, 0});
    scene.addDirectional(glm::normalize(glm::vec3(-0.45f, -1.0f, 0.15f)), {1.0f, 0.96f, 0.9f}, 8.0f);

    renderer->settings().enableVSM = false;
    renderer->settings().enableAsyncCompute = false;
    renderer->settings().enableOctahedralPointShadows = false;
    renderer->settings().enableMeshlets = false;
    renderer->settings().enableGpuMeshletCull = false;
    renderer->settings().enableMeshShaders = false;
    renderer->settings().enableHiZOcclusion = false;
    renderer->settings().enableVoxelGI = false;
    renderer->settings().enableToroidalShadows = false;
    renderer->settings().giTier = GITier::Off;
    renderer->settings().enableShadows = true;
    renderer->settings().enableSSR = false;
    renderer->settings().enableIBL = true;
    renderer->settings().enableBloom = true;
    renderer->settings().enableAO = true;
    renderer->settings().enableTonemap = true;
    renderer->settings().enableAutoExposure = true;
    renderer->settings().enableContactShadows = true;
    renderer->settings().enableVisibilityBuffer = false;
    if (device->supportsRaytracing()) {
      renderer->settings().enableRTShadows = true;
    } else {
      renderer->settings().enableRTShadows = false;
      renderer->settings().enableRTReflections = false;
    }

    ecs::World ecsWorld;

    veg::VegetationSystem& vegSys = veg::VegetationSystem::instance();
    veg::WindSystem& windSys = veg::WindSystem::instance();

    veg::VegetationConfig vegCfg;
    vegCfg.globalWindStrength = 0.8f;
    vegCfg.globalWindSpeed = 0.4f;
    vegSys.configure(vegCfg);

    veg::VegetationType grassType;
    grassType.name = "Grass";
    grassType.proceduralKind = 1;
    grassType.minScale = 0.3f;
    grassType.maxScale = 0.8f;
    grassType.windFlexibility = 1.5f;
    grassType.windHeight = 0.6f;
    grassType.cullDistance = 120.0f;
    grassType.lodDistance0 = 20.0f;
    grassType.lodDistance1 = 50.0f;
    grassType.lodDistance2 = 120.0f;
    uint32_t grassId = vegSys.registerType(grassType);

    veg::VegetationType bushType;
    bushType.name = "Bush";
    bushType.proceduralKind = 2;
    bushType.minScale = 0.5f;
    bushType.maxScale = 1.5f;
    bushType.windFlexibility = 0.3f;
    bushType.windHeight = 1.2f;
    bushType.cullDistance = 200.0f;
    bushType.lodDistance0 = 40.0f;
    bushType.lodDistance1 = 90.0f;
    bushType.lodDistance2 = 200.0f;
    vegSys.registerType(bushType);

    for (int cx = -2; cx <= 2; ++cx) {
      for (int cz = -2; cz <= 2; ++cz) {
        vegSys.scatter(cx, cz, grassId, 200, 42 + cx * 100 + cz);
      }
    }
    for (int cx = -1; cx <= 1; ++cx) {
      for (int cz = -1; cz <= 1; ++cz) {
        vegSys.scatter(cx, cz, 1, 50, 100 + cx * 50 + cz);
      }
    }

    std::cout << "[Veg] Total instances: " << vegSys.instanceCount() << std::endl;

    veg::VegetationRenderer vegRenderer(*device, 100000);

    auto computeRootSig = renderer->sharedComputeRootSig();
    if (!computeRootSig) {
      std::cerr << "[Veg] No compute root signature from renderer - vegetation disabled\n";
    } else {
      vegRenderer.createPipeline(computeRootSig);
    }

    vegRenderer.uploadFromSystem(vegSys, scene.camera.position(), 200.0f);

    LuaVM::instance().init();
    LuaVM::instance().setDevice(device.get());
    LuaVM::instance().setScene(&scene);
    LuaVM::instance().setWorld(&ecsWorld);
    LuaVM::instance().setRenderer(renderer.get());
    LuaVM::instance().setCamera(&scene.camera);
    LuaVM::instance().setInput(&input);
    LuaVM::instance().registerAllBindings();
    LuaVM::instance().loadScript("Scripts/main.lua");
    LuaVM::instance().callSetup();

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      swapChain->resize(w, h);
      renderer->resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 500.0f);
    });

    int frame = 0;
    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();
      ui.beginFrame();

      LuaVM::instance().tick(1.0f / 60.0f);

      veg::SeasonSystem::instance().update(1.0f / 60.0f);
      veg::GrowthSystem::instance().update(1.0f / 60.0f);
      veg::DestructionSystem::instance().update(1.0f / 60.0f);
      veg::LODManager::instance().advanceFrame();
      veg::VegetationInteraction::instance().clearInteractionPoints();
      veg::VegetationInteraction::instance().addInteractionPoint(scene.camera.position(), 3.0f, 1.0f);
      veg::VegetationInteraction::instance().update(1.0f / 60.0f, 0);

      input.endFrame();

      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();

      windSys.update(1.0f / 60.0f);
      vegRenderer.uploadFromSystem(vegSys, scene.camera.position(), 200.0f);

      veg::VegDispatch::recordDispatch(*device, *cmd, vegRenderer,
                                       scene.camera.viewProj(), scene.camera.position(), 200.0f,
                                       renderer->hizOcclusionMip(), window.width(), window.height());

      scene.instanceClouds.clear();
      vegRenderer.submitClouds(scene.instanceClouds);

      renderer->render(cmd, bb, scene);

      scene.instanceClouds.clear();

      ui.endFrame(*cmd, bb);
      cmd->transition(bb, rhi::ResourceState::Present);
      device->endFrame(*swapChain);

      if (frame % 60 == 0) {
        float fps = 1000.0f / std::max(1.0f, renderer->lastFrameMs());
        window.setTitle("Tucano LuaLab Veg-1 | " + std::to_string(int(fps)) + " FPS | " +
                        std::to_string(vegSys.instanceCount()) + " veg instances | SPACE=jump RMB=fly");
      }
      ++frame;
    }

    device->waitIdle();
    ui.shutdown();
    LuaVM::instance().shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
