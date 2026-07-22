#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Runtime/Screenshot.h"
#include "Physics/PhysicsWorld.h"
#include "ECS/World.h"
#include "ECS/PhysicsSync.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

using namespace tucano;

static std::shared_ptr<Mesh> makeCube(rhi::Device& device, float size) {
  float s = size * 0.5f;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;

  auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
    uint32_t base = (uint32_t)verts.size();
    verts.push_back({p0, n, {t.x, t.y, t.z, 1}, {0,0}, {1,1,1,1}});
    verts.push_back({p1, n, {t.x, t.y, t.z, 1}, {1,0}, {1,1,1,1}});
    verts.push_back({p2, n, {t.x, t.y, t.z, 1}, {1,1}, {1,1,1,1}});
    verts.push_back({p3, n, {t.x, t.y, t.z, 1}, {0,1}, {1,1,1,1}});
    indices.insert(indices.end(), {base, base+1, base+2, base, base+2, base+3});
  };

  // +Y top
  face({0,1,0}, {1,0,0}, {-s,s,-s}, {s,s,-s}, {s,s,s}, {-s,s,s});
  // -Y bottom
  face({0,-1,0}, {1,0,0}, {-s,-s,s}, {s,-s,s}, {s,-s,-s}, {-s,-s,-s});
  // +Z front
  face({0,0,1}, {1,0,0}, {-s,-s,s}, {s,-s,s}, {s,s,s}, {-s,s,s});
  // -Z back
  face({0,0,-1}, {-1,0,0}, {s,-s,-s}, {-s,-s,-s}, {-s,s,-s}, {s,s,-s});
  // +X right
  face({1,0,0}, {0,0,-1}, {s,-s,s}, {s,-s,-s}, {s,s,-s}, {s,s,s});
  // -X left
  face({-1,0,0}, {0,0,1}, {-s,-s,-s}, {-s,-s,s}, {-s,s,s}, {-s,s,-s});

  SubMesh sub{};
  sub.indexCount = (uint32_t)indices.size();
  return Mesh::create(device, verts, indices, {sub});
}

