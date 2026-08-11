#pragma once

#include "Renderer/Camera.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene.h"
#include "Renderer/Weather/WaterParams.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace tucano::waterlab {

inline void configureWaterRenderer(Renderer& renderer) {
  auto& s = renderer.settings();
  auto& clouds = renderer.clouds();

  s.sky.enableAtmosphere = true;
  s.sky.atmosphereDrivesSun = true;
  s.sky.useBrunetonAtmosphere = true;
  clouds.enabled = true;
  clouds.enableShadows = true;
  clouds.enableGodRays = true;
  clouds.driveRain = false;
  s.enableShadows = true;
  s.enableIBL = true;
  s.enableBloom = true;
  s.enableAO = true;
  s.enableTonemap = true;
  s.enableAutoExposure = true;
  s.enableSSR = true;
  s.enableContactShadows = true;
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

  s.sky.timeOfDay = 0.38f;
  s.sky.turbidity = 2.8f;
  s.sky.fogDensity = 0.008f;
  s.sky.fogHeight = 60.0f;
  clouds.coverage = 0.48f;
  clouds.density = 1.15f;
  clouds.altitude = 1400.0f;
  clouds.thickness = 2400.0f;
  clouds.storminess = 0.3f;
  s.sky.wind = {0.25f, 0.0f, 0.08f};

  renderer.rain().enabled = false;
  renderer.rain().enableSceneRain = false;
  renderer.rain().amount = 0.85f;
  renderer.rain().puddlesAmount = 1.1f;
  renderer.rain().streakIntensity = 1.0f;
  renderer.rain().mistAmount = 0.3f;
}

