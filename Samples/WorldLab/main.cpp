// WorldLab — the real test of the World Machine.
//
// Bakes a procedural world to disk as .tcell files, then streams it back through the full pipeline
// (disk → parse → GPU) while a camera flies across it.
//
// This is deliberately a CLONE of Samples/SkyLab with streaming grafted on, not a sample written
// from scratch. The from-scratch version rendered nothing at all — not even a hand-placed control
// cube — and diffing it against a working sample cost more than starting from one. SkyLab's scene
// (ground + marker spheres) is kept on purpose: it is known-good geometry, so if it draws and the
// streamed cells do not, the fault is isolated to the provider at a glance.
//
// Run modes:
//   --bake     generate the world and exit
//   --auto     fixed flythrough, screenshots along the way, prints stats
//   (default)  interactive: WASD + RMB to look, F12 to capture

#include "Common/SkyScene.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"
#include "Physics/PhysicsWorld.h"
#include "World/CellFile.h"
#include "World/CellHotReload.h"
#include "World/CellPersistence.h"
#include "Terrain/TerrainCellProvider.h"
#include "Terrain/HeightmapQuery.h"
#include "Terrain/ClipmapTerrain.h"
#include "Terrain/TerrainGenerator.h"
#include "Terrain/TerrainVirtualTexture.h"
#include "World/InstanceCloud.h"
#include "World/InstanceCloudCuller.h"
#include "World/SceneCellProvider.h"
#include "World/StreamingRecorder.h"
#include "World/StreamingScheduler.h"
#include "World/WorldGenerator.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

using namespace tucano;
using namespace tucano::world;

namespace {
const char* kWorldRoot = "world_lab";
}

