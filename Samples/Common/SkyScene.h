#pragma once

// Shared procedural scene for SkyLab / TestEditor (atmosphere + clouds sandbox).

#include "Renderer/Camera.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace tucano::skylab {

inline std::shared_ptr<Mesh> makeGroundPlane(rhi::Device& device, float halfExtent, int segments) {
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  verts.reserve(size_t(segments + 1) * size_t(segments + 1));
  for (int z = 0; z <= segments; ++z) {
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / float(segments);
      const float v = float(z) / float(segments);
      Vertex vert{};
      vert.position = {(u * 2.0f - 1.0f) * halfExtent, 0.0f, (v * 2.0f - 1.0f) * halfExtent};
      vert.normal = {0, 1, 0};
      vert.tangent = {1, 0, 0, 1};
      vert.uv = {u * 32.0f, v * 32.0f};
      vert.color = {1, 1, 1, 1};
      verts.push_back(vert);
    }
  }
  for (int z = 0; z < segments; ++z) {
    for (int x = 0; x < segments; ++x) {
      const uint32_t i0 = uint32_t(z * (segments + 1) + x);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + uint32_t(segments + 1);
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
  sub.aabbMin = {-halfExtent, -0.01f, -halfExtent};
  sub.aabbMax = {halfExtent, 0.01f, halfExtent};
  return Mesh::create(device, verts, indices, {sub});
}

inline std::shared_ptr<Mesh> makeMarkerSphere(rhi::Device& device, float radius, int segments) {
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  for (int y = 0; y <= segments; ++y) {
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / float(segments);
      const float v = float(y) / float(segments);
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
      const uint32_t i0 = uint32_t(y * (segments + 1) + x);
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + uint32_t(segments + 1);
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

inline void configureCleanRenderer(Renderer& renderer) {
  auto& s = renderer.settings();
  // Keep the lab light: sky / atmosphere / clouds only.
  s.enableAtmosphere = true;
  s.atmosphereDrivesSun = true;
  s.enableClouds = true;
  s.enableCloudShadows = true;
  s.enableCloudGodRays = true;
  s.cloudsDriveRain = false;
  s.timeOfDay = 0.42f;
  s.turbidity = 2.6f;
  s.fogDensity = 0.006f;
  s.fogHeight = 80.0f;
  s.cloudCoverage = 0.52f;
  s.cloudDensity = 1.2f;
  s.cloudAltitude = 1400.0f;
  s.cloudThickness = 2400.0f;
  s.cloudStorminess = 0.3f;
  s.wind = {0.25f, 0.0f, 0.08f};

  s.enableShadows = true;
  s.enableIBL = true;
  s.enableBloom = true;
  s.enableAO = true;
  s.enableTonemap = true;
  s.enableAutoExposure = true;
  s.enableSSR = false;
  s.enableContactShadows = false;
  s.enableRTShadows = false;
  s.enableRTReflections = false;
  s.enableVSM = false;
  s.enableAsyncCompute = false;
  s.enableOctahedralPointShadows = false;
  s.enableVisibilityBuffer = false;
  s.enableGpuMeshletCull = false;
  s.enableMeshShaders = false;
  s.enableMeshlets = false;
  s.enableHiZOcclusion = false;
  s.enableVoxelGI = false;
  s.enableToroidalShadows = false;
  s.giTier = GITier::Off;

  // Rain off by default in sky lab (open field). Keep amount ready so Enable rain works.
  renderer.rain().enabled = false;
  renderer.rain().enableSceneRain = false; // cones look like giant pillars outdoors
  renderer.rain().enableWorldSplashes = true;
  renderer.rain().amount = 0.85f;
  renderer.rain().streakIntensity = 1.0f;
  renderer.rain().puddlesAmount = 1.1f;
  renderer.rain().mistAmount = 0.35f;
}

inline void buildCleanScene(rhi::Device& device, Scene& scene) {
  scene.objects.clear();
  scene.lights.clear();

  scene.camera.setPerspective(glm::radians(65.0f), 16.0f / 9.0f, 0.2f, 4000.0f);
  scene.camera.setPosition({0.0f, 4.0f, 18.0f});
  scene.camera.lookAt({0.0f, 3.0f, 0.0f});

  scene.addDirectional(glm::normalize(glm::vec3(-0.35f, -1.0f, -0.2f)), {1.0f, 0.96f, 0.9f}, 6.0f);

  auto groundMat = std::make_shared<Material>();
  groundMat->name = "Ground";
  groundMat->baseColorFactor = {0.22f, 0.28f, 0.18f, 1.0f};
  groundMat->roughnessFactor = 0.92f;
  groundMat->metallicFactor = 0.0f;

  RenderObject ground;
  ground.name = "Ground";
  ground.mesh = makeGroundPlane(device, 800.0f, 32);
  ground.materials = {groundMat};
  ground.worldMatrix = glm::mat4(1.0f);
  scene.objects.push_back(std::move(ground));

  auto sphereMesh = makeMarkerSphere(device, 0.6f, 20);
  const glm::vec3 colors[] = {{0.85f, 0.85f, 0.88f}, {0.75f, 0.55f, 0.35f}, {0.35f, 0.45f, 0.7f}};
  for (int i = 0; i < 3; ++i) {
    auto mat = std::make_shared<Material>();
    mat->name = "Marker";
    mat->baseColorFactor = glm::vec4(colors[i], 1.0f);
    mat->roughnessFactor = 0.35f + float(i) * 0.2f;
    mat->metallicFactor = i == 0 ? 0.8f : 0.05f;
    RenderObject obj;
    obj.name = "Marker" + std::to_string(i);
    obj.mesh = sphereMesh;
    obj.materials = {mat};
    obj.worldMatrix = glm::translate(glm::mat4(1.0f), {float(i - 1) * 3.0f, 0.6f, 0.0f});
    scene.objects.push_back(std::move(obj));
  }
}

} // namespace tucano::skylab