inline void buildWaterScene(rhi::Device& device, Scene& scene) {
  scene.objects.clear();
  scene.lights.clear();

  scene.camera.setPerspective(glm::radians(65.0f), 16.0f / 9.0f, 0.2f, 4000.0f);
  scene.camera.setPosition({0.0f, 12.0f, 35.0f});
  scene.camera.lookAt({0.0f, -1.0f, 0.0f});

  scene.addDirectional(glm::normalize(glm::vec3(-0.25f, -1.0f, -0.15f)), {1.0f, 0.96f, 0.85f}, 5.0f);

  // Large underwater ground plane
  auto groundMat = std::make_shared<Material>();
  groundMat->name = "Ground";
  groundMat->baseColorFactor = {0.28f, 0.23f, 0.15f, 1.0f};
  groundMat->roughnessFactor = 0.9f;
  groundMat->metallicFactor = 0.0f;

  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  int segs = 64;
  float half = 600.0f;
  verts.reserve(size_t(segs + 1) * size_t(segs + 1));
  for (int z = 0; z <= segs; ++z) {
    for (int x = 0; x <= segs; ++x) {
      float u = float(x) / float(segs);
      float v = float(z) / float(segs);
      float px = (u * 2.0f - 1.0f) * half;
      float pz = (v * 2.0f - 1.0f) * half;
      Vertex vert{};
      vert.position = {px, -4.0f + sin(u * 20.0f) * cos(v * 20.0f) * 1.5f, pz};
      vert.normal = {0, 1, 0};
      vert.tangent = {1, 0, 0, 1};
      vert.uv = {u * 64.0f, v * 64.0f};
      vert.color = {1, 1, 1, 1};
      verts.push_back(vert);
    }
  }
  for (int z = 0; z < segs; ++z) {
    for (int x = 0; x < segs; ++x) {
      uint32_t i0 = uint32_t(z * (segs + 1) + x);
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + uint32_t(segs + 1);
      uint32_t i3 = i2 + 1;
      indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
      indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }
  }
  SubMesh sub{};
  sub.indexCount = static_cast<uint32_t>(indices.size());
  sub.aabbMin = {-half, -10.0f, -half};
  sub.aabbMax = {half, 10.0f, half};

  RenderObject ground;
  ground.name = "Ground";
  ground.mesh = Mesh::create(device, verts, indices, {sub});
  ground.materials = {groundMat};
  ground.worldMatrix = glm::mat4(1.0f);
  scene.objects.push_back(std::move(ground));

  // Buoys / marker spheres
  auto sphereMesh = [&]() -> std::shared_ptr<Mesh> {
    std::vector<Vertex> sv;
    std::vector<uint32_t> si;
    int sseg = 24;
    float r = 0.5f;
    for (int y = 0; y <= sseg; ++y) {
      for (int x = 0; x <= sseg; ++x) {
        float u = float(x) / float(sseg);
        float v = float(y) / float(sseg);
        float theta = u * 6.2831853f;
        float phi = v * 3.14159265f;
        Vertex vert{};
        float spx = r * sin(phi) * cos(theta);
        float spy = r * cos(phi);
        float spz = r * sin(phi) * sin(theta);
        vert.position = {spx, spy, spz};
        vert.normal = normalize(vert.position);
        vert.tangent = {1, 0, 0, 1};
        vert.uv = {u, v};
        vert.color = {1, 1, 1, 1};
        sv.push_back(vert);
      }
    }
    for (int y = 0; y < sseg; ++y) {
      for (int x = 0; x < sseg; ++x) {
        uint32_t i0 = uint32_t(y * (sseg + 1) + x);
        uint32_t i1 = i0 + 1;
        uint32_t i2 = i0 + uint32_t(sseg + 1);
        uint32_t i3 = i2 + 1;
        si.push_back(i0); si.push_back(i2); si.push_back(i1);
        si.push_back(i1); si.push_back(i2); si.push_back(i3);
      }
    }
    SubMesh ssub{};
    ssub.indexCount = static_cast<uint32_t>(si.size());
    return Mesh::create(device, sv, si, {ssub});
  }();

  const glm::vec3 buoyColors[] = {
    {0.85f, 0.2f, 0.15f},
    {0.15f, 0.85f, 0.2f},
    {0.2f, 0.25f, 0.9f},
    {0.9f, 0.75f, 0.1f},
    {0.85f, 0.85f, 0.85f},
  };
  const float buoyPositions[][2] = {
    {-8, -4}, {5, 7}, {-3, -10}, {10, -5}, {-12, 6},
  };

  for (int i = 0; i < 5; ++i) {
    auto mat = std::make_shared<Material>();
    mat->name = "Buoy";
    mat->baseColorFactor = glm::vec4(buoyColors[i], 1.0f);
    mat->roughnessFactor = 0.3f;
    mat->metallicFactor = 0.5f;

    RenderObject obj;
    obj.name = "Buoy" + std::to_string(i);
    obj.mesh = sphereMesh;
    obj.materials = {mat};
    obj.worldMatrix = glm::translate(glm::mat4(1.0f), {buoyPositions[i][0], 1.5f, buoyPositions[i][1]});
    scene.objects.push_back(std::move(obj));
  }

  // Tall markers standing out of the water. Spheres floating at 1.5 m are too small to show a
  // reflection, so without something like this there is no way to tell screen-space reflections
  // from a plain sky mirror.
  auto boxMesh = [&](float w, float h, float d) -> std::shared_ptr<Mesh> {
    std::vector<Vertex> bv;
    std::vector<uint32_t> bi;
    const glm::vec3 hs{w * 0.5f, h * 0.5f, d * 0.5f};
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
      const glm::vec3 n = normals[f];
      // Any axis not parallel to the face normal works as the in-plane basis.
      const glm::vec3 ref = std::abs(n.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
      const glm::vec3 right = glm::normalize(glm::cross(ref, n));
      const glm::vec3 up = glm::cross(n, right);
      const glm::vec3 centre = n * hs;
      const glm::vec3 rx = right * glm::abs(glm::dot(right, hs));
      const glm::vec3 uy = up * glm::abs(glm::dot(up, hs));
      const uint32_t base = uint32_t(bv.size());
      const glm::vec3 offsets[4] = {-rx - uy, rx - uy, rx + uy, -rx + uy};
      const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
      for (int k = 0; k < 4; ++k) {
        Vertex v{};
        v.position = centre + offsets[k];
        v.normal = n;
        v.tangent = glm::vec4(right, 1.0f);
        v.uv = uvs[k];
        v.color = {1, 1, 1, 1};
        bv.push_back(v);
      }
      bi.insert(bi.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    SubMesh bsub{};
    bsub.indexCount = uint32_t(bi.size());
    bsub.aabbMin = -hs;
    bsub.aabbMax = hs;
    return Mesh::create(device, bv, bi, {bsub});
  }(1.6f, 14.0f, 1.6f);

  const glm::vec3 pillarColors[] = {{0.92f, 0.28f, 0.12f}, {0.95f, 0.82f, 0.18f}, {0.15f, 0.55f, 0.95f}};
  const float pillarPositions[][2] = {{-14.0f, -18.0f}, {14.0f, -26.0f}, {0.0f, -40.0f}};
  for (int i = 0; i < 3; ++i) {
    auto mat = std::make_shared<Material>();
    mat->name = "Marker";
    mat->baseColorFactor = glm::vec4(pillarColors[i], 1.0f);
    mat->roughnessFactor = 0.45f;
    mat->metallicFactor = 0.0f;
    mat->emissiveFactor = pillarColors[i] * 0.35f;

    RenderObject obj;
    obj.name = "Marker" + std::to_string(i);
    obj.mesh = boxMesh;
    obj.materials = {mat};
    obj.worldMatrix =
        glm::translate(glm::mat4(1.0f), {pillarPositions[i][0], 6.0f, pillarPositions[i][1]});
    scene.objects.push_back(std::move(obj));
  }
}

} // namespace tucano::waterlab
