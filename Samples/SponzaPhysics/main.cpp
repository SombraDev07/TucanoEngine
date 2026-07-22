#include "AssetPipeline/GLTFLoader.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"
#include "Physics/PhysicsWorld.h"
#include "ECS/World.h"
#include "ECS/PhysicsSync.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using namespace tucano;

static std::shared_ptr<Mesh> makeSphere(rhi::Device& device, float radius, int segments) {
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  for (int y = 0; y <= segments; ++y) {
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / float(segments);
      const float v = float(y) / float(segments);
      const float theta = u * 6.2831853f;
      const float phi   = v * 3.14159265f;
      Vertex vert{};
      vert.position = {radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi),
                       radius * std::sin(phi) * std::sin(theta)};
      vert.normal   = glm::normalize(vert.position);
      vert.tangent  = {1, 0, 0, 1};
      vert.uv       = {u, v};
      vert.color    = {1, 1, 1, 1};
      verts.push_back(vert);
    }
  }
  for (int y = 0; y < segments; ++y) {
    for (int x = 0; x < segments; ++x) {
      const uint32_t i0 = y * (segments + 1) + x;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + (segments + 1);
      const uint32_t i3 = i2 + 1;
      indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
      indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }
  }
  SubMesh sub{};
  sub.indexCount = static_cast<uint32_t>(indices.size());
  return Mesh::create(device, verts, indices, {sub});
}

