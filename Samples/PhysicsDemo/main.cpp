#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Runtime/Screenshot.h"
#include "Physics/PhysicsWorld.h"
#include "ECS/World.h"
#include "ECS/PhysicsSync.h"

#include <GLFW/glfw3.h>
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

        auto e = ecsWorld.create();
        ecsWorld.transforms.add(e, {pos, {1,0,0,0}, {1,1,1}});
        ecsWorld.physicsBodies.add(e, {bodyId});
        ecsWorld.renderObjects.add(e, {scene.objects.size() - 1});
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

    auto charEntity = ecsWorld.create();
    ecsWorld.transforms.add(charEntity, {{0, 1.6f, 12}, {1,0,0,0}, {0.6f, 1.6f, 0.6f}});
    ecsWorld.renderObjects.add(charEntity, {scene.objects.size() - 1});

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      swapChain->resize(w, h);
      renderer.resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    });

    int frame = 0;
    bool shotDone = screenshotPath.empty();

    float fixedDt = 1.0f / 60.0f;

    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();

      // Character movement
      glm::vec3 wishDir{0};
      if (input.keyDown(GLFW_KEY_W)) wishDir.z -= 1;
      if (input.keyDown(GLFW_KEY_S)) wishDir.z += 1;
      if (input.keyDown(GLFW_KEY_A)) wishDir.x -= 1;
      if (input.keyDown(GLFW_KEY_D)) wishDir.x += 1;
      physics.moveCharacter(character, wishDir, fixedDt, 8.0f);
      if (input.keyPressed(GLFW_KEY_SPACE)) physics.jumpCharacter(character);

      // Camera follows character
      if (character->joltCharacter) {
        auto joltPos = character->joltCharacter->GetPosition();
        scene.camera.setPosition({joltPos.GetX(), joltPos.GetY() + 5.0f, joltPos.GetZ() + 8.0f});
        scene.camera.lookAt({joltPos.GetX(), joltPos.GetY() + 1.0f, joltPos.GetZ() + 1.0f});
      }

      input.endFrame();

      // Step physics
      physics.step(fixedDt);

      // Sync physics → ECS → Scene
      ecs::syncPhysicsToTransforms(physics, ecsWorld);

      // Update character entity transform
      if (character->joltCharacter) {
        auto joltPos = character->joltCharacter->GetPosition();
        auto* charTransform = ecsWorld.transforms.get(charEntity);
        if (charTransform) {
          charTransform->position = {joltPos.GetX(), joltPos.GetY(), joltPos.GetZ()};
        }
      }
      ecs::syncTransformsToScene(ecsWorld, scene);

      // Render
      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      renderer.render(cmd, bb, scene);
      if (frame == 0) {
        std::cout << "drawCalls=" << renderer.drawCalls() << " frameMs=" << renderer.lastFrameMs() << "\n" << std::flush;
      }

      ScreenshotPending shot;
      if (!shotDone && frame >= 3)
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