int main(int argc, char** argv) {
  std::string screenshotPath;
  int maxFrames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--screenshot" && i + 1 < argc)
      screenshotPath = argv[++i];
    else if (std::string(argv[i]) == "--frames" && i + 1 < argc)
      maxFrames = std::stoi(argv[++i]);
  }

  try {
    std::cout << "PhysicsDemo starting...\n" << std::flush;
    Window window({1600, 900, "Tucano — Physics Demo"});
    std::cout << "Window OK\n" << std::flush;
    auto device = rhi::Device::create(true);
    std::cout << "Device OK\n" << std::flush;
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    std::cout << "SwapChain OK\n" << std::flush;
    Renderer renderer(*device, window.width(), window.height());
    // Preset for this simple procedural scene. The meshlet/visbuffer path needs cooked meshlet
    // data the makeCube() meshes lack, so use the classic IA path. RT shadows/reflections are
    // essentially free here (~0.3 ms) and stay on; VoxelGI/DDGI voxelizes the world every frame
    // at a fixed grid cost (~90 ms regardless of scene size) so it stays off for a few boxes.
    {
      auto& s = renderer.settings();
      s.enableMeshlets = false;
      s.enableGpuMeshletCull = false;
      s.enableMeshShaders = false;
      s.enableVisibilityBuffer = false;
      s.enableHiZOcclusion = false;
      s.enableVoxelGI = false;
      s.giTier = GITier::Off;
      s.enableSSR = false;
      s.enableContactShadows = false;
      s.enableOctahedralPointShadows = false;
    }
    std::cout << "Renderer OK\n" << std::flush;
    Input input(window.handle());

    // Physics
    physics::PhysicsWorld physics;

    // ECS
    ecs::World ecsWorld;

    // Scene
    Scene scene;
    scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    scene.camera.setPosition({0, 5, 15});
    scene.camera.lookAt({0, 1, 0});
    scene.addDirectional(glm::normalize(glm::vec3(0.3f, -1.0f, -0.5f)), {1.0f, 0.95f, 0.85f}, 4.0f);
    scene.addPoint({-8, 4, -8}, {1.0f, 1.0f, 1.0f}, 3.0f, 30.0f);

    auto cubeMesh = makeCube(*device, 1.0f);
    auto floorMesh = makeCube(*device, 1.0f);

    auto floorMat = std::make_shared<Material>();
    floorMat->baseColorFactor = {0.3f, 0.3f, 0.35f, 1.0f};
    floorMat->roughnessFactor = 0.8f;
    floorMat->metallicFactor = 0.0f;

    // Static floor
    physics.createStaticBox({15.0f, 0.5f, 15.0f}, {0, -0.5f, 0});

    RenderObject floorObj;
    floorObj.mesh = floorMesh;
    floorObj.materials = {floorMat};
    floorObj.worldMatrix = glm::scale(glm::translate(glm::mat4(1.0f), {0, -0.5f, 0}), {30.0f, 1.0f, 30.0f});
    scene.objects.push_back(std::move(floorObj));

    // Static walls
    physics.createStaticBox({0.5f, 3.0f, 15.0f}, {15.5f, 2.5f, 0});
    physics.createStaticBox({0.5f, 3.0f, 15.0f}, {-15.5f, 2.5f, 0});
    physics.createStaticBox({15.0f, 3.0f, 0.5f}, {0, 2.5f, 15.5f});
    physics.createStaticBox({15.0f, 3.0f, 0.5f}, {0, 2.5f, -15.5f});

    // Dynamic cubes
    std::vector<glm::vec3> colors = {
      {0.9f, 0.2f, 0.2f}, {0.2f, 0.9f, 0.2f}, {0.2f, 0.2f, 0.9f},
      {0.9f, 0.9f, 0.2f}, {0.9f, 0.2f, 0.9f}, {0.2f, 0.9f, 0.9f},
    };

    for (int x = 0; x < 5; ++x) {
      for (int y = 0; y < 5; ++y) {
        glm::vec3 pos = {(x - 2) * 1.8f, 1.0f + y * 1.8f, 0};
        auto bodyId = physics.createDynamicBox({0.5f, 0.5f, 0.5f}, pos, {1,0,0,0}, 1.0f);

        auto mat = std::make_shared<Material>();
        int ci = (x + y) % (int)colors.size();
        mat->baseColorFactor = {colors[ci].x, colors[ci].y, colors[ci].z, 1.0f};
        mat->roughnessFactor = 0.5f;
        mat->metallicFactor = 0.1f;

        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.materials = {mat};
        obj.worldMatrix = glm::translate(glm::mat4(1.0f), pos);
        scene.objects.push_back(std::move(obj));

        auto e = ecsWorld.createWith<ecs::TransformComponent, ecs::PhysicsBodyComponent,
                                      ecs::RenderObjectComponent>();
        *ecsWorld.get<ecs::TransformComponent>(e) = {pos, {1, 0, 0, 0}, {1, 1, 1}, pos, {1, 0, 0, 0}};
        ecsWorld.get<ecs::PhysicsBodyComponent>(e)->joltBodyId = bodyId;
        ecsWorld.get<ecs::RenderObjectComponent>(e)->sceneIndex = uint32_t(scene.objects.size() - 1);
      }
    }

    // Character controller
    auto* character = physics.createCharacter({0, 5, 12});

    auto charMat = std::make_shared<Material>();
    charMat->baseColorFactor = {0.1f, 0.8f, 0.3f, 1.0f};
    charMat->roughnessFactor = 0.3f;
    charMat->metallicFactor = 0.0f;

    RenderObject charObj;
    charObj.mesh = makeCube(*device, 1.0f);
    charObj.materials = {charMat};
    charObj.worldMatrix = glm::scale(glm::translate(glm::mat4(1.0f), {0, 1.6f, 12}), {0.6f, 1.6f, 0.6f});
    scene.objects.push_back(std::move(charObj));

    auto charEntity = ecsWorld.createWith<ecs::TransformComponent, ecs::RenderObjectComponent>();
    *ecsWorld.get<ecs::TransformComponent>(charEntity) =
        {{0, 1.6f, 12}, {1, 0, 0, 0}, {0.6f, 1.6f, 0.6f}, {0, 1.6f, 12}, {1, 0, 0, 0}};
    ecsWorld.get<ecs::RenderObjectComponent>(charEntity)->sceneIndex = uint32_t(scene.objects.size() - 1);

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      swapChain->resize(w, h);
      renderer.resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    });

    int frame = 0;
    bool shotDone = screenshotPath.empty();

    const float fixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    // Simulation speed. Real-time (1.0) feels fast for a drop demo, so default to a gentle
    // slow-motion; [ and ] adjust it live. Headless --frames runs stay at 1.0 for determinism.
    float timeScale = (maxFrames >= 0) ? 1.0f : 0.5f;
    auto prevTime = std::chrono::high_resolution_clock::now();

    // Headless-friendly: a fixed --frames run ignores window close so physics runs to completion.
    while ((maxFrames >= 0 && frame < maxFrames) || (maxFrames < 0 && !window.shouldClose())) {
      window.pollEvents();
      input.beginFrame();

      // Wall-clock delta → fixed-timestep accumulator, so physics runs at real speed regardless
      // of frame rate (a fixed dt-per-frame ran in slow motion whenever the frame rate dipped).
      // Headless runs use a synthetic 1/60 step for deterministic screenshots.
      float realDt = fixedDt;
      if (maxFrames < 0) {
        const auto now = std::chrono::high_resolution_clock::now();
        realDt = std::chrono::duration<float>(now - prevTime).count();
        prevTime = now;
      }
      // Live speed control: [ slows down, ] speeds up (0.1x .. 2.0x).
      if (input.keyPressed(GLFW_KEY_LEFT_BRACKET))  timeScale = std::max(0.1f, timeScale - 0.1f);
      if (input.keyPressed(GLFW_KEY_RIGHT_BRACKET)) timeScale = std::min(2.0f, timeScale + 0.1f);
      accumulator += std::min(realDt, 0.1f) * timeScale; // clamp to avoid a spiral of death after a stall

      // Character movement (world-space). The camera sits behind the character looking down -Z,
      // which mirrors the X axis on screen, so A/D map to +X/-X to match the player's view.
      glm::vec3 wishDir{0};
      if (input.keyDown(GLFW_KEY_W)) wishDir.z -= 1;
      if (input.keyDown(GLFW_KEY_S)) wishDir.z += 1;
      if (input.keyDown(GLFW_KEY_A)) wishDir.x += 1;
      if (input.keyDown(GLFW_KEY_D)) wishDir.x -= 1;
      if (input.keyPressed(GLFW_KEY_SPACE)) physics.jumpCharacter(character);

      // Camera follows the character while it is on/near the arena; if it falls out of the
      // world (character controller sinking through the floor) fall back to a fixed framing
      // that keeps the cube stack in view instead of staring into the void.
      if (character && character->joltCharacter) {
        auto joltPos = character->joltCharacter->GetPosition();
        if (joltPos.GetY() > -3.0f) {
          scene.camera.setPosition({joltPos.GetX(), joltPos.GetY() + 5.0f, joltPos.GetZ() + 8.0f});
          scene.camera.lookAt({joltPos.GetX(), joltPos.GetY() + 1.0f, joltPos.GetZ() + 1.0f});
        } else {
          scene.camera.setPosition({12.0f, 9.0f, 18.0f});
          scene.camera.lookAt({0.0f, 3.5f, 0.0f});
        }
      }

      input.endFrame();

      // Step physics in fixed sub-steps to consume the accumulated real time. Sync inside the
      // loop so each transform's prev/current pair straddles exactly one fixed step, which the
      // render then interpolates between (smooth motion despite a high, variable frame rate).
      auto* charTransform = ecsWorld.get<ecs::TransformComponent>(charEntity);
      int subSteps = 0;
      while (accumulator >= fixedDt && subSteps < 5) {
        if (charTransform) {
          charTransform->prevPosition = charTransform->position;
          charTransform->prevRotation = charTransform->rotation;
        }
        physics.moveCharacter(character, wishDir, fixedDt, 8.0f);
        physics.step(fixedDt);
        ecs::syncPhysicsToTransforms(physics, ecsWorld);
        if (character && character->joltCharacter && charTransform) {
          auto jp = character->joltCharacter->GetPosition();
          charTransform->position = {jp.GetX(), jp.GetY(), jp.GetZ()};
        }
        accumulator -= fixedDt;
        ++subSteps;
      }

      // Render at the interpolated state between the last two fixed steps (headless uses the
      // current state directly for deterministic screenshots).
      const float alpha = (maxFrames >= 0) ? 1.0f : accumulator / fixedDt;
      ecs::syncTransformsToScene(ecsWorld, scene, alpha);

      // Render
      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer.render(cmd, bb, scene);
      if (frame == 0) {
        std::cout << "drawCalls=" << renderer.drawCalls() << " frameMs=" << renderer.lastFrameMs() << "\n"
                  << "Controls: WASD move, Space jump, [ / ] slow down / speed up simulation\n"
                  << std::flush;
      }

      // Capture near the end of a fixed --frames run so physics has time to settle.
      const int shotFrame = (maxFrames > 5) ? maxFrames - 2 : 3;
      ScreenshotPending shot;
      if (!shotDone && frame >= shotFrame)
        shot = beginScreenshot(*device, *cmd, bb);
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