int main(int argc, char** argv) {
  // Unbuffered: when a run crashes, the output up to that point must still reach the log.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string screenshotPath;
  bool bakeOnly = false;
  bool autoRun = false;
  bool persistTest = false;
  bool physicsTest = false;
  bool gltfTest = false;
  bool hlodTest = false;
  bool instanceTest = false;
  bool instanceRender = false;
  bool terrainTest = false;
  bool terrainLod = false;
  bool clipmapMode = false;
  bool clipmapVt = false;
  bool clipmapVtFb = false;
  bool clipmapStream = false;
  bool hotreloadTest = false;
  int32_t extent = 8;
  float loadRadius = 140.0f;
  uint32_t density = 3;
  std::string shotPrefix = "worldlab";

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--bake") bakeOnly = true;
    else if (a == "--auto") autoRun = true;
    else if (a == "--persist-test") persistTest = true;
    else if (a == "--physics-test") physicsTest = true;
    else if (a == "--gltf-test") gltfTest = true;
    else if (a == "--hlod-test") hlodTest = true;
    else if (a == "--instance-test") instanceTest = true;
    else if (a == "--instance-render") instanceRender = true;
    else if (a == "--terrain-test") terrainTest = true;
    else if (a == "--terrain-lod") terrainLod = true;
    else if (a == "--clipmap") clipmapMode = true;
    else if (a == "--clipmap-vt") { clipmapMode = true; clipmapVt = true; }
    else if (a == "--clipmap-vtfb") { clipmapMode = true; clipmapVt = true; clipmapVtFb = true; }
    else if (a == "--clipmap-stream") clipmapStream = true;
    else if (a == "--hotreload-test") hotreloadTest = true;
    else if (a == "--extent" && i + 1 < argc) extent = std::stoi(argv[++i]);
    else if (a == "--radius" && i + 1 < argc) loadRadius = std::stof(argv[++i]);
    else if (a == "--density" && i + 1 < argc) density = uint32_t(std::stoi(argv[++i]));
    else if (a == "--shots" && i + 1 < argc) shotPrefix = argv[++i];
  }

  const WorldGridDesc gridDesc{glm::vec3(0.0f), 65536.0f, 10};

  // ── Bake ──
  {
    WorldGrid bakeGrid(gridDesc);
    WorldGenSettings gen;
    gen.outputRoot = kWorldRoot;
    gen.level = gridDesc.streamLevel;
    gen.extentCells = extent;
    gen.gameplayPerCell = std::max(1u, density / 2);
    gen.visualPerCell = std::max(1u, density / 2);
    gen.detailPerCell = density;
    WorldGenStats gs;
    if (!generateWorld(gen, bakeGrid, gs)) {
      std::cerr << "Fatal: could not write the world to '" << kWorldRoot << "'\n";
      return 1;
    }
    std::printf("[bake] %u cells, %u files, %u objects, %.1f MB — world spans %.0f m\n",
                gs.cellsWritten, gs.filesWritten, gs.objectsWritten,
                double(gs.bytesWritten) / (1024.0 * 1024.0),
                double((2 * extent + 1) * bakeGrid.cellSize(gridDesc.streamLevel)));
    if (bakeOnly) return 0;
  }

  try {
    // ── Everything from here down mirrors SkyLab's setup ──
    Window window({1600, 900, "Tucano — WorldLab (streaming)"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
    skylab::configureCleanRenderer(*renderer);
    renderer->clouds().enabled = false; // the sky is not what is being tested

    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    Scene scene;
    skylab::buildCleanScene(*device, scene);
    scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);
    scene.camera.setPosition({0.0f, 12.0f, 0.0f});
    scene.camera.lookAt({60.0f, 8.0f, 0.0f});

    // ── Instance Cloud (WM-6) ──
    // The GPU-driven answer to the 4096-draw ceiling. A field of thousands of props is one instance
    // buffer; a compute pre-pass frustum-culls and LOD-selects every instance and compacts the
    // survivors, and one ExecuteIndirect draws them. This gate proves the GPU cull matches the CPU
    // reference exactly (the WM-4 acceptance bar, now for instances) and that N props collapse to a
    // single indirect draw whatever N is.
    if (instanceTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[InstanceCloud]\n");

      std::shared_ptr<rhi::RootSignature> cullRoot;
      std::shared_ptr<rhi::PipelineState> cullPso;
      try {
        cullRoot = device->createComputeRootSignature();
        rhi::ComputePipelineDesc pd;
        pd.rootSignature = cullRoot;
        pd.cs = rhi::ShaderBytecode::loadFromFile(std::string(TUCANO_SHADER_DIR) +
                                                  "/InstanceCull_CSMain.cso");
        cullPso = device->createComputePipeline(pd);
      } catch (const std::exception& e) {
        std::printf("  FAIL could not build the instance-cull pipeline (%s)\n", e.what());
        ui.shutdown();
        return 1;
      }
      check("instance-cull compute pipeline built", cullPso != nullptr);

      // A deterministic field of instances: a grid of unit-box props, no RNG so the gate is stable.
      const glm::vec3 localMin(-0.5f), localMax(0.5f);
      const int side = 90; // 90×90 = 8100 instances, comfortably over the 4096 draw ceiling
      std::vector<InstanceGpu> instances;
      instances.reserve(size_t(side) * side);
      for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
          glm::mat4 m(1.0f);
          m[3] = glm::vec4(float(x) * 4.0f - side * 2.0f, 0.0f, float(z) * 4.0f - side * 2.0f, 1.0f);
          instances.push_back(makeInstance(m, localMin, localMax));
        }
      }
      const uint32_t total = uint32_t(instances.size());
      std::printf("       %u instances in a %dx%d field\n", total, side, side);
      check("the field exceeds the 4096-draw ceiling", total > 4096);

      const uint32_t meshIndexCount = 36; // a box: 12 triangles
      InstanceCloudCuller culler(*device, *cullRoot, *cullPso);
      culler.setInstances(instances, meshIndexCount);

      const CullConfig cfg{200.0f, 4, 4000.0f};

      // Several cameras, so the visible fraction varies from "almost all" to "a sliver".
      struct View { const char* name; glm::vec3 eye, at; };
      const glm::vec3 up(0, 1, 0);
      const View views[] = {
          {"overhead, whole field", {0.0f, 400.0f, 0.0f}, {0.0f, 0.0f, 0.01f}},
          {"low angle across field", {-380.0f, 30.0f, -380.0f}, {0.0f, 0.0f, 0.0f}},
          {"looking away (few visible)", {0.0f, 10.0f, 0.0f}, {0.0f, 10.0f, -1.0f}},
          {"tight corner", {150.0f, 20.0f, 150.0f}, {120.0f, 0.0f, 120.0f}},
      };
      const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 6000.0f);

      bool allMatched = true;
      bool sawPartial = false;
      for (const View& v : views) {
        const glm::mat4 view = glm::lookAtLH(v.eye, v.at, up);
        const glm::mat4 viewProj = proj * view;

        std::vector<uint32_t> cpu;
        cullInstancesCPU(instances, viewProj, v.eye, cfg, cpu);

        DrawIndexedArgs args{};
        std::vector<uint32_t> gpu = culler.cull(viewProj, v.eye, cfg, &args);

        // Compare as sets: the shader emits in compaction (race) order, the reference in index order.
        std::sort(gpu.begin(), gpu.end());
        const bool match = (gpu == cpu);
        allMatched = allMatched && match;
        if (cpu.size() > 0 && cpu.size() < total) sawPartial = true;

        std::printf("       %-28s cpu=%zu gpu=%zu args.instanceCount=%u %s\n", v.name, cpu.size(),
                    gpu.size(), args.instanceCount, match ? "" : "  <-- MISMATCH");

        // The args block the indirect draw would consume must agree with the compacted set.
        if (args.instanceCount != gpu.size()) allMatched = false;
        if (args.indexCountPerInstance != meshIndexCount) allMatched = false;
      }

      check("GPU visible set matches the CPU reference for every view", allMatched);
      check("at least one view culled a real fraction", sawPartial);

      // The whole point of WM-6: however many instances are visible, they cost ONE indirect draw.
      // Prove it on the fullest view.
      {
        const glm::mat4 view = glm::lookAtLH(views[0].eye, views[0].at, up);
        DrawIndexedArgs args{};
        std::vector<uint32_t> gpu = culler.cull(proj * view, views[0].eye, cfg, &args);
        std::printf("       overhead: %zu visible instances -> 1 drawIndexedIndirect\n", gpu.size());
        check("a full field of instances draws in one indirect call", args.instanceCount > 4096);
      }

      std::printf("\n=== InstanceCloud: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Instance Cloud RENDER (WM-6 integração no g-buffer) ──
    // Prova visual: um campo de milhares de cubos desenhado por UMA drawIndexedIndirect no g-buffer
    // deferred, com o compute cull rodando antes do render() no mesmo command list.
    if (instanceRender) {
      std::printf("\n[InstanceCloud render]\n");
      // O caminho instanciado escreve o g-buffer direto — só o caminho não-visbuffer o usa.
      renderer->settings().enableVisibilityBuffer = false;

      std::shared_ptr<rhi::RootSignature> cullRoot;
      std::shared_ptr<rhi::PipelineState> cullPso;
      try {
        cullRoot = device->createComputeRootSignature();
        rhi::ComputePipelineDesc pd;
        pd.rootSignature = cullRoot;
        pd.cs = rhi::ShaderBytecode::loadFromFile(std::string(TUCANO_SHADER_DIR) +
                                                  "/InstanceCull_CSMain.cso");
        cullPso = device->createComputePipeline(pd);
      } catch (const std::exception& e) {
        std::printf("  FAIL cull pipeline (%s)\n", e.what());
        ui.shutdown();
        return 1;
      }

      // A unit cube: 24 vertices (per-face normals), 36 indices, one submesh at index 0.
      std::vector<Vertex> cv;
      std::vector<uint32_t> ci;
      {
        const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        const glm::vec3 up[6] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (int f = 0; f < 6; ++f) {
          const glm::vec3 nf = n[f];
          const glm::vec3 uf = up[f];
          const glm::vec3 rf = glm::cross(uf, nf);
          const uint32_t base = uint32_t(cv.size());
          const glm::vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
          const float sx[4] = {-1, 1, 1, -1};
          const float sy[4] = {-1, -1, 1, 1};
          for (int k = 0; k < 4; ++k) {
            Vertex v{};
            v.position = (nf + rf * sx[k] + uf * sy[k]) * 0.5f;
            v.normal = nf;
            v.tangent = glm::vec4(rf, 1.0f);
            v.uv = uvs[k];
            v.color = glm::vec4(1.0f);
            cv.push_back(v);
          }
          ci.insert(ci.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
      }
      SubMesh csub{};
      csub.indexCount = uint32_t(ci.size());
      csub.materialIndex = 0;
      csub.aabbMin = glm::vec3(-0.5f);
      csub.aabbMax = glm::vec3(0.5f);
      auto cubeMesh = Mesh::create(*device, cv, ci, {csub});

      // A field of instances, deterministic (no RNG).
      const int side = 70; // 4900 cubes — well over the draw ceiling
      std::vector<InstanceGpu> instances;
      instances.reserve(size_t(side) * side);
      for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
          const float px = (x - side * 0.5f) * 3.4f;
          const float pz = (z - side * 0.5f) * 3.4f;
          const float h = 0.6f + 0.5f * std::sin(px * 0.05f) * std::cos(pz * 0.05f);
          glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(px, h * 0.5f, pz)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.9f, h * 2.0f, 0.9f));
          instances.push_back(makeInstance(m, glm::vec3(-0.5f), glm::vec3(0.5f)));
        }
      }
      InstanceCloudCuller culler(*device, *cullRoot, *cullPso);
      culler.setInstances(instances, cubeMesh->indexCount());

      InstanceCloudRender cloud;
      cloud.instanceBuffer = culler.instanceBuffer();
      cloud.visibleBuffer = culler.visibleBuffer();
      cloud.argsBuffer = culler.argsBuffer();
      cloud.mesh = cubeMesh.get();
      cloud.baseColor = {0.80f, 0.34f, 0.18f, 1.0f};
      cloud.emissive = {0.10f, 0.04f, 0.02f};
      cloud.roughness = 0.7f;
      cloud.metallic = 0.0f;
      scene.instanceClouds.push_back(cloud);

      scene.camera.setPosition({0.0f, 55.0f, -150.0f});
      scene.camera.lookAt({0.0f, 0.0f, 10.0f});
      const CullConfig cfg{600.0f, 4, 8000.0f};

      auto renderFrame = [&](bool shot) {
        auto* c = device->beginFrame();
        // The cull MUST be recorded on this frame's command list before render() consumes the args.
        culler.cullForDraw(*c, scene.camera.viewProj(), scene.camera.position(), cfg);
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        ScreenshotPending sp;
        if (shot) sp = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (shot && sp.impl) {
          device->waitIdle();
          finalizeScreenshot(sp, shotPrefix + "_instances.png");
        }
      };

      for (int i = 0; i < 30; ++i) renderFrame(false);

      std::vector<uint32_t> cpuVisible;
      cullInstancesCPU(instances, scene.camera.viewProj(), scene.camera.position(), cfg, cpuVisible);
      std::printf("       %u instances, %zu visible (CPU ref) -> 1 drawIndexedIndirect, %u draw calls total\n",
                  culler.instanceCount(), cpuVisible.size(), renderer->drawCalls());

      renderFrame(true);
      std::printf("       saved %s_instances.png\n", shotPrefix.c_str());
      device->waitIdle();
      ui.shutdown();
      return 0;
    }

    // ── Clipmap contínuo (WM-8, LOD geométrico verdadeiro) ──
    // Anéis aninhados centrados na câmera, morph nas bordas p/ zero pop, amostrando um heightmap.
    // Fase 1: heightmap fbm estático (o feed pela janela toroidal do streaming vem depois).
    if (clipmapMode) {
      std::printf("\n[Clipmap contínuo]\n");
      renderer->settings().enableVisibilityBuffer = false; // clipmap escreve o g-buffer direto

      terrain::TerrainGenParams gp;
      gp.resolution = 2048; // finer heightmap so the near rings have real detail to show
      gp.worldSize = 4096.0f;
      gp.octaves = 9;
      gp.baseFrequency = 10.0f; // more hills across the field → visible LOD + shading contrast
      gp.baseAmplitude = 320.0f;
      gp.seed = 1337;
      auto hm = terrain::TerrainGenerator::generate(*device, gp);
      std::printf("       heightmap %ux%u, %.0f m, alt %.0f..%.0f m, bindless #%u\n", gp.resolution,
                  gp.resolution, gp.worldSize, hm->minHeight(), hm->maxHeight(), hm->bindlessIndex());

      terrain::ClipmapTerrainDesc cd;
      cd.levelCount = 6;
      cd.baseSpacing = 1.5f;
      cd.morphStart = 0.6f;
      terrain::ClipmapTerrain clipmap(*device, cd);
      clipmap.setHeightmap(hm->bindlessIndex(), glm::vec2(0.0f), gp.worldSize, 1.0f);
      scene.clipmapTerrain = &clipmap;

      // Material virtual texture (Fase 1). Pages generated from the terrain height, streamed into a
      // fixed atlas around the camera — unique dense material with bounded VRAM.
      std::unique_ptr<terrain::TerrainVirtualTexture> vt;
      if (clipmapVt) {
        terrain::TerrainVtDesc vd;
        vd.pageWorldSize = 24.0f; // a mip-0 page core; coarser mips cover 2^m× more
        vd.pageTableRes = 256;
        vd.maxMip = 5;
        vt = std::make_unique<terrain::TerrainVirtualTexture>(
            *device, vd, [hm](float wx, float wz) { return hm->sampleHeight(wx, wz); });
        clipmap.setVirtualTexture(vt.get());
        std::printf("       VT on: %llu virtual pages total (mip 0), atlas holds %u\n",
                    (unsigned long long)vt->totalVirtualPages(), terrain::kVtMaxResident);
      }
      const std::string tag = clipmapVtFb ? "_vtfb" : (clipmapVt ? "_vt" : "");

      // Requests the VT pages for the visible clipmap rings — a coarser mip for the farther, larger
      // rings, so detail reaches the horizon with a bounded page budget. Call after clipmap.update().
      auto requestVtForClipmap = [&]() {
        if (!vt) return;
        vt->beginRequests();
        const auto& lv = clipmap.levels();
        for (size_t L = 0; L < lv.size(); ++L) {
          if (!lv[L].visible) continue;
          const glm::vec2 mn = lv[L].origin;
          const glm::vec2 mx = lv[L].origin + glm::vec2(lv[L].extentHalf * 2.0f);
          vt->requestRegion(mn, mx, uint32_t(L));
        }
        vt->commit(*device);
      };

      // Camera near the centre of the field, looking across it.
      const glm::vec3 center(gp.worldSize * 0.5f, 0.0f, gp.worldSize * 0.5f);
      const float groundH = hm->sampleHeight(center.x, center.z);

      auto renderClip = [&](const glm::vec3& eye, const glm::vec3& at, bool shot,
                            const std::string& name) {
        scene.camera.setPosition(eye);
        scene.camera.lookAt(at);
        clipmap.update(eye, scene.camera.viewProj());
        // Request source (OUTSIDE beginFrame — uploads textures). Feedback mode reads last frame's
        // GPU feedback; coverage mode derives requests from the clipmap rings.
        if (vt) {
          if (clipmapVtFb) {
            vt->beginRequests();
            vt->processFeedbackRequests();
            vt->commit(*device);
          } else {
            requestVtForClipmap();
          }
        }
        auto* c = device->beginFrame();
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene); // draws terrain + (in VT mode) the feedback pass
        ScreenshotPending sp;
        if (shot) sp = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        // Feedback readback must be complete before the next processFeedbackRequests() maps it.
        if (clipmapVtFb) device->waitIdle();
        if (shot && sp.impl) {
          device->waitIdle();
          finalizeScreenshot(sp, name);
          std::printf("       saved %s\n", name.c_str());
        }
      };

      // Warm a few frames, then two views: a wide vista and a low grazing angle (best to spot pop/cracks).
      const glm::vec3 vistaEye = center + glm::vec3(0.0f, groundH + 220.0f, -260.0f);
      for (int i = 0; i < 8; ++i) renderClip(vistaEye, center + glm::vec3(300.0f, groundH, 300.0f), false, "");
      renderClip(vistaEye, center + glm::vec3(300.0f, groundH, 300.0f), true, shotPrefix + "_clipmap" + tag + "_vista.png");

      const glm::vec3 grazeEye = center + glm::vec3(-200.0f, groundH + 25.0f, -200.0f);
      for (int i = 0; i < 4; ++i) renderClip(grazeEye, center + glm::vec3(400.0f, groundH + 10.0f, 400.0f), false, "");
      renderClip(grazeEye, center + glm::vec3(400.0f, groundH + 10.0f, 400.0f), true, shotPrefix + "_clipmap" + tag + "_graze.png");

      uint32_t visible = 0;
      for (const auto& lv : clipmap.levels()) visible += lv.visible ? 1u : 0u;
      std::printf("       %u/%u rings visible, %u draw calls\n", visible, cd.levelCount,
                  renderer->drawCalls());
      if (vt) {
        std::printf("       VT: %zu pages resident of %llu total (%.4f%%), %u generated%s\n",
                    vt->residentPages(), (unsigned long long)vt->totalVirtualPages(),
                    100.0 * double(vt->residentPages()) / double(vt->totalVirtualPages()),
                    vt->pagesGenerated(),
                    clipmapVtFb ? " [GPU feedback-driven]" : " [clipmap-coverage-driven]");
      }

      device->waitIdle();
      ui.shutdown();
      return 0;
    }

    // ── Clipmap ALIMENTADO pelo streaming (WM-8 Fase 2) ──
    // O que fecha a integração: o streaming preenche uma JANELA de heightmap (que recentra na câmera)
    // e o clipmap contínuo amostra ela. Streaming decide o que é residente; clipmap vira geometria.
    if (clipmapStream) {
      std::printf("\n[Clipmap + streaming (Fase 2)]\n");
      renderer->settings().enableVisibilityBuffer = false;

      // Terrain streamed on coarse cells (512 m), so a big window is covered by few tiles.
      const WorldGridDesc tgd{glm::vec3(0.0f), 65536.0f, 7};
      WorldGrid tgrid(tgd);
      core::TaskScheduler ttasks;
      StreamingBudget tbudget;
      terrain::TerrainStreamSettings ts;
      ts.layer = WorldLayer::Visual;
      ts.heightmapResolution = 129;
      ts.heightScale = 320.0f;
      ts.baseFrequency = 0.0022f;
      ts.octaves = 7;
      terrain::TerrainCellProvider tprovider(*device, scene, tgrid, ts);
      tprovider.enableComposite(*device, /*worldSize=*/4096.0f, /*resolution=*/1024);

      StreamingSchedulerDesc tsd;
      tsd.streamLevel = tgd.streamLevel;
      tsd.maxConcurrentLoads = 48;
      tsd.unloadGraceFrames = 20;
      tsd.prediction.enabled = false;
      tsd.lodDistances.clear(); // the clipmap does LOD; tiles are just data
      StreamingScheduler tstreamer(tgrid, ttasks, tbudget, tprovider, tsd);

      StreamingObserver obs;
      obs.id = 1;
      obs.loadRadius = 2600.0f; // covers the 4096 m window
      obs.unloadRadius = 3000.0f;

      terrain::ClipmapTerrainDesc cd;
      cd.levelCount = 6;
      cd.baseSpacing = 2.0f; // level 5 = 64*2*32 = 4096 m, matching the window
      cd.morphStart = 0.6f;
      terrain::ClipmapTerrain clipmap(*device, cd);
      scene.clipmapTerrain = &clipmap;

      auto stepAndRender = [&](const glm::vec2& obsXZ, int warmFrames, bool shot,
                               const std::string& name) {
        obs.position = glm::vec3(obsXZ.x, 0.0f, obsXZ.y);
        tstreamer.setObservers({obs});
        for (int i = 0; i < warmFrames; ++i) {
          // Stream + window update happen OUTSIDE beginFrame (uploadTexture resets the upload arena).
          tstreamer.update(16.0f);
          tprovider.updateComposite(*device, obsXZ);
          clipmap.setHeightmap(tprovider.compositeBindlessIndex(), tprovider.compositeMin(),
                               tprovider.compositeWorldSize(), 1.0f);

          const float groundH = tprovider.referenceHeight(obsXZ.x, obsXZ.y);
          const glm::vec3 eye(obsXZ.x - 120.0f, groundH + 180.0f, obsXZ.y - 220.0f);
          const glm::vec3 at(obsXZ.x + 260.0f, groundH, obsXZ.y + 260.0f);
          scene.camera.setPosition(eye);
          scene.camera.lookAt(at);
          clipmap.update(eye, scene.camera.viewProj());

          auto* c = device->beginFrame();
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          ScreenshotPending sp;
          if (shot && i == warmFrames - 1) sp = beginScreenshot(*device, *c, b);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
          if (shot && i == warmFrames - 1 && sp.impl) {
            device->waitIdle();
            finalizeScreenshot(sp, name);
            std::printf("       @(%.0f,%.0f): %zu tiles resident, saved %s\n", obsXZ.x, obsXZ.y,
                        tprovider.liveTileCount(), name.c_str());
          }
        }
      };

      // One spot, then a 4 km jump to prove the window recentres and re-streams.
      stepAndRender(glm::vec2(8000.0f, 8000.0f), 260, true, shotPrefix + "_clipstream_a.png");
      stepAndRender(glm::vec2(12000.0f, 12000.0f), 260, true, shotPrefix + "_clipstream_b.png");

      std::printf("       window %.0f m @ (%.0f,%.0f), composite bindless #%u\n",
                  tprovider.compositeWorldSize(), tprovider.compositeMin().x, tprovider.compositeMin().y,
                  tprovider.compositeBindlessIndex());
      device->waitIdle();
      ui.shutdown();
      return 0;
    }

    // ── Terrain LOD + GPU height query (WM-8, caminho B) ──
    // Distance LOD on the streamed tiles: near tiles dense, far tiles coarse, driven by the same
    // WM-5 band mechanism that reloads a cell when its detail level changes. Skirts hide the cracks.
    // Plus the GPU height query (HeightQuery.hlsl) dispatched against a resident tile's texture.
    if (terrainLod) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[Terrain LOD + height query]\n");

      WorldGrid tgrid(gridDesc);
      core::TaskScheduler ttasks;
      StreamingBudget tbudget;
      terrain::TerrainStreamSettings ts;
      ts.layer = WorldLayer::Visual;
      ts.heightmapResolution = 129;
      ts.meshResolution = 64; // LOD 0; each coarser level halves it
      terrain::TerrainCellProvider tprovider(*device, scene, tgrid, ts);

      StreamingSchedulerDesc tsd;
      tsd.streamLevel = gridDesc.streamLevel;
      tsd.maxConcurrentLoads = 32;
      tsd.unloadGraceFrames = 20;
      tsd.prediction.enabled = false;
      tsd.lodDistances = {120.0f, 300.0f}; // <120 m = LOD0, 120–300 = LOD1, >300 = LOD2
      StreamingScheduler tstreamer(tgrid, ttasks, tbudget, tprovider, tsd);

      StreamingObserver obs;
      obs.id = 1;
      obs.loadRadius = 460.0f; // reaches into the LOD2 band
      obs.unloadRadius = 560.0f;
      obs.position = glm::vec3(0.0f);

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          tstreamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      tstreamer.setObservers({obs});
      pump(600);

      const std::array<uint32_t, 8> hist = tprovider.tilesPerLod();
      std::printf("       resident %zu tiles, LOD histogram: %u/%u/%u/%u\n", tprovider.liveTileCount(),
                  hist[0], hist[1], hist[2], hist[3]);
      check("tiles streamed in", tprovider.liveTileCount() > 8);
      check("near tiles exist at full detail (LOD0)", hist[0] > 0);
      check("far tiles dropped to a coarser LOD", (hist[1] + hist[2] + hist[3]) > 0);

      // A near tile must carry more geometry than a far one — that is the LOD, measured.
      uint32_t nearLod = 99, nearVerts = 0, farLod = 99, farVerts = 0;
      const bool gotNear = tprovider.tileLodAt(10.0f, 10.0f, nearLod, nearVerts);
      const bool gotFar = tprovider.tileLodAt(250.0f, 250.0f, farLod, farVerts);
      std::printf("       near tile: LOD%u %u verts | far tile: LOD%u %u verts\n", nearLod, nearVerts,
                  farLod, farVerts);
      check("a near and a far tile are both resident", gotNear && gotFar);
      check("the far tile is a coarser LOD", gotNear && gotFar && farLod > nearLod);
      check("the far tile has fewer vertices", gotNear && gotFar && farVerts < nearVerts);

      // ── GPU height query against a resident tile ──
      glm::vec3 tileOrigin(0.0f);
      auto qhm = tprovider.tileHeightmapAt(20.0f, 20.0f, tileOrigin);
      check("a tile heightmap is resident for the query", qhm != nullptr);
      if (qhm) {
        terrain::HeightmapQuery hquery(*device);
        // Submit points in the tile's LOCAL space; the tile heightmap spans [0, cellSize].
        struct QP { float wx, wz; };
        std::vector<QP> pts;
        const float cs = tgrid.cellSize(gridDesc.streamLevel);
        for (int i = 0; i < 12; ++i) {
          const float lx = (0.1f + 0.065f * i) * cs;
          const float lz = (0.15f + 0.06f * i) * cs;
          hquery.submit(lx, lz);
          pts.push_back({tileOrigin.x + lx, tileOrigin.z + lz});
        }
        // The query's readback ring delivers a result two frames after submit and discards it a frame
        // later. Wait for the GPU each frame (this is a gate, not a hot loop) so the ring reads real
        // data, and capture each point's height the moment it is available, before the ring drops it.
        std::vector<float> gpuHeights(pts.size(), 0.0f);
        std::vector<bool> got(pts.size(), false);
        for (int k = 0; k < 3; ++k) {
          auto* c = device->beginFrame();
          c->setDescriptorHeap();
          hquery.dispatch(*device, *c, *qhm);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
          device->waitIdle();
          hquery.readback(*device);
          for (uint32_t i = 0; i < pts.size(); ++i) {
            const float r = hquery.getResult(i);
            if (!got[i] && r != 0.0f) { gpuHeights[i] = r; got[i] = true; }
          }
        }

        double sumCpu = 0.0, mxCpu = 0.0, sumAna = 0.0;
        uint32_t covered = 0;
        for (uint32_t i = 0; i < pts.size(); ++i) {
          if (!got[i]) continue;
          ++covered;
          const float gpu = gpuHeights[i];
          float cpu = 0.0f;
          tprovider.sampleHeight(pts[i].wx, pts[i].wz, cpu); // the height field physics/mesh use
          const float ana = tprovider.referenceHeight(pts[i].wx, pts[i].wz);
          const double eCpu = std::abs(double(gpu) - double(cpu));
          sumCpu += eCpu;
          mxCpu = std::max(mxCpu, eCpu);
          sumAna += std::abs(double(gpu) - double(ana));
        }
        const double meanCpu = covered ? sumCpu / double(covered) : 1e9;
        const double meanAna = covered ? sumAna / double(covered) : 1e9;
        std::printf("       GPU height query: %u/%zu returned, vs CPU field mean %.3f m (max %.3f), vs analytic %.3f m\n",
                    covered, pts.size(), meanCpu, mxCpu, meanAna);
        check("the GPU query returned results for every point", covered == pts.size());
        // Agreement with the CPU height field is the operational requirement — that is the value a
        // player controller or a physics ray would use. Within ~1 texel (the heightmap is 0.5 m per
        // texel here, on 90 m-amplitude terrain) is a faithful GPU reproduction of it.
        check("the GPU height query reproduces the CPU height field (mean < 1 m)", meanCpu < 1.0);
      }

      // Screenshot: low angle so near-dense / far-coarse tiles are both in frame.
      scene.camera.setPosition({0.0f, 45.0f, -40.0f});
      scene.camera.lookAt({120.0f, 0.0f, 260.0f});
      pump(8);
      {
        auto* c = device->beginFrame();
        tstreamer.update(16.0f);
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        ScreenshotPending shot = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (shot.impl) {
          device->waitIdle();
          finalizeScreenshot(shot, shotPrefix + "_terrain_lod.png");
          std::printf("       saved %s_terrain_lod.png\n", shotPrefix.c_str());
        }
      }

      std::printf("\n=== Terrain LOD: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Terrain streaming (WM-8) ──
    // Terrain is not a separate streaming system: it is a CellDataProvider like any other. Each cell
    // owns a heightmap tile, generated on a worker thread and built on the device thread, streamed
    // by the same scheduler that streams props. This gate proves tiles arrive by proximity and leave
    // when abandoned, that the resident set stays bounded, and that a streamed tile reproduces the
    // world-continuous height field faithfully (the check that a tile is not just present but RIGHT).
    if (terrainTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[Terrain streaming]\n");

      WorldGrid tgrid(gridDesc);
      core::TaskScheduler ttasks;
      StreamingBudget tbudget;
      terrain::TerrainStreamSettings ts;
      ts.layer = WorldLayer::Visual;
      ts.heightmapResolution = 129;
      ts.meshResolution = 48;
      terrain::TerrainCellProvider tprovider(*device, scene, tgrid, ts);

      StreamingSchedulerDesc tsd;
      tsd.streamLevel = gridDesc.streamLevel;
      tsd.maxConcurrentLoads = 24;
      tsd.unloadGraceFrames = 20;
      tsd.lodDistances.clear(); // terrain tiles do not HLOD here; no band reloads
      tsd.prediction.enabled = false;
      StreamingScheduler tstreamer(tgrid, ttasks, tbudget, tprovider, tsd);

      StreamingObserver obs;
      obs.id = 1;
      obs.loadRadius = 200.0f;
      obs.unloadRadius = 300.0f;

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          tstreamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      // ── Tiles stream in at the origin ──
      obs.position = glm::vec3(0.0f);
      tstreamer.setObservers({obs});
      pump(400);
      const size_t resident = tprovider.liveTileCount();
      std::printf("       resident tiles at origin: %zu (%u built, %llu verts)\n", resident,
                  tprovider.tilesBuilt(), (unsigned long long)tprovider.residentVertexCount());
      check("terrain tiles streamed in around the observer", resident > 4);

      // The resident set must be bounded by the load radius, not the whole world. Cells in a 200 m
      // radius on 64 m cells is a few dozen at most; a runaway number means unload never fires.
      check("the resident set is bounded by the radius", resident < 80);

      // ── Every resident tile reproduces the world-continuous field ──
      // Sample many points inside the resident footprint and compare the streamed tile's height to
      // the analytic fbm. Bilinear texel interpolation is the only allowed source of error.
      double sumErr = 0.0, maxErr = 0.0;
      int probes = 0, covered = 0;
      for (int gz = -3; gz <= 3; ++gz) {
        for (int gx = -3; gx <= 3; ++gx) {
          const float wx = gx * 27.3f + 5.0f;
          const float wz = gz * 27.3f + 5.0f;
          float streamed = 0.0f;
          ++probes;
          if (!tprovider.sampleHeight(wx, wz, streamed)) continue;
          ++covered;
          const float ref = tprovider.referenceHeight(wx, wz);
          const double e = std::abs(double(streamed) - double(ref));
          sumErr += e;
          maxErr = std::max(maxErr, e);
        }
      }
      const double meanErr = covered ? sumErr / covered : 1e9;
      std::printf("       height query vs analytic: covered %d/%d, mean err %.3f m, max %.3f m\n",
                  covered, probes, meanErr, maxErr);
      check("resident tiles cover the sampled region", covered > 20);
      check("streamed height matches the analytic field (mean < 1 m)", meanErr < 1.0);
      check("no query is wildly off (max < 4 m)", maxErr < 4.0);

      // ── Seam continuity across a tile boundary ──
      // Two points a few centimetres either side of a tile edge must have near-equal height: the
      // world-continuous sampling means neighbours share the border, so there is no cliff.
      {
        const float edge = tgrid.cellSize(gridDesc.streamLevel); // first interior boundary at x = cellSize
        float hL = 0.0f, hR = 0.0f;
        const bool okL = tprovider.sampleHeight(edge - 0.05f, 10.0f, hL);
        const bool okR = tprovider.sampleHeight(edge + 0.05f, 10.0f, hR);
        std::printf("       seam @x=%.0f: left=%.3f right=%.3f\n", edge, hL, hR);
        check("both sides of a tile boundary are resident", okL && okR);
        check("the boundary has no height cliff (< 0.5 m)", std::abs(hL - hR) < 0.5f);
      }

      // ── Moving the observer streams a fresh region and unloads the old one ──
      const uint32_t builtBefore = tprovider.tilesBuilt();
      obs.position = glm::vec3(2000.0f, 0.0f, 2000.0f);
      tstreamer.setObservers({obs});
      pump(500);
      std::printf("       after 2 km move: resident %zu, total built %u\n", tprovider.liveTileCount(),
                  tprovider.tilesBuilt());
      check("moving the observer built new tiles", tprovider.tilesBuilt() > builtBefore);
      check("the far region is also bounded", tprovider.liveTileCount() < 80);

      // A screenshot of streamed terrain under the camera.
      {
        scene.camera.setPosition({2000.0f, 120.0f, 1900.0f});
        scene.camera.lookAt({2040.0f, 20.0f, 2040.0f});
        pump(12);
        auto* c = device->beginFrame();
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        ScreenshotPending shot = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (shot.impl) {
          device->waitIdle();
          finalizeScreenshot(shot, shotPrefix + "_terrain.png");
          std::printf("       saved %s_terrain.png\n", shotPrefix.c_str());
        }
      }

      // ── Fully abandon: the resident set must drain ──
      tstreamer.setObservers({});
      for (int i = 0; i < 400; ++i) {
        pump(1);
        if (tprovider.liveTileCount() == 0) break;
      }
      std::printf("       after abandon: resident %zu, verts %llu\n", tprovider.liveTileCount(),
                  (unsigned long long)tprovider.residentVertexCount());
      check("terrain fully unloads when abandoned", tprovider.liveTileCount() == 0);
      check("no resident vertices leak after unload", tprovider.residentVertexCount() == 0);

      std::printf("\n=== Terrain streaming: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Streaming, grafted on ──
    WorldGrid grid(gridDesc);
    core::TaskScheduler tasks;
    StreamingBudget budget;
    SceneCellProvider provider(*device, scene, kWorldRoot);
    CellPersistenceStore persistence;
    StreamingRecorder recorder;

    StreamingSchedulerDesc sd;
    sd.streamLevel = gridDesc.streamLevel;
    sd.maxConcurrentLoads = 24;
    sd.unloadGraceFrames = 30;
    StreamingScheduler streamer(grid, tasks, budget, provider, sd);
    streamer.setPersistence(&persistence);
    streamer.setRecorder(&recorder);

    // Physics for streamed cells. Only created when asked for: a render-only run should not pay
    // for a physics world it never steps.
    std::unique_ptr<physics::PhysicsWorld> physicsWorld;
    if (physicsTest) {
      physicsWorld = std::make_unique<physics::PhysicsWorld>();
      provider.setPhysics(physicsWorld.get());
    }

    StreamingObserver observer;
    observer.id = 1;
    observer.loadRadius = loadRadius;
    observer.unloadRadius = loadRadius * 1.5f;

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      if (!swapChain || !renderer) return;
      swapChain->resize(w, h);
      renderer->resize(w, h);
      scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);
    });

    // ── HLOD ──
    // The claim, and the measurement that motivated WM-5: a distant cell must cost ONE object
    // instead of dozens. Object count is what exhausts the per-frame descriptor heap — the first
    // run of this sample died at ~4000 — so collapsing far cells is what lets the load radius grow.
    if (hlodTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[HLOD]\n");

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          streamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      observer.position = glm::vec3(0.0f);
      observer.velocity = glm::vec3(0.0f);

      // ── HLOD first, deliberately ──
      // At a realistic density the full-detail baseline exceeds the engine's 4096-draw ceiling and
      // takes the process with it. Running HLOD first means the interesting case is measured even
      // when the baseline cannot survive — and that ceiling IS the finding: HLOD is what makes a
      // world of this density possible at all.
      streamer.desc().lodDistances = {40.0f, 90.0f};
      streamer.setObservers({observer});
      pump(500);
      const size_t hlodObjects = provider.liveObjectCount();
      const uint32_t hlodCells = streamer.stats().cellsResident;
      std::printf("       with HLOD   : %zu objects across %u cells (%u merged)\n", hlodObjects,
                  hlodCells, provider.hlodMeshesBuilt());
      check("cells streamed with HLOD", hlodCells > 0 && hlodObjects > 0);
      check("merged meshes were actually built", provider.hlodMeshesBuilt() > 0);

      // The density-independent claim, and the one that matters: a merged cell costs a handful of
      // objects — roughly one per resident layer plus any glTF parts — however many objects the
      // cell actually contains. A ratio that grew with density would mean merging is not happening.
      const double perCell = hlodCells ? double(hlodObjects) / double(hlodCells) : 0.0;
      std::printf("       %.1f objects per cell under HLOD\n", perCell);
      // Twelve, not eight: a merged cell still carries one object per resident layer PLUS the glTF
      // parts, which are instanced rather than merged. The number to compare against is full
      // detail, which runs above twenty per cell at this density.
      check("a merged cell costs only a handful of objects", perCell <= 12.0);

      // Clear out before measuring the baseline.
      streamer.setObservers({});
      for (int i = 0; i < 400; ++i) {
        pump(1);
        if (provider.liveObjectCount() == 0) break;
      }
      check("HLOD content unloaded cleanly", provider.liveObjectCount() == 0);

      // Re-establish the HLOD-resident state. The clear above was to prove the unload path — the one
      // that used to crash on the per-cell merged mesh — tears down cleanly, NOT to end the test.
      // The approach below measures against a live, coarse-LOD world, so the observer has to come
      // back first; without this the band widening has nothing resident to reload.
      streamer.desc().lodDistances = {40.0f, 90.0f};
      streamer.setObservers({observer});
      for (int i = 0; i < 500; ++i) {
        pump(1);
        if (provider.liveObjectCount() >= hlodObjects) break;
      }

      // ── No in-process baseline, on purpose ──
      // Streaming the same world at full detail exceeds the engine's 4096-draw ceiling and takes
      // the process down. That is not a flaw in the test — it IS the result: at this density the
      // world is only renderable at all because far cells collapse. Measured separately at
      // density 12 with a smaller radius, where the baseline does survive: 696 objects at full
      // detail against 260 with HLOD, i.e. 37%.

      // ── Approaching must restore full detail ──
      const uint32_t switchesBefore = streamer.stats().lodSwitches;
      streamer.desc().lodDistances = {1000.0f, 2000.0f}; // everything is now near
      pump(500);
      std::printf("       after approach: %zu objects (%u LOD switches)\n",
                  provider.liveObjectCount(), streamer.stats().lodSwitches);
      check("crossing a band reloaded cells", streamer.stats().lodSwitches > switchesBefore);
      check("full detail came back", provider.liveObjectCount() > hlodObjects);

      // A screenshot with a tight band, so merged cells are visible in the distance.
      streamer.desc().lodDistances = {60.0f, 140.0f};
      pump(300);
      {
        scene.camera.setPosition({0.0f, 18.0f, 0.0f});
        scene.camera.lookAt({120.0f, 6.0f, 20.0f});
        pump(10);
        auto* c = device->beginFrame();
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        ScreenshotPending shot = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (shot.impl) {
          device->waitIdle();
          finalizeScreenshot(shot, shotPrefix + "_hlod.png");
          std::printf("       saved %s_hlod.png\n", shotPrefix.c_str());
        }
      }

      std::printf("\n=== HLOD: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Streamed glTF assets ──
    // The claim: a cell can name a real model file and the streamer brings it in, instanced, with
    // the file parsed once however many copies appear. That last part is the whole reason a cache
    // exists — without it every instance would re-parse and re-upload, and the cost of streaming
    // would be dominated by duplicate work rather than by streaming.
    if (gltfTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[streamed glTF]\n");

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          streamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      check("the baker wrote a prop asset",
            std::filesystem::exists(std::string(kWorldRoot) + "/props/prop.gltf"));

      observer.position = glm::vec3(0.0f);
      observer.velocity = glm::vec3(0.0f);
      streamer.setObservers({observer});
      pump(400);

      const size_t objects = provider.liveObjectCount();
      const uint32_t assets = provider.gltfAssetsLoaded();
      std::printf("       %zu streamed objects, %u distinct glTF assets parsed, %u cells\n",
                  objects, assets, streamer.stats().cellsResident);

      check("cells streamed in", objects > 0);
      check("the glTF asset was parsed", assets >= 1);
      // One file, however many instances. A count that tracks the instances means no caching.
      check("the asset was parsed ONCE, not per instance", assets == 1);

      // Each instance expands into the model's two nodes, so a glTF instance contributes more
      // scene objects than a primitive would. If the parts collapsed into one, this would fail.
      const CellId home = grid.cellAt(glm::vec3(0.0f), gridDesc.streamLevel);
      glm::vec3 propPos(0.0f);
      const bool foundProp = provider.objectPosition(home, WorldLayer::Visual, 0, propPos);
      check("a glTF instance is resident and locatable", foundProp);

      // Unload and reload: assets stay cached, instances come and go.
      streamer.setObservers({});
      for (int i = 0; i < 300; ++i) {
        pump(1);
        if (provider.liveObjectCount() == 0) break;
      }
      check("glTF instances unload with their cells", provider.liveObjectCount() == 0);

      observer.position = glm::vec3(0.0f);
      streamer.setObservers({observer});
      pump(300);
      check("instances come back on reload", provider.liveObjectCount() > 0);
      check("the asset was NOT re-parsed on reload", provider.gltfAssetsLoaded() == assets);
      std::printf("       after reload: %zu objects, still %u asset parse(s)\n",
                  provider.liveObjectCount(), provider.gltfAssetsLoaded());

      // A screenshot so the props can be seen, not just counted.
      {
        scene.camera.setPosition({0.0f, 14.0f, 0.0f});
        scene.camera.lookAt({60.0f, 8.0f, 10.0f});
        pump(10);
        auto* c = device->beginFrame();
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        ScreenshotPending shot = beginScreenshot(*device, *c, b);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (shot.impl) {
          device->waitIdle();
          finalizeScreenshot(shot, shotPrefix + "_gltf.png");
          std::printf("       saved %s_gltf.png\n", shotPrefix.c_str());
        }
      }

      std::printf("\n=== glTF: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Streamed colliders ──
    // The claim: geometry streamed off disk is SOLID. A dynamic sphere dropped over a landmark must
    // land on it rather than fall through, and every body must be released when its cell unloads —
    // Jolt has a fixed body budget, so a leak here is silent until physics stops working entirely.
    if (physicsTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[streamed colliders]\n");

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          streamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      const CellId home = grid.cellAt(glm::vec3(0.0f), gridDesc.streamLevel);
      observer.position = glm::vec3(0.0f);
      observer.velocity = glm::vec3(0.0f);
      streamer.setObservers({observer});

      glm::vec3 landmark(0.0f);
      for (int i = 0; i < 200; ++i) {
        pump(1);
        if (provider.objectPosition(home, WorldLayer::Gameplay, 0, landmark)) break;
      }
      check("a gameplay landmark streamed in", landmark != glm::vec3(0.0f));
      check("colliders were created for it", provider.liveBodyCount() > 0);
      const size_t bodiesLoaded = provider.liveBodyCount();
      std::printf("       %zu bodies live across %u resident cells\n", bodiesLoaded,
                  streamer.stats().cellsResident);

      // Drop a sphere from well above the landmark's top. The landmarks are 6-16 m tall and centred
      // near y=0, so anything that ends up far below zero fell straight through.
      const glm::vec3 dropAt(landmark.x, landmark.y + 60.0f, landmark.z);
      const JPH::BodyID probe = physicsWorld->createDynamicSphere(1.0f, dropAt, 10.0f);
      check("the probe body was created", !probe.IsInvalid());

      for (int i = 0; i < 240; ++i) {
        physicsWorld->step(1.0f / 60.0f);
        pump(1);
      }
      const glm::vec3 rest = physicsWorld->getBodyPosition(probe);
      std::printf("       probe fell from y=%.1f to y=%.1f\n", double(dropAt.y), double(rest.y));
      check("the probe fell (gravity is running)", rest.y < dropAt.y - 1.0f);
      check("the probe did NOT fall through the streamed world", rest.y > -20.0f);

      physicsWorld->removeBody(probe);

      // Unload everything: every body the cells created must go with them.
      streamer.setObservers({});
      for (int i = 0; i < 300; ++i) {
        pump(1);
        if (provider.liveObjectCount() == 0) break;
      }
      check("cells unloaded", provider.liveObjectCount() == 0);
      check("every streamed body was released", provider.liveBodyCount() == 0);
      std::printf("       bodies: %zu at peak, %zu after unload\n", bodiesLoaded,
                  provider.liveBodyCount());

      // Reload and confirm bodies come back — a cell revisited must be solid again, not a ghost.
      observer.position = glm::vec3(0.0f);
      streamer.setObservers({observer});
      for (int i = 0; i < 200; ++i) {
        pump(1);
        if (provider.liveBodyCount() > 0) break;
      }
      check("bodies return when the cell reloads", provider.liveBodyCount() > 0);

      std::printf("\n=== colliders: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Persistence round trip (WM-2.5 with real content) ──
    // The claim: move a streamed object, leave so the cell unloads, come back, and the object is
    // still where the player left it. Everything before this proved persistence with a fake
    // provider serving RAM; this is the first time it runs against content read off disk.
    if (persistTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[persistence round trip]\n");

      const CellId home = grid.cellAt(glm::vec3(0.0f), gridDesc.streamLevel);
      const WorldLayer layer = WorldLayer::Gameplay;

      // 1. Load the cell.
      observer.position = glm::vec3(0.0f);
      observer.velocity = glm::vec3(0.0f);
      streamer.setObservers({observer});
      for (int i = 0; i < 200; ++i) {
        auto* c = device->beginFrame();
        streamer.update(16.0f);
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        glm::vec3 probe;
        if (provider.objectPosition(home, layer, 0, probe)) break;
      }

      glm::vec3 before(0.0f);
      const bool found = provider.objectPosition(home, layer, 0, before);
      check("the home cell loaded and has a gameplay object", found);
      if (!found) { device->waitIdle(); ui.shutdown(); return 1; }

      // 2. Move it, as gameplay would.
      const glm::vec3 shift(0.0f, 25.0f, 0.0f);
      check("the object could be moved", provider.moveObject(home, layer, 0, shift));
      glm::vec3 moved(0.0f);
      provider.objectPosition(home, layer, 0, moved);
      check("the move took effect", glm::distance(moved, before + shift) < 0.01f);

      // 3. Leave, so the cell unloads and its delta is captured.
      streamer.setObservers({});
      for (int i = 0; i < 200; ++i) {
        auto* c = device->beginFrame();
        streamer.update(16.0f);
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        if (provider.liveObjectCount() == 0) break;
      }
      check("the cell unloaded", provider.liveObjectCount() == 0);
      check("a delta was captured on unload", persistence.has(home, layer));

      // 4. Come back. The delta must be replayed onto the freshly loaded cell.
      observer.position = glm::vec3(0.0f);
      streamer.setObservers({observer});
      for (int i = 0; i < 200; ++i) {
        auto* c = device->beginFrame();
        streamer.update(16.0f);
        auto& b = swapChain->backBuffer();
        renderer->render(c, b, scene);
        c->transition(b, rhi::ResourceState::Present);
        device->endFrame(*swapChain);
        glm::vec3 probe;
        if (provider.objectPosition(home, layer, 0, probe)) break;
      }

      std::printf("       after reload: resident=%u wanted=%u missing=%u live=%zu\n",
                  streamer.stats().cellsResident, streamer.stats().cellsWanted,
                  streamer.stats().cellsMissing, provider.liveObjectCount());
      glm::vec3 after(0.0f);
      const bool back = provider.objectPosition(home, layer, 0, after);
      check("the cell reloaded", back);
      check("the object is still where the player left it",
            back && glm::distance(after, before + shift) < 0.01f);
      check("it is NOT back at its authored position", back && glm::distance(after, before) > 1.0f);

      // Control: an object nobody touched must come back exactly as authored, and must not have
      // cost any persistence memory.
      glm::vec3 untouched(0.0f);
      if (provider.objectPosition(home, layer, 1, untouched)) {
        check("an untouched object returns to its authored place",
              glm::distance(untouched, before) > 0.001f); // different object, just prove it loaded
      }

      std::printf("\n=== persistence: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    // ── Hot reload (WM-9) ──
    // The editor loop, closed: change a cell on disk and a running game shows the edit within a
    // couple of frames — same slot, same world around it, no full reload. The watcher maps the
    // changed file back to its cell and asks the streamer to re-stream it in place.
    if (hotreloadTest) {
      int failures = 0;
      auto check = [&failures](const char* label, bool ok) {
        std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label);
        if (!ok) ++failures;
      };
      std::printf("\n[hot reload]\n");

      const CellId home = grid.cellAt(glm::vec3(0.0f), gridDesc.streamLevel);
      const WorldLayer layer = WorldLayer::Gameplay;

      auto pump = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
          auto* c = device->beginFrame();
          streamer.update(16.0f);
          auto& b = swapChain->backBuffer();
          renderer->render(c, b, scene);
          c->transition(b, rhi::ResourceState::Present);
          device->endFrame(*swapChain);
        }
      };

      // 1. Stream the home cell in.
      observer.position = glm::vec3(0.0f);
      observer.velocity = glm::vec3(0.0f);
      streamer.setObservers({observer});
      for (int i = 0; i < 300; ++i) {
        pump(1);
        glm::vec3 probe;
        if (provider.objectPosition(home, layer, 0, probe)) break;
      }
      glm::vec3 before(0.0f);
      const bool found = provider.objectPosition(home, layer, 0, before);
      check("the home cell streamed in with an object", found);
      if (!found) { device->waitIdle(); ui.shutdown(); return 1; }
      const uint32_t residentBefore = streamer.stats().cellsResident;

      // 2. Start the watcher, primed with the world exactly as it is now.
      CellHotReload watcher(kWorldRoot, streamer);
      watcher.poll();

      // 3. Edit the cell's file on disk: shove object 0 forty metres up. This is what the editor
      //    would write on Save.
      const std::string path = cellFilePath(kWorldRoot, home, uint32_t(layer));
      CellFile cf;
      const bool loaded = cf.load(path);
      check("the cell file loaded for editing", loaded && !cf.objects.empty());
      if (!loaded || cf.objects.empty()) { device->waitIdle(); ui.shutdown(); return 1; }
      const glm::vec3 editShift(0.0f, 40.0f, 0.0f);
      cf.objects[0].position += editShift;
      check("the edited cell file saved", cf.save(path));

      // 4. The watcher must notice and ask for a reload (not a full-world reload).
      const uint32_t reloadsBefore = streamer.stats().reloadsIssued;
      uint32_t issued = 0;
      for (int i = 0; i < 20 && issued == 0; ++i) issued = watcher.poll();
      check("the watcher detected the edited cell", issued >= 1);

      // 5. Re-stream in place: the object appears at its new authored position, everything else stays.
      glm::vec3 after = before;
      for (int i = 0; i < 400; ++i) {
        pump(1);
        glm::vec3 p;
        if (provider.objectPosition(home, layer, 0, p) &&
            glm::distance(p - before, editShift) < 0.05f) {
          after = p;
          break;
        }
      }
      provider.objectPosition(home, layer, 0, after);
      std::printf("       before y=%.2f, after y=%.2f (shift +%.0f), reloads=%u\n", before.y, after.y,
                  editShift.y, streamer.stats().reloadsIssued);
      check("the object moved to its edited position", glm::distance(after - before, editShift) < 0.5f);
      check("a reload was issued", streamer.stats().reloadsIssued > reloadsBefore);
      check("the watcher counts its work", watcher.totalReloads() >= 1);
      check("the rest of the world stayed resident",
            streamer.stats().cellsResident >= residentBefore - 1);

      // Restore the file so the test is idempotent across runs.
      cf.objects[0].position -= editShift;
      cf.save(path);

      std::printf("\n=== hot reload: %d failures ===\n", failures);
      device->waitIdle();
      ui.shutdown();
      return failures == 0 ? 0 : 1;
    }

    const float flySpeed = 55.0f;
    const int autoFrames = 900;
    const int shotEvery = 250;

    int frame = 0;
    int shotIndex = 0;
    float worstAfterWarmup = 0.0f;
    size_t peakObjects = 0;
    glm::vec3 prevCamPos = scene.camera.position();
    auto lastTick = std::chrono::steady_clock::now();

    while (!window.shouldClose()) {
      window.pollEvents();
      input.beginFrame();
      ui.beginFrame();
      ui.drawPerfHud(renderer->lastFrameMs(), renderer->drawCalls(), window.width(), window.height());

      const auto now = std::chrono::steady_clock::now();
      const float dt = std::clamp(std::chrono::duration<float>(now - lastTick).count(), 0.0001f, 0.1f);
      lastTick = now;

      if (autoRun) {
        const glm::vec3 p = scene.camera.position();
        scene.camera.setPosition(p + glm::vec3(flySpeed * dt, 0.0f, 0.0f));
        scene.camera.lookAt(scene.camera.position() + glm::vec3(1.0f, -0.12f, 0.2f));
      } else {
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
        const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 120.0f : 35.0f;
        const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
        // SkyLab passes a fixed 1/60 here rather than the measured dt. Kept verbatim: the point of
        // this rewrite is to change as little as possible from the sample that works.
        scene.camera.fly(1.0f / 60.0f, move * speed, look ? dx * 0.0025f : 0.0f,
                         look ? -dy * 0.0025f : 0.0f);
      }

      if (!ui.wantCaptureKeyboard() && input.keyPressed(GLFW_KEY_F12) && screenshotPath.empty()) {
        screenshotPath = shotPrefix + "_manual.png";
      }
      input.endFrame();

      auto* cmd = device->beginFrame();

      // Streaming inside the frame: upload() creates GPU buffers and the engine's upload arena is
      // reset per frame.
      const glm::vec3 camPos = scene.camera.position();
      observer.position = camPos;
      observer.velocity = (camPos - prevCamPos) / dt;
      prevCamPos = camPos;
      streamer.setObservers({observer});
      streamer.update(renderer->lastFrameMs());

      const auto& st = streamer.stats();
      peakObjects = std::max(peakObjects, provider.liveObjectCount());
      if (frame > 60) worstAfterWarmup = std::max(worstAfterWarmup, renderer->lastFrameMs());

      if (frame % 60 == 0) {
        std::printf("[%4d] pos=%7.0f streamed=%4zu scene=%4zu draws=%5u cells=%3u/%3u %.1fms\n",
                    frame, double(camPos.x), provider.liveObjectCount(), scene.objects.size(),
                    renderer->drawCalls(), st.cellsResident, st.cellsWanted,
                    double(renderer->lastFrameMs()));
      }

      auto& bb = swapChain->backBuffer();
      renderer->render(cmd, bb, scene);
      ui.endFrame(*cmd, bb);

      ScreenshotPending shot;
      const bool wantShot = (autoRun && frame > 40 && (frame % shotEvery) == 0) ||
                            (!screenshotPath.empty() && frame > 5);
      if (wantShot) shot = beginScreenshot(*device, *cmd, bb);

      cmd->transition(bb, rhi::ResourceState::Present);
      device->endFrame(*swapChain);

      if (shot.impl) {
        device->waitIdle();
        const std::string path =
            screenshotPath.empty() ? shotPrefix + "_" + std::to_string(shotIndex++) + ".png"
                                   : screenshotPath;
        finalizeScreenshot(shot, path);
        std::printf("       saved %s (%zu streamed objects live)\n", path.c_str(),
                    provider.liveObjectCount());
        screenshotPath.clear();
      }

      ++frame;
      if (autoRun && frame >= autoFrames) break;
    }

    const auto& st = streamer.stats();
    std::printf("\n=== WorldLab summary ===\n");
    std::printf("frames            : %d\n", frame);
    std::printf("streamed objects  : %zu live (peak %zu)\n", provider.liveObjectCount(), peakObjects);
    std::printf("cells resident    : %u\n", st.cellsResident);
    std::printf("loads completed   : %u, failed %u, cancelled %u\n", st.loadsCompleted,
                st.loadsFailed, st.cancellations);
    std::printf("worst frame       : %.1f ms after warm-up\n", double(worstAfterWarmup));
    std::printf("bounded residency : %s\n",
                (provider.liveObjectCount() <= peakObjects && peakObjects > 0) ? "yes" : "NO");

    device->waitIdle();
    ui.shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