namespace {
void setupDefaultLights(Scene& scene) {
  scene.lights.clear();
  scene.addDirectional(glm::normalize(glm::vec3(-0.45f, -1.0f, 0.15f)), {1.0f, 0.96f, 0.9f}, 8.0f);
  scene.addPoint({0.0f, 2.5f, 0.0f}, {1.0f, 0.85f, 0.6f}, 20.0f, 12.0f);
  scene.addPoint({-4.0f, 1.5f, -1.0f}, {0.4f, 0.6f, 1.0f}, 12.0f, 8.0f);
  scene.addPoint({4.0f, 1.5f, 1.0f}, {1.0f, 0.5f, 0.3f}, 12.0f, 8.0f);
  scene.addPoint({0.0f, 4.0f, 3.0f}, {0.8f, 0.9f, 1.0f}, 10.0f, 10.0f);
  scene.addSpot({-2.0f, 5.0f, 0.0f}, glm::normalize(glm::vec3(0.35f, -1.0f, 0.1f)), {1.0f, 0.92f, 0.75f}, 40.0f,
                18.0f, 15.0f, 35.0f);
}
} // anon

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
    std::cout << "SponzaPhysics starting...\n" << std::flush;
    Window window({1920, 1080, "Tucano — Sponza Physics Test"});
    std::cout << "Window OK\n" << std::flush;
    auto device = rhi::Device::create(true);
    std::cout << "Device OK\n" << std::flush;
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    std::cout << "SwapChain OK\n" << std::flush;
    Renderer renderer(*device, window.width(), window.height());
    std::cout << "Renderer OK\n" << std::flush;    
    Input input(window.handle());

    // Load Sponza
    Scene scene;
    std::cout << "Loading Sponza..." << std::flush;
    if (!loadGLTFScene(*device, "Assets/Sponza/Sponza.gltf", scene)) {
      std::cerr << "Failed to load Sponza\n";
      return 1;
    }
    std::cout << " OK (" << scene.objects.size() << " meshes)\n" << std::flush;

    setupDefaultLights(scene);
    scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 200.0f);
    scene.camera.setPosition({0, 6, 12});
    scene.camera.lookAt({0, 3, 0});

    // Build physics mesh from all Sponza objects
    physics::PhysicsWorld physics;
    physics::PhysicsWorld meshPhysics(65536);

    std::vector<glm::vec3> allVerts;
    std::vector<uint32_t> allIndices;
    for (auto& obj : scene.objects) {
      if (!obj.mesh) continue;
      uint32_t vertOffset = static_cast<uint32_t>(allVerts.size());
      const auto& positions = obj.mesh->cpuPositions();
      const auto& packedIdx = obj.mesh->packedIndices();

      for (size_t v = 0; v < positions.size(); ++v) {
        glm::vec3 worldPos = glm::vec3(obj.worldMatrix * glm::vec4(positions[v], 1.0f));
        allVerts.push_back(worldPos);
      }

      for (size_t i = 0; i < obj.mesh->indexCount(); ++i) {
        allIndices.push_back(vertOffset + packedIdx[i]);
      }
    }
    std::cout << "Physics mesh: " << allVerts.size() << " verts, " << allIndices.size() << " tris\n" << std::flush;

    // Create static body for Sponza
    meshPhysics.createStaticMesh(allVerts, allIndices, {0, 0, 0});
    std::cout << "Sponza static body created\n" << std::flush;

    // Make spheres
    auto sphereMesh = makeSphere(*device, 0.4f, 24);

    // Colors for spheres
    std::vector<glm::vec3> sphereColors = {
      {0.9f, 0.2f, 0.2f}, {0.2f, 0.8f, 0.2f}, {0.2f, 0.3f, 1.0f},
      {0.9f, 0.8f, 0.1f}, {0.8f, 0.2f, 0.8f}, {0.1f, 0.8f, 0.8f},
    };

    // ECS for tracking physics spheres
    ecs::World ecsWorld;
    struct SphereInfo { JPH::BodyID bodyId; float hue; };
    std::vector<SphereInfo> spheres;

    // Spawn 6 spheres above Sponza
    for (int i = 0; i < 6; ++i) {
      glm::vec3 pos = {(i - 2.5f) * 1.5f, 8.0f + i * 0.5f, i * 0.8f - 2.0f};
      auto bodyId = meshPhysics.createDynamicSphere(0.4f, pos, 2.0f);

      auto mat = std::make_shared<Material>();
      mat->baseColorFactor = {sphereColors[i].x, sphereColors[i].y, sphereColors[i].z, 1.0f};
      mat->roughnessFactor = 0.3f;
      mat->metallicFactor = 0.8f;

      RenderObject obj;
      obj.mesh = sphereMesh;
      obj.materials = {mat};
      obj.worldMatrix = glm::translate(glm::mat4(1.0f), pos);
      scene.objects.push_back(std::move(obj));

      auto e = ecsWorld.create();
      ecsWorld.transforms.add(e, {pos, {1,0,0,0}, {1,1,1}});
      ecsWorld.physicsBodies.add(e, {bodyId});
      ecsWorld.renderObjects.add(e, {scene.objects.size() - 1});

      spheres.push_back({bodyId, float(i) / 6.0f});
    }

    // Grabbed sphere state
    JPH::BodyID grabbedBodyId;
    float grabDist = 5.0f;
    bool grabbing = false;

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

      // Fly camera (RMB)
      float dx = 0, dy = 0;
      input.mouseDelta(dx, dy);
      glm::vec3 move(0);
      if (input.keyDown(GLFW_KEY_W)) move.z += 1;
      if (input.keyDown(GLFW_KEY_S)) move.z -= 1;
      if (input.keyDown(GLFW_KEY_A)) move.x -= 1;
      if (input.keyDown(GLFW_KEY_D)) move.x += 1;
      if (input.keyDown(GLFW_KEY_E)) move.y += 1;
      if (input.keyDown(GLFW_KEY_Q)) move.y -= 1;
      if (input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        scene.camera.fly(fixedDt, move * 8.0f, dx * 0.002f, -dy * 0.002f);
      } else {
        scene.camera.fly(fixedDt, move * 8.0f, 0, 0);
      }

      // Left click: raycast to grab/release sphere
      if (input.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        glm::vec3 origin = scene.camera.position();
        glm::vec3 dir = scene.camera.forward();

        float hitDist = 0;
        glm::vec3 hitNormal;
        if (meshPhysics.rayCast(origin, dir, 30.0f, hitDist, hitNormal)) {
          // Find if we hit a dynamic sphere
          glm::vec3 hitPoint = origin + dir * hitDist;
          JPH::BodyID closestBody;
          grabbedBodyId = JPH::BodyID();
          grabDist = 0;

          for (auto& s : spheres) {
            auto pos = meshPhysics.getBodyPosition(s.bodyId);
            float d = glm::distance(pos, hitPoint);
            if (d < 1.0f) {
              grabbedBodyId = s.bodyId;
              grabDist = d;
              break;
            }
          }

          if (!grabbedBodyId.IsInvalid()) {
            grabbing = true;
            // Make kinematic so we can move it
            meshPhysics.bodyInterface().SetMotionType(grabbedBodyId, JPH::EMotionType::Kinematic,
                                                       JPH::EActivation::Activate);
            std::cout << "Grabbed sphere at " << hitDist << "m\n";
          }
        }
      }

      if (grabbing && !input.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
        // Release: throw the sphere
        glm::vec3 throwVel = scene.camera.forward() * 15.0f + glm::vec3(0, 3, 0);
        meshPhysics.bodyInterface().SetMotionType(grabbedBodyId, JPH::EMotionType::Dynamic,
                                                   JPH::EActivation::Activate);
        meshPhysics.setLinearVelocity(grabbedBodyId, throwVel);
        grabbing = false;
        grabbedBodyId = JPH::BodyID();
        std::cout << "Thrown!\n";
      }

      if (grabbing) {
        // Move grabbed sphere in front of camera
        glm::vec3 grabPos = scene.camera.position() + scene.camera.forward() * 4.0f;
        meshPhysics.setBodyTransform(grabbedBodyId, grabPos, {1,0,0,0});
      }

      if (input.keyPressed(GLFW_KEY_R)) {
        // Reset: re-spawn spheres
        for (size_t i = 0; i < spheres.size(); ++i) {
          glm::vec3 pos = {(float(i) - 2.5f) * 1.5f, 8.0f + float(i) * 0.5f, float(i) * 0.8f - 2.0f};
          meshPhysics.bodyInterface().SetMotionType(spheres[i].bodyId, JPH::EMotionType::Dynamic,
                                                     JPH::EActivation::Activate);
          meshPhysics.setBodyTransform(spheres[i].bodyId, pos, {1,0,0,0});
          meshPhysics.setLinearVelocity(spheres[i].bodyId, {0,0,0});
        }
        grabbing = false;
        std::cout << "Spheres reset!\n";
      }

      input.endFrame();

      // Step physics
      meshPhysics.step(fixedDt);

      // Sync physics → ECS → Scene
      ecs::syncPhysicsToTransforms(meshPhysics, ecsWorld);
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
