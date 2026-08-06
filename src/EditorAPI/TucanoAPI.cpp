#include "EditorAPI/TucanoAPI.h"

#include "Platform/Input.h"
#include "Platform/Window.h"
#include "AssetPipeline/GLTFAnimation.h"
#include "AssetPipeline/AssetImport.h"
#include "Renderer/Weather/WeatherReflection.h"
#include "AssetPipeline/GLTFLoader.h"
#include "Core/Json.h"
#include "Input/VirtualInput.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/DevTexture.h"
#include "Renderer/Renderer.h"
#include "Renderer/Mesh.h"
#include "Runtime/Screenshot.h"
#include "Runtime/DebugUI.h"
#include <imgui.h>
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Audio/AudioListener.h"
#include "Audio/AudioSource.h"
#include "World/FrustumCull.h"
#include "World/GpuCellCuller.h"
#include "World/WorldGrid.h"
#include "Core/TaskScheduler.h"
#include "World/StreamingScheduler.h"
#include "World/StreamingBudget.h"
#include "World/SceneCellProvider.h"
#include "World/WorldGenerator.h"
#include "Terrain/Heightmap.h"
#include "Terrain/TerrainGenerator.h"
#include "Terrain/TerrainComponent.h"
#include "Terrain/HeightmapBrush.h"
#include "Terrain/ErosionSimulation.h"
#include "Terrain/MaterialNodeEditor.h"
#include "Terrain/Heightmap.h"
#include "Terrain/TerrainGenerator.h"
#include "Terrain/TerrainComponent.h"
#include "Terrain/HeightmapBrush.h"
#include "Terrain/ErosionSimulation.h"
#include "Vegetation/VegetationSystem.h"
#include "Vegetation/VegetationRenderer.h"
#include "Vegetation/VegetationDispatch.h"
#include "Vegetation/VegetationTool.h"
#include "Vegetation/WindSystem.h"
#include "Vegetation/SeasonSystem.h"
#include "Vegetation/GrowthSystem.h"
#include "Vegetation/VegetationInteraction.h"
#include "Vegetation/VegetationEditor.h"
#include "Vegetation/LODManager.h"
#include "Lua/LuaVM.h"
#include "Lua/LuaBindings.h"

#include <functional>
#include <cmath>

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL // required by gtx/matrix_decompose
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>
#include <mutex>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ── Internal helpers ─────────────────────────────────

namespace {

std::shared_ptr<tucano::Mesh> makeGroundPlane(tucano::rhi::Device& device,
                                               float halfExtent, int segments) {
  using namespace tucano;
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
      indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
      indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }
  }
  SubMesh sub{};
  sub.indexCount = static_cast<uint32_t>(indices.size());
  sub.aabbMin = {-halfExtent, -0.01f, -halfExtent};
  sub.aabbMax = {halfExtent, 0.01f, halfExtent};
  return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<tucano::Mesh> makeMarkerSphere(tucano::rhi::Device& device,
                                                float radius, int segments) {
  using namespace tucano;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  for (int y = 0; y <= segments; ++y) {
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / float(segments);
      const float v = float(y) / float(segments);
      const float theta = u * 6.2831853f;
      const float phi = v * 3.14159265f;
      Vertex vert{};
      vert.position = {radius * std::sin(phi) * std::cos(theta),
                       radius * std::cos(phi),
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
      indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
      indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }
  }
  SubMesh sub{};
  sub.indexCount = static_cast<uint32_t>(indices.size());
  return Mesh::create(device, verts, indices, {sub});
}

std::shared_ptr<tucano::Mesh> makeCubeMesh(tucano::rhi::Device& device, float size) {
  using namespace tucano;
  const float s = size * 0.5f;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  // p0..p3 wind counter-clockwise starting at the face's lower-left. V is flipped because texture
  // space has V=0 at the top: without this any oriented detail (the scale label) renders upside
  // down on every side face.
  auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
    const uint32_t base = uint32_t(verts.size());
    // U runs right-to-left because the engine is left-handed: with the camera looking down -Z, +X
    // lands on the left of the screen, so a naive U made text read mirrored from outside the cube.
    verts.push_back({p0, n, {t.x, t.y, t.z, 1}, {1, 1}, {1, 1, 1, 1}});
    verts.push_back({p1, n, {t.x, t.y, t.z, 1}, {0, 1}, {1, 1, 1, 1}});
    verts.push_back({p2, n, {t.x, t.y, t.z, 1}, {0, 0}, {1, 1, 1, 1}});
    verts.push_back({p3, n, {t.x, t.y, t.z, 1}, {1, 0}, {1, 1, 1, 1}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  };
  face({0, 1, 0}, {1, 0, 0}, {-s, s, -s}, {s, s, -s}, {s, s, s}, {-s, s, s});
  face({0, -1, 0}, {1, 0, 0}, {-s, -s, s}, {s, -s, s}, {s, -s, -s}, {-s, -s, -s});
  face({0, 0, 1}, {1, 0, 0}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s});
  face({0, 0, -1}, {-1, 0, 0}, {s, -s, -s}, {-s, -s, -s}, {-s, s, -s}, {s, s, -s});
  face({1, 0, 0}, {0, 0, -1}, {s, -s, s}, {s, -s, -s}, {s, s, -s}, {s, s, s});
  face({-1, 0, 0}, {0, 0, 1}, {-s, -s, -s}, {-s, -s, s}, {-s, s, s}, {-s, s, -s});
  SubMesh sub{};
  sub.indexCount = uint32_t(indices.size());
  sub.aabbMin = {-s, -s, -s};
  sub.aabbMax = {s, s, s};
  return Mesh::create(device, verts, indices, {sub});
}

void configureCleanRenderer(tucano::Renderer& renderer) {
  auto& s = renderer.settings();
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
  s.giTier = tucano::GITier::Off;
  renderer.rain().enabled = false;
  renderer.rain().enableSceneRain = false;
  renderer.rain().enableWorldSplashes = true;
  renderer.rain().amount = 0.85f;
  renderer.rain().streakIntensity = 1.0f;
  renderer.rain().puddlesAmount = 1.1f;
  renderer.rain().mistAmount = 0.35f;
}

void buildCleanScene(tucano::rhi::Device& device, tucano::Scene& scene) {
  scene.objects.clear();
  scene.lights.clear();
  scene.camera.setPerspective(glm::radians(65.0f), 16.0f / 9.0f, 0.2f, 4000.0f);
  // Close enough that the starting cube fills a good part of the frame and the ground grid reads —
  // the old wide shot made an empty-looking scene.
  scene.camera.setPosition({3.4f, 2.2f, 5.6f});
  scene.camera.lookAt({0.0f, 0.9f, 0.0f});
  scene.addDirectional(
      glm::normalize(glm::vec3(-0.35f, -1.0f, -0.2f)), {1.0f, 0.96f, 0.9f}, 6.0f);
  auto groundMat = std::make_shared<tucano::Material>();
  groundMat->name = "Ground";
  // Dev grid on the floor too — it is what gives the viewport a sense of distance and speed while
  // flying around, and makes object placement readable before any real art exists. The floor
  // variant drops the metre label, which would tile hundreds of times and read as noise.
  groundMat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  groundMat->albedo = tucano::devtex::defaultFloor(device);
  groundMat->normal = tucano::devtex::defaultNormal(device);
  groundMat->roughnessFactor = 0.88f;
  groundMat->metallicFactor = 0.0f;
  tucano::RenderObject ground;
  ground.name = "Ground";
  ground.mesh = makeGroundPlane(device, 800.0f, 32);
  ground.materials = {groundMat};
  ground.worldMatrix = glm::mat4(1.0f);
  scene.objects.push_back(std::move(ground));
  // A single dev-textured box sitting on the grid: enough to show scale, lighting and shadows the
  // moment the editor opens, without pretending to be a level.
  auto boxMat = std::make_shared<tucano::Material>();
  boxMat->name = "Box";
  boxMat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  boxMat->albedo = tucano::devtex::defaultAlbedo(device);
  boxMat->normal = tucano::devtex::defaultNormal(device);
  boxMat->roughnessFactor = 0.72f;
  boxMat->metallicFactor = 0.0f;

  tucano::RenderObject box;
  box.name = "Box";
  box.mesh = makeCubeMesh(device, 1.0f);
  box.materials = {boxMat};
  box.transform.translation = {0.0f, 0.5f, 0.0f};
  box.worldMatrix = box.transform.matrix();
  scene.objects.push_back(std::move(box));
}

} // namespace

// ── Internal handle structs ──────────────────────────

// How each scene object was created. Kept parallel to Scene::objects (every mutation goes through
// this API) so a saved scene can be rebuilt: a RenderObject only holds GPU buffers, which say
// nothing about where the geometry came from.
// Animation state for one object: the rig it was imported with, its clips, and where playback is.
// Held beside the scene rather than inside RenderObject so the renderer stays unaware of clips.
struct ObjectAnimation {
  tucano::anim::Skeleton skeleton;
  std::vector<std::shared_ptr<tucano::anim::AnimationClip>> clips;
  tucano::anim::AnimationPlayer player;
  tucano::anim::Pose pose;
  int currentClip = -1;
};

struct ObjectSource {
  enum class Kind { Cube, Sphere, Plane, Gltf, BuiltinGround, BuiltinMarker };
  Kind kind = Kind::Cube;
  std::string path;    // Gltf only
  std::string folder;  // outliner group, "" = scene root
  int physics = 0;     // TucanoPhysicsKind
  float mass = 1.0f;
};

struct TucanoRuntime {
  TucanoInitDesc desc;
  std::unique_ptr<tucano::Window> window;
  std::unique_ptr<tucano::rhi::Device> device;
  std::unique_ptr<tucano::rhi::SwapChain> swapChain;
  std::unique_ptr<tucano::Renderer> renderer;
  std::unique_ptr<tucano::Input> input;
  std::unique_ptr<tucano::DebugUI> debugUI;
  tucano::Scene scene;
  std::vector<ObjectSource> sources; // 1:1 with scene.objects
  // Sparse: only objects that were imported with a rig get an entry.
  std::unordered_map<uint32_t, ObjectAnimation> animations;
  bool alive = true;

  // Play mode. editTransforms holds the authored placement so Stop can undo the simulation.
  std::unique_ptr<tucano::physics::PhysicsWorld> physics;
  std::vector<JPH::BodyID> bodies; // parallel to scene.objects; invalid id = no collider
  std::vector<tucano::Transform> editTransforms;
  int playState = 0; // TucanoPlayState
  uint32_t collidersBuilt = 0;
  uint32_t collidersFailed = 0;
  float playAccumulator = 0.0f;

  // Wall-clock delta between render calls. Renderer::lastFrameMs() only measures GPU work, so using
  // it as a timestep makes physics and camera motion run slower than real time whenever the host
  // paces frames (an editor at 60 Hz with a 2 ms frame would advance time 8x too slowly).
  std::chrono::steady_clock::time_point lastTick{};
  bool hasTick = false;
  float deltaSeconds = 1.0f / 60.0f;
  bool cameraNavigation = true; // editor fly-cam inside the embedded viewport
  bool overlayVisible = false;  // editor has its own status bar; avoid a second FPS counter
  // Cached primitive meshes so spawning doesn't rebuild geometry each call.
  std::shared_ptr<tucano::Mesh> primCube;
  std::shared_ptr<tucano::Mesh> primSphere;
  std::shared_ptr<tucano::Mesh> primPlane;
  // Viewport selection / drag state.
  int selectedObject = -1;
  glm::vec3 selectedBaseColor{1.0f}; // untinted colour, restored when the selection moves on
  bool dragging = false;
  float dragDistance = 0.0f;   // camera→object distance frozen at grab time
  glm::vec3 dragGrabOffset{0.0f}; // object origin minus grab point, keeps the object from snapping
  uint32_t viewportWidth = 1;
  uint32_t viewportHeight = 1;
  // render() pumps the OS message queue, so a host that renders from a message handler can re-enter
  // it. Recording D3D12 commands re-entrantly corrupts the device, so the nested call is dropped.
  bool inRender = false;

  // Per-frame phase breakdown, in milliseconds. Exposed through
  // tucano_runtime_frame_breakdown so the host can attribute a slow frame instead of guessing.
  float phaseSimMs = 0.0f;      // input, scripts, animation, physics, streaming
  float phaseGpuWaitMs = 0.0f;  // waiting for the GPU / presentation engine to free a slot
  float phaseRecordMs = 0.0f;   // building this frame's scene command list
  float phaseUiMs = 0.0f;       // ImGui overlay recording
  float phaseVegMs = 0.0f;      // vegetation instance upload + GPU cull dispatch
  float phasePresentMs = 0.0f;  // submit + Present

  // Virtual input (Phase I-0). The snapshot pair gives edge detection for physical buttons; the
  // VirtualInput layer turns those into named buttons and axes.
  tucano::input::VirtualInput virtualInput;
  tucano::input::InputSnapshot inputSnapshot{};
  tucano::input::InputSnapshot prevInputSnapshot{};
  // Gizmo. `gizmoBlocking` carries last frame's hover/use state: the gizmo is drawn after input is
  // sampled, so the current frame's answer isn't available when picking decides whether to run.
  tucano::DebugUI::GizmoOp gizmoOp = tucano::DebugUI::GizmoOp::Translate;
  bool gizmoEnabled = true;
  bool gizmoWorldSpace = true;
  float gizmoSnap = 0.0f;
  float gizmoScale = 1.0f;
  bool gizmoBlocking = false;

  // ── Editor overlay ──
  // Widgets drawn inside the viewport by the engine, because the host's UI toolkit cannot paint
  // over a native child window. See drawEditorOverlay().
  uint32_t overlayFlags = 0;      // see TUCANO_OVERLAY_* bits
  std::string dropHint;           // non-empty shows a badge following the cursor


  // Terrain sculpt mode (TM-5)
  bool terrainSculptActive = false;
  float terrainBrushRadius = 20.0f;
  float terrainBrushStrength = 15.0f;
  int terrainBrushTool = 0;
  int terrainObjectIndex = -1;

  // Material graph editor
  std::unique_ptr<tucano::terrain::MaterialNodeEditor> materialNodeEditor;
  bool materialGraphOpen = false;

  // Async asset import
  std::atomic<float> importProgress{0.0f};
  std::atomic<bool> importDone{true};
  std::atomic<int> importCount{0};
  mutable std::mutex importMutex;
  std::string importStatus;
  std::thread importThread;

  // Audio (Phase I-2)
  std::vector<tucano::AudioClip*> audioClips;
  std::vector<std::unique_ptr<tucano::AudioSource>> audioSources;

  // Terrain (TM-5)
  std::shared_ptr<tucano::terrain::Heightmap> terrainHm;
  std::unique_ptr<tucano::terrain::TerrainComponent> terrainComp;
  std::unique_ptr<tucano::terrain::BrushSystem> terrainBrushes;
  std::unique_ptr<tucano::terrain::ErosionSimulation> terrainErosion;

  // Vegetation (Veg-1)
  std::unique_ptr<tucano::veg::VegetationRenderer> vegRenderer;
  bool vegEnabled = false;

  // World streaming session (WM-9 outliner). Declared streamer-last so it destroys first — it
  // references the grid/tasks/budget/provider that follow it in memory.
  std::unique_ptr<tucano::world::WorldGrid> streamGrid;
  std::unique_ptr<tucano::core::TaskScheduler> streamTasks;
  std::unique_ptr<tucano::world::StreamingBudget> streamBudget;
  std::unique_ptr<tucano::world::SceneCellProvider> streamProvider;
  std::unique_ptr<tucano::world::StreamingScheduler> streamer;
  tucano::world::StreamingObserver streamObserver;
  std::string streamWorldRoot;
  bool streamingActive = false;
  bool streamAutoStarted = false;
};

// `data` aliases the runtime's scene rather than copying it: the renderer draws runtime->scene, so
// a separate authoring copy would silently discard anything the viewport itself changes (a dragged
// object would snap back on the next spawn).
struct TucanoScene {
  TucanoRuntime* runtime;
  tucano::Scene& data;
};

// ── Lifecycle ────────────────────────────────────────

TUCANO_API int tucano_api_version(void) {
  return TUCANO_API_VERSION;
}

TUCANO_API const char* tucano_runtime_version(void) {
  return "0.1.0";
}

TUCANO_API TucanoRuntime* tucano_runtime_init(const TucanoInitDesc* desc) {
  if (!desc) return nullptr;

  auto* rt = new TucanoRuntime();
  rt->desc = *desc;

  try {
    const uint32_t w = desc->width > 0 ? desc->width : 1280;
    const uint32_t h = desc->height > 0 ? desc->height : 720;
    const char* title = desc->title ? desc->title : "Tucano Engine";
    rt->viewportWidth = w;
    rt->viewportHeight = h;

    rt->window = std::make_unique<tucano::Window>(
        tucano::WindowDesc{w, h, title, true, !desc->borderless});

    rt->device = tucano::rhi::Device::create(desc->enableDebugLayer);

    rt->swapChain = rt->device->createSwapChain(
        rt->window->nativeHandle(), w, h, desc->vsync);

    rt->renderer = std::make_unique<tucano::Renderer>(*rt->device, w, h);
    configureCleanRenderer(*rt->renderer);

    rt->input = std::make_unique<tucano::Input>(rt->window->handle());
    rt->debugUI = std::make_unique<tucano::DebugUI>();
    rt->debugUI->init(*rt->window, *rt->device);

    buildCleanScene(*rt->device, rt->scene);
    rt->scene.camera.setPerspective(glm::radians(65.0f), rt->window->aspect(), 0.2f, 4000.0f);

    {
      using namespace tucano;
      LuaVM::instance().init();
      LuaVM::instance().setDevice(rt->device.get());
      LuaVM::instance().setScene(&rt->scene);
      LuaVM::instance().setRenderer(rt->renderer.get());
      LuaVM::instance().setCamera(&rt->scene.camera);
      LuaVM::instance().setInput(rt->input.get());
      LuaVM::instance().setAudio(&Audio::instance());
      LuaVM::instance().registerAllBindings();
      LuaVM::instance().loadScript("Scripts/main.lua");
      LuaVM::instance().callSetup();

      rt->vegRenderer = std::make_unique<veg::VegetationRenderer>(*rt->device, 100000);
      auto csRoot = rt->renderer->sharedComputeRootSig();
      if (csRoot) {
        rt->vegRenderer->createPipeline(csRoot);
        rt->vegEnabled = true;

        auto& vegSys = veg::VegetationSystem::instance();
        veg::VegetationType grassType;
        grassType.name = "Grass";
        grassType.proceduralKind = 1;
        grassType.minScale = 0.3f; grassType.maxScale = 0.8f;
        grassType.windFlexibility = 1.5f; grassType.windHeight = 0.6f;
        grassType.cullDistance = 120.0f;
        vegSys.registerType(grassType);

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

        for (int cx = -2; cx <= 2; ++cx)
          for (int cz = -2; cz <= 2; ++cz)
            vegSys.scatter(cx, cz, 0, 200, uint32_t(42 + cx * 100 + cz));

        for (int cx = -1; cx <= 1; ++cx)
          for (int cz = -1; cz <= 1; ++cz)
            vegSys.scatter(cx, cz, 1, 50, uint32_t(100 + cx * 50 + cz));

        veg::VegetationConfig vegCfg;
        vegCfg.globalWindStrength = 0.8f;
        vegSys.configure(vegCfg);
      }
    }

    rt->device->setDeviceLostCallback([rt]() {
      const auto cam = rt->scene.camera;
      const auto settings = rt->renderer->settings();
      rt->debugUI->shutdown();
      rt->renderer.reset();
      rt->swapChain.reset();
      rt->swapChain = rt->device->createSwapChain(
          rt->window->nativeHandle(), rt->window->width(), rt->window->height(), rt->desc.vsync);
      rt->renderer = std::make_unique<tucano::Renderer>(*rt->device,
          rt->window->width(), rt->window->height());
      rt->renderer->settings() = settings;
      buildCleanScene(*rt->device, rt->scene);
      rt->scene.camera = cam;
      rt->debugUI->init(*rt->window, *rt->device);
    });

    rt->window->setResizeCallback([rt](uint32_t nw, uint32_t nh) {
      if (!rt->swapChain || !rt->renderer) return;
      rt->swapChain->resize(nw, nh);
      rt->renderer->resize(nw, nh);
      rt->scene.camera.setPerspective(
          glm::radians(65.0f), rt->window->aspect(), 0.2f, 4000.0f);
    });

    return rt;
  } catch (...) {
    delete rt;
    return nullptr;
  }
}

TUCANO_API void tucano_runtime_shutdown(TucanoRuntime* rt) {
  if (!rt) return;
  tucano::LuaVM::instance().shutdown();
  // Process-lifetime caches own GPU resources, so they have to be dropped here, while the device
  // is still alive. Left to static destruction they release after the device is gone, and
  // ~DX12Texture / ~DX12Buffer then dereference a dangling device pointer — an intermittent
  // access violation during process teardown.
  tucano::LuaBindings::releaseCachedResources();
  tucano::devtex::releaseDefaults();
  rt->device->waitIdle();
  if (rt->importThread.joinable()) rt->importThread.join();
  rt->debugUI->shutdown();
  delete rt;
}

TUCANO_API bool tucano_runtime_alive(TucanoRuntime* rt) {
  if (!rt) return false;
  return rt->alive && !rt->window->shouldClose();
}

// ── Viewport / Window ────────────────────────────────

TUCANO_API void* tucano_runtime_native_window(TucanoRuntime* rt) {
  if (!rt || !rt->window) return nullptr;
  return rt->window->nativeHandle();
}

TUCANO_API void* tucano_runtime_viewport_handle(TucanoRuntime* rt) {
  if (!rt || !rt->swapChain) return nullptr;
  // Return DX12 swapchain backbuffer resource pointer (for external composition)
  return &rt->swapChain->backBuffer();
}

TUCANO_API void tucano_runtime_resize(TucanoRuntime* rt, uint32_t w, uint32_t h) {
  if (!rt || !rt->swapChain || !rt->renderer) return;
  rt->swapChain->resize(w, h);
  rt->renderer->resize(w, h);
  rt->viewportWidth = w;
  rt->viewportHeight = h;
  rt->scene.camera.setPerspective(
      glm::radians(65.0f), float(w) / float(h), 0.2f, 4000.0f);
}

namespace {

// Editor viewport navigation: RMB looks, WASD flies, QE up/down, Shift boosts, scroll dollies.
// Union of the mesh submesh AABBs, in object space.
bool objectLocalBounds(const tucano::RenderObject& obj, glm::vec3& lo, glm::vec3& hi) {
  if (!obj.mesh || obj.mesh->submeshes().empty()) return false;
  lo = glm::vec3(std::numeric_limits<float>::max());
  hi = glm::vec3(std::numeric_limits<float>::lowest());
  for (const auto& sm : obj.mesh->submeshes()) {
    lo = glm::min(lo, sm.aabbMin);
    hi = glm::max(hi, sm.aabbMax);
  }
  // Flat meshes (ground planes) have a zero-thickness axis and would never be hit by a ray, so
  // give every axis a minimum thickness. Fully degenerate bounds mean there is nothing to pick.
  if (glm::any(glm::greaterThan(lo, hi)) || lo == hi) return false;
  constexpr float kMinHalf = 0.01f;
  for (int a = 0; a < 3; ++a) {
    if (hi[a] - lo[a] < kMinHalf * 2.0f) {
      const float mid = (hi[a] + lo[a]) * 0.5f;
      lo[a] = mid - kMinHalf;
      hi[a] = mid + kMinHalf;
    }
  }
  return true;
}

// Slab test. `o`/`d` must already be in the box's space.
bool rayHitsAabb(const glm::vec3& o, const glm::vec3& d, const glm::vec3& lo, const glm::vec3& hi,
                 float& tNear) {
  float t0 = 0.0f, t1 = std::numeric_limits<float>::max();
  for (int a = 0; a < 3; ++a) {
    if (std::abs(d[a]) < 1e-8f) {
      if (o[a] < lo[a] || o[a] > hi[a]) return false;
      continue;
    }
    const float inv = 1.0f / d[a];
    float n = (lo[a] - o[a]) * inv;
    float f = (hi[a] - o[a]) * inv;
    if (n > f) std::swap(n, f);
    t0 = std::max(t0, n);
    t1 = std::min(t1, f);
    if (t0 > t1) return false;
  }
  tNear = t0;
  return true;
}

// Cursor → world-space ray, via the inverse view-projection.
bool cursorRay(TucanoRuntime* rt, glm::vec3& origin, glm::vec3& dir) {
  float mx = 0.0f, my = 0.0f;
  rt->input->mousePosition(mx, my);
  const float w = float(std::max(rt->viewportWidth, 1u));
  const float h = float(std::max(rt->viewportHeight, 1u));
  if (mx < 0.0f || my < 0.0f || mx > w || my > h) return false;

  const float ndcX = (mx / w) * 2.0f - 1.0f;
  const float ndcY = 1.0f - (my / h) * 2.0f; // GLFW y grows downward
  const glm::mat4 invVP = glm::inverse(rt->scene.camera.viewProj());
  // Reverse-Z is not assumed here: near=1 / far=0 both get probed and the pair is ordered by
  // taking the camera position as the origin.
  glm::vec4 pNear = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  glm::vec4 pFar = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  if (std::abs(pNear.w) < 1e-9f || std::abs(pFar.w) < 1e-9f) return false;
  const glm::vec3 a = glm::vec3(pNear) / pNear.w;
  const glm::vec3 b = glm::vec3(pFar) / pFar.w;

  origin = rt->scene.camera.position();
  const glm::vec3 toA = a - origin;
  const glm::vec3 toB = b - origin;
  const glm::vec3 far = glm::dot(toA, toA) > glm::dot(toB, toB) ? a : b;
  const glm::vec3 delta = far - origin;
  if (glm::dot(delta, delta) < 1e-12f) return false;
  dir = glm::normalize(delta);
  return true;
}

// Nearest object under the cursor, or -1. Tests in object space so the box follows rotation/scale.
int pickObject(TucanoRuntime* rt) {
  glm::vec3 ro, rd;
  if (!cursorRay(rt, ro, rd)) return -1;

  int best = -1;
  float bestT = std::numeric_limits<float>::max();
  for (size_t i = 0; i < rt->scene.objects.size(); ++i) {
    const auto& obj = rt->scene.objects[i];
    if (!obj.visible) continue; // hidden objects are not clickable
    glm::vec3 lo, hi;
    if (!objectLocalBounds(obj, lo, hi)) continue;

    const glm::mat4 inv = glm::inverse(obj.worldMatrix);
    const glm::vec3 lro = glm::vec3(inv * glm::vec4(ro, 1.0f));
    const glm::vec3 lrd = glm::vec3(inv * glm::vec4(rd, 0.0f));
    if (glm::dot(lrd, lrd) < 1e-16f) continue;

    float t = 0.0f;
    if (!rayHitsAabb(lro, lrd, lo, hi, t)) continue;
    // t is in local units; compare in world space so scale doesn't bias the ordering.
    const glm::vec3 hitWorld = glm::vec3(obj.worldMatrix * glm::vec4(lro + lrd * t, 1.0f));
    const float worldT = glm::length(hitWorld - ro);
    if (worldT < bestT) {
      bestT = worldT;
      best = int(i);
    }
  }
  return best;
}

// Marks the selection by tinting its base colour. Emissive would read better, but GBuffer.hlsl
// multiplies emissiveFactor by the emissive texture, so a material without one stays black — base
// colour is the only channel that shows up with no extra pass. A proper outline belongs in the
// renderer later.
void applySelection(TucanoRuntime* rt, int index) {
  if (rt->selectedObject == index) return;

  auto materialOf = [&](int idx) -> tucano::Material* {
    if (idx < 0 || idx >= int(rt->scene.objects.size())) return nullptr;
    auto& mats = rt->scene.objects[size_t(idx)].materials;
    return mats.empty() ? nullptr : mats[0].get();
  };

  if (auto* prev = materialOf(rt->selectedObject)) {
    prev->baseColorFactor = glm::vec4(rt->selectedBaseColor, prev->baseColorFactor.a);
  }
  rt->selectedObject = index;
  if (auto* cur = materialOf(index)) {
    rt->selectedBaseColor = glm::vec3(cur->baseColorFactor);
    const glm::vec3 marker(1.0f, 0.62f, 0.05f); // editor amber
    cur->baseColorFactor =
        glm::vec4(glm::mix(rt->selectedBaseColor, marker, 0.65f), cur->baseColorFactor.a);
  } else {
    rt->selectedBaseColor = glm::vec3(1.0f);
  }
}

// Left-drag moves the selected object across the plane that faces the camera and passes through the
// object, which is the cheapest thing that still feels direct. Axis gizmos come later.
void updateObjectDrag(TucanoRuntime* rt) {
  auto& in = *rt->input;

  // The gizmo owns the left button whenever the cursor is on one of its handles.
  if (rt->gizmoBlocking) {
    rt->dragging = false;
    return;
  }

  if (!in.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) {
    rt->dragging = false;
    return;
  }

  glm::vec3 ro, rd;
  if (!cursorRay(rt, ro, rd)) return;

  if (in.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
    const int hit = pickObject(rt);
    applySelection(rt, hit);
    rt->dragging = false;
    if (hit >= 0) {
      const auto& obj = rt->scene.objects[size_t(hit)];
      const glm::vec3 origin = obj.transform.translation;
      rt->dragDistance = glm::dot(origin - ro, rd);
      if (rt->dragDistance > 0.05f) {
        rt->dragGrabOffset = origin - (ro + rd * rt->dragDistance);
        rt->dragging = true;
      }
    }
    return;
  }

  if (!rt->dragging || rt->selectedObject < 0 ||
      rt->selectedObject >= int(rt->scene.objects.size())) {
    return;
  }
  auto& obj = rt->scene.objects[size_t(rt->selectedObject)];
  obj.transform.translation = ro + rd * rt->dragDistance + rt->dragGrabOffset;
  obj.worldMatrix = obj.transform.matrix();
}

// World-space half extents of an object, from its mesh AABB scaled by the transform.
bool objectWorldHalfExtent(const tucano::RenderObject& obj, glm::vec3& halfExtent,
                           glm::vec3& centerOffset) {
  if (!obj.mesh || obj.mesh->submeshes().empty()) return false;
  glm::vec3 lo(std::numeric_limits<float>::max());
  glm::vec3 hi(std::numeric_limits<float>::lowest());
  for (const auto& sm : obj.mesh->submeshes()) {
    lo = glm::min(lo, sm.aabbMin);
    hi = glm::max(hi, sm.aabbMax);
  }
  // A ground plane is flat: its AABB is degenerate on Y (Mesh::create recomputes bounds from the
  // vertices, so a flat mesh really does report min.y == max.y). Only a fully degenerate box is
  // unusable — flat ones get thickened below.
  if (glm::any(glm::greaterThan(lo, hi)) || lo == hi) return false;
  const glm::vec3 s = glm::abs(obj.transform.scale);
  halfExtent = (hi - lo) * 0.5f * s;
  centerOffset = (hi + lo) * 0.5f * s;

  // Jolt's default convex radius is 0.05: a BoxShape thinner than that on any axis is rejected and
  // no body is created at all — which is how a 2 cm ground plane silently became a hole. Thicken
  // thin axes and push the centre down by the same amount so the visible surface stays put.
  constexpr float kMinHalf = 0.06f;
  for (int a = 0; a < 3; ++a) {
    if (halfExtent[a] < kMinHalf) {
      centerOffset[a] -= kMinHalf - halfExtent[a];
      halfExtent[a] = kMinHalf;
    }
  }
  return true;
}

// Advances every animation player and refreshes the skinning palette it feeds.
// Runs before the gizmo and physics so the pose the frame renders is the pose the editor reports.
void updateAnimations(TucanoRuntime* rt) {
  if (rt->animations.empty()) return;

  for (auto& [index, animation] : rt->animations) {
    if (index >= rt->scene.objects.size()) continue;
    auto& obj = rt->scene.objects[index];

    animation.player.update(rt->deltaSeconds);

    // Always start from the bind pose: a clip that animates only some bones must leave the rest
    // at rest instead of accumulating drift from the previous frame.
    animation.pose = animation.skeleton.bindPose();
    animation.player.evaluate(animation.pose);
    animation.skeleton.computeSkinningMatrices(animation.pose, obj.skinningMatrices);
  }
}

// Advances the simulation on a fixed step and copies dynamic body transforms back onto the objects.
// Fixed step keeps the simulation deterministic and stable regardless of the editor's frame rate.
// Samples the wall clock once per frame; every other per-frame helper reads rt->deltaSeconds.
void tickClock(TucanoRuntime* rt) {
  const auto now = std::chrono::steady_clock::now();
  if (rt->hasTick) {
    const float dt = std::chrono::duration<float>(now - rt->lastTick).count();
    // Clamp so a breakpoint or a stalled frame can't teleport the simulation.
    rt->deltaSeconds = glm::clamp(dt, 1.0f / 1000.0f, 0.1f);
  }
  rt->lastTick = now;
  rt->hasTick = true;
}

void updatePlay(TucanoRuntime* rt) {
  if (rt->playState != 1 /*RUNNING*/ || !rt->physics) return;

  constexpr float kStep = 1.0f / 60.0f;
  constexpr float kMaxCatchUp = 0.25f; // never spiral trying to catch up after a stall

  rt->playAccumulator = std::min(rt->playAccumulator + rt->deltaSeconds, kMaxCatchUp);
  while (rt->playAccumulator >= kStep) {
    rt->physics->step(kStep);
    rt->playAccumulator -= kStep;
  }

  auto& objects = rt->scene.objects;
  const size_t n = std::min(objects.size(), rt->bodies.size());
  for (size_t i = 0; i < n; ++i) {
    if (rt->bodies[i].IsInvalid()) continue;
    if (i >= rt->sources.size() || rt->sources[i].physics != 2 /*DYNAMIC*/) continue;

    // Bodies are centred on the mesh AABB centre, which may be offset from the object origin.
    glm::vec3 half, offset;
    if (!objectWorldHalfExtent(objects[i], half, offset)) continue;
    const glm::quat rot = rt->physics->getBodyRotation(rt->bodies[i]);
    objects[i].transform.rotation = rot;
    objects[i].transform.translation = rt->physics->getBodyPosition(rt->bodies[i]) - (rot * offset);
    objects[i].worldMatrix = objects[i].transform.matrix();
  }
}

/// Widgets drawn in viewport space by the engine itself.
///
/// The host is an Avalonia application and Avalonia cannot paint over a NativeControlHost, so
/// anything that has to appear *on* the 3D image — a drop badge following the cursor, an object
/// label pinned to a world position, a live gizmo readout — cannot come from the host. Drawing it
/// here costs nothing extra: ImGui is already in the frame for the gizmo.
///
/// Must run between DebugUI::beginFrame() and endFrame().
void drawEditorOverlay(TucanoRuntime* rt) {
  if (!rt->overlayFlags && rt->dropHint.empty()) return;

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  // ImGui's own display size, not the runtime's cached viewport dimensions: those are updated by
  // the host's resize callback and can lag a frame, which parks a bottom-anchored widget off the
  // bottom of the screen.
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  if (screen.x < 1.0f || screen.y < 1.0f) return;

  // Editor palette, matching the host's chrome so the overlay reads as one product.
  const ImU32 kInk = IM_COL32(242, 239, 230, 235);
  const ImU32 kInkDim = IM_COL32(154, 154, 164, 220);
  const ImU32 kAmber = IM_COL32(255, 201, 74, 255);
  const ImU32 kPanel = IM_COL32(12, 12, 15, 205);
  const ImU32 kEdge = IM_COL32(58, 44, 16, 255);

  auto badge = [&](ImVec2 at, const char* text, ImU32 textCol, ImU32 edgeCol) {
    const ImVec2 sz = ImGui::CalcTextSize(text);
    const ImVec2 pad(7.0f, 4.0f);
    const ImVec2 lo(at.x, at.y);
    const ImVec2 hi(at.x + sz.x + pad.x * 2.0f, at.y + sz.y + pad.y * 2.0f);
    dl->AddRectFilled(lo, hi, kPanel, 4.0f);
    dl->AddRect(lo, hi, edgeCol, 4.0f);
    dl->AddText(ImVec2(lo.x + pad.x, lo.y + pad.y), textCol, text);
    return ImVec2(hi.x - lo.x, hi.y - lo.y);
  };

  // ── Drop badge: follows the cursor, which is the whole reason this exists ──
  if (!rt->dropHint.empty()) {
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(rt->window->handle(), &mx, &my);
    badge(ImVec2(float(mx) + 16.0f, float(my) + 16.0f), rt->dropHint.c_str(), kAmber, kEdge);
  }

  // ── World-space object labels ──
  if (rt->overlayFlags & 2u) {
    const glm::mat4 viewProj = rt->scene.camera.viewProj();
    for (size_t i = 0; i < rt->scene.objects.size(); ++i) {
      const auto& obj = rt->scene.objects[i];
      if (!obj.visible || obj.name.empty()) continue;

      glm::vec3 lo, hi;
      if (!objectLocalBounds(obj, lo, hi)) continue;
      // Label sits above the object's bounding box, not at its origin, so it does not sit inside
      // the geometry it names.
      const glm::vec3 topLocal((lo.x + hi.x) * 0.5f, hi.y, (lo.z + hi.z) * 0.5f);
      const glm::vec4 clip = viewProj * obj.worldMatrix * glm::vec4(topLocal, 1.0f);
      if (clip.w <= 1e-4f) continue;

      const glm::vec3 ndc = glm::vec3(clip) / clip.w;
      if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f) continue;

      const ImVec2 at((ndc.x * 0.5f + 0.5f) * screen.x,
                      (0.5f - ndc.y * 0.5f) * screen.y - 24.0f);
      const bool selected = int(i) == rt->selectedObject;
      const ImVec2 sz = ImGui::CalcTextSize(obj.name.c_str());
      badge(ImVec2(at.x - sz.x * 0.5f - 7.0f, at.y), obj.name.c_str(),
            selected ? kAmber : kInkDim, selected ? kEdge : IM_COL32(36, 36, 45, 255));
    }
  }

  // ── Viewport HUD, bottom-left ──
  if (rt->overlayFlags & 1u) {
    const glm::vec3 eye = rt->scene.camera.position();
    char line[256];
    float y = screen.y - 30.0f;

    std::snprintf(line, sizeof(line), "cam  %.1f  %.1f  %.1f", eye.x, eye.y, eye.z);
    badge(ImVec2(12.0f, y), line, kInkDim, IM_COL32(36, 36, 45, 255));

    if (rt->selectedObject >= 0 && rt->selectedObject < int(rt->scene.objects.size())) {
      const auto& obj = rt->scene.objects[size_t(rt->selectedObject)];
      const glm::vec3 p = obj.transform.translation;
      std::snprintf(line, sizeof(line), "#%d  %s    %.2f  %.2f  %.2f", rt->selectedObject,
                    obj.name.empty() ? "(unnamed)" : obj.name.c_str(), p.x, p.y, p.z);
      y -= 26.0f;
      badge(ImVec2(12.0f, y), line, kInk, kEdge);
    }
  }

  // ── Gizmo readout: the manipulated values, next to the handles ──
  if ((rt->overlayFlags & 4u) && rt->gizmoBlocking && rt->selectedObject >= 0 &&
      rt->selectedObject < int(rt->scene.objects.size())) {
    const auto& t = rt->scene.objects[size_t(rt->selectedObject)].transform;
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    char line[192];
    switch (rt->gizmoOp) {
      case tucano::DebugUI::GizmoOp::Rotate:
        std::snprintf(line, sizeof(line), "rot  %.1f  %.1f  %.1f", euler.x, euler.y, euler.z);
        break;
      case tucano::DebugUI::GizmoOp::Scale:
        std::snprintf(line, sizeof(line), "scale  %.3f  %.3f  %.3f", t.scale.x, t.scale.y, t.scale.z);
        break;
      default:
        std::snprintf(line, sizeof(line), "pos  %.2f  %.2f  %.2f", t.translation.x,
                      t.translation.y, t.translation.z);
        break;
    }
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(rt->window->handle(), &mx, &my);
    badge(ImVec2(float(mx) + 20.0f, float(my) - 30.0f), line, kAmber, kEdge);
  }
}

// Draws the ImGuizmo handles for the selection and writes the manipulated matrix back into the
// object's transform. Must run between DebugUI::beginFrame() and endFrame().
void updateGizmo(TucanoRuntime* rt) {
  rt->gizmoBlocking = false;
  if (!rt->gizmoEnabled || !rt->debugUI) return;
  if (rt->selectedObject < 0 || rt->selectedObject >= int(rt->scene.objects.size())) return;

  auto& obj = rt->scene.objects[size_t(rt->selectedObject)];
  glm::mat4 model = obj.worldMatrix;
  const bool using_ = rt->debugUI->drawTransformGizmo(
      rt->scene.camera.view(), rt->scene.camera.proj(), model, rt->gizmoOp, rt->gizmoWorldSpace,
      rt->gizmoSnap, rt->viewportWidth, rt->viewportHeight);

  rt->gizmoBlocking = using_ || rt->debugUI->gizmoHovered();
  if (!using_) return;

  // Keep transform (what the inspector reads) authoritative by decomposing rather than storing the
  // matrix alone; skew/perspective are discarded, which is fine for editor-authored objects.
  glm::vec3 scale, translation, skew;
  glm::quat rotation;
  glm::vec4 perspective;
  if (glm::decompose(model, scale, rotation, translation, skew, perspective)) {
    obj.transform.translation = translation;
    obj.transform.rotation = rotation;
    obj.transform.scale = scale;
    obj.worldMatrix = obj.transform.matrix();
  } else {
    obj.worldMatrix = model;
  }
}

void updateEditorCamera(TucanoRuntime* rt) {
  auto& cam = rt->scene.camera;
  auto& in = *rt->input;

  const float dt = rt->deltaSeconds;
  const bool look = in.mouseDown(GLFW_MOUSE_BUTTON_RIGHT);

  // Selection and dragging only while not flying, so RMB-look never nudges geometry.
  if (!look) {
    updateObjectDrag(rt);
    // W/E/R switch gizmo mode, as in Unity — but only when RMB isn't held, where they mean fly.
    if (in.keyPressed(GLFW_KEY_W)) rt->gizmoOp = tucano::DebugUI::GizmoOp::Translate;
    if (in.keyPressed(GLFW_KEY_E)) rt->gizmoOp = tucano::DebugUI::GizmoOp::Rotate;
    if (in.keyPressed(GLFW_KEY_R)) rt->gizmoOp = tucano::DebugUI::GizmoOp::Scale;
    if (in.keyPressed(GLFW_KEY_X)) rt->gizmoWorldSpace = !rt->gizmoWorldSpace;
  } else {
    rt->dragging = false;
  }

  float dx = 0.0f, dy = 0.0f;
  in.mouseDelta(dx, dy);

  glm::vec3 move(0.0f);
  if (look) { // WASD only while holding RMB, like UE/Unity fly mode
    if (in.keyDown(GLFW_KEY_W)) move.z += 1.0f;
    if (in.keyDown(GLFW_KEY_S)) move.z -= 1.0f;
    if (in.keyDown(GLFW_KEY_D)) move.x += 1.0f;
    if (in.keyDown(GLFW_KEY_A)) move.x -= 1.0f;
    if (in.keyDown(GLFW_KEY_E)) move.y += 1.0f;
    if (in.keyDown(GLFW_KEY_Q)) move.y -= 1.0f;
  }
  const float speed = in.keyDown(GLFW_KEY_LEFT_SHIFT) ? 40.0f : 12.0f;

  // Scroll dollies along the view direction even without RMB.
  const float scroll = in.scrollY();
  if (std::abs(scroll) > 1e-4f) {
    move.z += scroll * 6.0f;
  }

  cam.fly(dt, move * speed, look ? dx * 0.0025f : 0.0f, look ? -dy * 0.0025f : 0.0f);

  if (rt->terrainHm) {
    float th = rt->terrainHm->sampleHeight(cam.position().x, cam.position().z);
    if (cam.position().y < th + 2.0f) {
      cam.setPosition({cam.position().x, th + 2.0f, cam.position().z});
    }
  }
}

} // namespace

TUCANO_API void tucano_runtime_set_camera_navigation(TucanoRuntime* rt, bool enabled) {
  if (rt) rt->cameraNavigation = enabled;
}
TUCANO_API bool tucano_runtime_get_camera_navigation(TucanoRuntime* rt) {
  return rt && rt->cameraNavigation;
}
TUCANO_API void tucano_runtime_frame_breakdown(TucanoRuntime* rt, float* simMs, float* vegMs,
                                               float* gpuWaitMs, float* recordMs, float* uiMs,
                                               float* presentMs) {
  if (vegMs) *vegMs = rt ? rt->phaseVegMs : 0.0f;
  if (simMs) *simMs = rt ? rt->phaseSimMs : 0.0f;
  if (gpuWaitMs) *gpuWaitMs = rt ? rt->phaseGpuWaitMs : 0.0f;
  if (recordMs) *recordMs = rt ? rt->phaseRecordMs : 0.0f;
  if (uiMs) *uiMs = rt ? rt->phaseUiMs : 0.0f;
  if (presentMs) *presentMs = rt ? rt->phasePresentMs : 0.0f;
}

TUCANO_API void tucano_runtime_set_overlay_visible(TucanoRuntime* rt, bool visible) {
  if (rt) rt->overlayVisible = visible;
}
TUCANO_API bool tucano_runtime_get_overlay_visible(TucanoRuntime* rt) {
  return rt && rt->overlayVisible;
}

TUCANO_API bool tucano_runtime_render(TucanoRuntime* rt) {
  if (!rt || !rt->device || !rt->swapChain || !rt->renderer) return false;
  if (rt->inRender) return true; // re-entered from inside pollEvents; skip rather than corrupt
  rt->inRender = true;
  struct Guard {
    TucanoRuntime* rt;
    ~Guard() { rt->inRender = false; }
  } guard{rt};
  const auto tFrameStart = std::chrono::steady_clock::now();
  try {
    using namespace tucano;
    // The runtime window is a child of the editor's Avalonia window and shares its UI thread, so
    // pump only our own messages — a full glfwPollEvents() re-dispatches Avalonia's messages from
    // inside this call and makes its dispatcher re-enter itself.
    rt->window->pollEventsEmbedded();

    // Presentation throttling must not park the caller's thread. The host calls this from its UI
    // thread, and the swapchain's frame-latency wait blocks for most of a refresh interval — the
    // UI then only ran in the gaps, which is what made the viewport's frame rate oscillate.
    // Poll instead: when the presentation engine is not ready, return and let the host do
    // something useful, and it will call back in a moment.
    // Returns false for "no frame this tick" — not an error. The host uses it to keep its frame
    // counter honest.
    if (!rt->swapChain->tryAcquireFrame()) {
      return false;
    }

    rt->input->beginFrame();
    rt->debugUI->beginFrame();
    if (rt->materialGraphOpen && rt->materialNodeEditor) {
      rt->materialNodeEditor->render();
    }

    veg::VegetationTool::instance().drawUI();

    // Async import progress bar
    if (!rt->importDone.load()) {
      ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 60), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(400, 50));
      ImGui::Begin("Importing...", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
      ImGui::ProgressBar(rt->importProgress.load(), ImVec2(380, 20));
      {
        std::lock_guard<std::mutex> lock(rt->importMutex);
        ImGui::Text("%s", rt->importStatus.c_str());
      }
      ImGui::End();
    }
    if (rt->overlayVisible) {
      rt->debugUI->drawPerfHud(rt->renderer->lastFrameMs(),
          rt->renderer->drawCalls(),
          rt->window->width(), rt->window->height());
    }

    tickClock(rt);
    updateAnimations(rt);

    tucano::LuaVM::instance().tick(rt->deltaSeconds);

    veg::SeasonSystem::instance().update(rt->deltaSeconds);
    veg::GrowthSystem::instance().update(rt->deltaSeconds);
    veg::DestructionSystem::instance().update(rt->deltaSeconds);
    veg::LODManager::instance().advanceFrame();
    veg::VegetationInteraction::instance().update(rt->deltaSeconds, 0);

    // Sample physical input once per frame, then resolve the virtual layer from it.
    rt->prevInputSnapshot = rt->inputSnapshot;
    tucano::input::fillSnapshotFromPlatform(*rt->input, rt->inputSnapshot);
    rt->virtualInput.update(rt->inputSnapshot);

    updatePlay(rt);

    // Terrain sculpt mode
    if (rt->terrainSculptActive && rt->terrainHm && rt->terrainBrushes && rt->terrainComp) {
      double mx, my;
      glfwGetCursorPos(rt->window->handle(), &mx, &my);
      float ndcX = float(mx) / float(rt->window->width()) * 2.0f - 1.0f;
      float ndcY = 1.0f - float(my) / float(rt->window->height()) * 2.0f;

      glm::mat4 invVP = glm::inverse(rt->scene.camera.viewProj());
      glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
      glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
      nearP /= nearP.w; farP /= farP.w;
      glm::vec3 origin(nearP);
      glm::vec3 dir = glm::normalize(glm::vec3(farP) - origin);
      float maxDist = glm::length(glm::vec3(farP) - origin);

      bool hitTerrain = false;
      float hitX = 0, hitZ = 0;
      float step = 1.0f;
      float t = 0.0f;
      for (; t < maxDist; t += step) {
        glm::vec3 p = origin + dir * t;
        float h = rt->terrainHm->sampleHeight(p.x, p.z);
        if (p.y < h) {
          float lo = t - step, hi = t;
          for (int i = 0; i < 8; ++i) {
            float mid = (lo + hi) * 0.5f;
            glm::vec3 mp = origin + dir * mid;
            if (mp.y < rt->terrainHm->sampleHeight(mp.x, mp.z)) hi = mid;
            else lo = mid;
          }
          glm::vec3 hitP = origin + dir * (lo + hi) * 0.5f;
          hitX = hitP.x; hitZ = hitP.z;
          hitTerrain = true;
          break;
        }
      }

      if (hitTerrain) {
        auto tool = static_cast<tucano::terrain::BrushTool>(rt->terrainBrushTool);
        bool lmb = rt->input->mouseDown(GLFW_MOUSE_BUTTON_LEFT);
        if (lmb) {
          rt->terrainBrushes->applyStroke(*rt->terrainHm, hitX, hitZ,
              rt->terrainBrushRadius, rt->terrainBrushStrength, tool);
          rt->terrainHm->uploadToGPU(*rt->device);
          rt->terrainComp->regenerateMesh(*rt->device);
          if (rt->terrainObjectIndex >= 0 && size_t(rt->terrainObjectIndex) < rt->scene.objects.size()) {
            rt->scene.objects[rt->terrainObjectIndex].mesh = rt->terrainComp->mesh();
          }
        }

        float centerH = rt->terrainHm->sampleHeight(hitX, hitZ);
        glm::vec4 clipPt = rt->scene.camera.viewProj() * glm::vec4(hitX, centerH + 0.15f, hitZ, 1.0f);
        if (glm::abs(clipPt.w) > 1e-6f) {
          float csx = (clipPt.x / clipPt.w * 0.5f + 0.5f) * float(rt->window->width());
          float csy = (1.0f - (clipPt.y / clipPt.w * 0.5f + 0.5f)) * float(rt->window->height());
          ImDrawList* dl = ImGui::GetForegroundDrawList();
          dl->AddCircleFilled(ImVec2(csx, csy), 5.0f, IM_COL32(255, 255, 180, 255));

          const int segs = 32;
          ImVec2 pts[segs]; int valid = 0;
          for (int i = 0; i < segs; ++i) {
            float a = float(i) / segs * 6.2831853f;
            float wx = hitX + std::cos(a) * rt->terrainBrushRadius;
            float wz = hitZ + std::sin(a) * rt->terrainBrushRadius;
            float h = rt->terrainHm->sampleHeight(wx, wz);
            glm::vec4 cp = rt->scene.camera.viewProj() * glm::vec4(wx, h + 0.15f, wz, 1.0f);
            if (glm::abs(cp.w) > 1e-6f) {
              float sx = (cp.x / cp.w * 0.5f + 0.5f) * float(rt->window->width());
              float sy = (1.0f - (cp.y / cp.w * 0.5f + 0.5f)) * float(rt->window->height());
              pts[valid++] = ImVec2(sx, sy);
            }
          }
          if (valid >= 3) {
            dl->AddConvexPolyFilled(pts, valid, IM_COL32(255, 255, 255, 35));
            dl->AddPolyline(pts, valid, IM_COL32(255, 255, 200, 180), ImDrawFlags_Closed, 2.0f);
          }
        }
      }
    } else {
      updateGizmo(rt);
      drawEditorOverlay(rt);
    }

    // Draw terrain preview grid when landscape mode is active but no terrain loaded yet
    if (rt->terrainSculptActive && !rt->terrainHm) {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      float gridSize = 1024.0f;
      float step = 64.0f;
      glm::mat4 vp = rt->scene.camera.viewProj();
      auto project = [&](float wx, float wy, float wz, float& sx, float& sy) {
        glm::vec4 cp = vp * glm::vec4(wx, wy, wz, 1.0f);
        if (glm::abs(cp.w) < 1e-6f) return false;
        sx = (cp.x / cp.w * 0.5f + 0.5f) * float(rt->window->width());
        sy = (1.0f - (cp.y / cp.w * 0.5f + 0.5f)) * float(rt->window->height());
        return sx >= -200 && sx < float(rt->window->width()) + 200 &&
               sy >= -200 && sy < float(rt->window->height()) + 200;
      };

      for (float x = 0; x <= gridSize; x += step) {
        float s0x, s0z, s1x, s1z;
        if (project(x, 0, 0, s0x, s0z) && project(x, 0, gridSize, s1x, s1z)) {
          dl->AddLine(ImVec2(s0x, s0z), ImVec2(s1x, s1z), IM_COL32(100, 100, 100, 60), 1.0f);
        }
        if (project(0, 0, x, s0x, s0z) && project(gridSize, 0, x, s1x, s1z)) {
          dl->AddLine(ImVec2(s0x, s0z), ImVec2(s1x, s1z), IM_COL32(100, 100, 100, 60), 1.0f);
        }
      }
    }

    // Vegetation paint: place/erase plants (or density) under the cursor.
    {
      using namespace tucano;
      auto& paint = veg::VegetationSystem::instance().paint();
      if (paint.enabled && rt->input && !ImGui::GetIO().WantCaptureMouse) {
        double mx, my;
        glfwGetCursorPos(rt->window->handle(), &mx, &my);
        float ndcX = float(mx) / float(rt->window->width()) * 2.0f - 1.0f;
        float ndcY = 1.0f - float(my) / float(rt->window->height()) * 2.0f;

        glm::mat4 invVP = glm::inverse(rt->scene.camera.viewProj());
        glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farP = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearP /= nearP.w;
        farP /= farP.w;
        glm::vec3 origin(nearP);
        glm::vec3 dir = glm::normalize(glm::vec3(farP) - origin);
        float maxDist = glm::length(glm::vec3(farP) - origin);

        bool hit = false;
        float hitX = 0, hitY = 0, hitZ = 0;
        if (rt->terrainHm) {
          float step = 1.0f;
          for (float t = 0.0f; t < maxDist; t += step) {
            glm::vec3 p = origin + dir * t;
            float h = rt->terrainHm->sampleHeight(p.x, p.z);
            if (p.y < h) {
              float lo = t - step, hi = t;
              for (int i = 0; i < 8; ++i) {
                float mid = (lo + hi) * 0.5f;
                glm::vec3 mp = origin + dir * mid;
                if (mp.y < rt->terrainHm->sampleHeight(mp.x, mp.z)) hi = mid;
                else lo = mid;
              }
              glm::vec3 hitP = origin + dir * (lo + hi) * 0.5f;
              hitX = hitP.x;
              hitZ = hitP.z;
              hitY = rt->terrainHm->sampleHeight(hitX, hitZ);
              hit = true;
              break;
            }
          }
        } else if (std::abs(dir.y) > 1e-5f) {
          float tPlane = -origin.y / dir.y;
          if (tPlane > 0.0f && tPlane < maxDist) {
            glm::vec3 hitP = origin + dir * tPlane;
            hitX = hitP.x;
            hitY = 0.0f;
            hitZ = hitP.z;
            hit = true;
          }
        }

        if (hit) {
          auto& sys = veg::VegetationSystem::instance();
          const bool lmb = rt->input->mouseDown(GLFW_MOUSE_BUTTON_LEFT);
          const bool click = rt->input->mousePressed(GLFW_MOUSE_BUTTON_LEFT);
          std::function<float(float, float)> heightFn;
          if (rt->terrainHm) {
            heightFn = [rt](float x, float z) { return rt->terrainHm->sampleHeight(x, z); };
          }

          using Mode = veg::VegetationPaintState::Mode;
          if (paint.mode == Mode::PlaceSingle && click && sys.typeCount() > 0) {
            sys.placeAt({hitX, hitY, hitZ}, uint32_t(paint.typeId));
          } else if (lmb && sys.typeCount() > 0) {
            const float stroke = paint.brushStrength * std::min(rt->deltaSeconds * 6.0f, 1.0f);
            if (paint.mode == Mode::PlaceBrush) {
              sys.paintBrush({hitX, hitY, hitZ}, paint.brushRadius, uint32_t(paint.typeId), stroke,
                             paint.plantsPerSquareMeter, heightFn);
            } else if (paint.mode == Mode::EraseBrush) {
              sys.eraseBrush({hitX, hitY, hitZ}, paint.brushRadius);
            } else if (paint.mode == Mode::DensityPaint && sys.densityMap()) {
              sys.densityMap()->paintBrush(hitX, hitZ, paint.brushRadius, stroke);
            } else if (paint.mode == Mode::DensityErase && sys.densityMap()) {
              sys.densityMap()->eraseBrush(hitX, hitZ, paint.brushRadius, stroke);
            }
          }

          ImDrawList* dl = ImGui::GetForegroundDrawList();
          glm::vec4 clipPt = rt->scene.camera.viewProj() * glm::vec4(hitX, hitY + 0.15f, hitZ, 1.0f);
          if (glm::abs(clipPt.w) > 1e-6f) {
            float csx = (clipPt.x / clipPt.w * 0.5f + 0.5f) * float(rt->window->width());
            float csy = (1.0f - (clipPt.y / clipPt.w * 0.5f + 0.5f)) * float(rt->window->height());
            dl->AddCircleFilled(ImVec2(csx, csy), 4.0f, IM_COL32(120, 220, 120, 255));
            const int segs = 32;
            ImVec2 pts[32];
            int valid = 0;
            for (int i = 0; i < segs; ++i) {
              float a = float(i) / float(segs) * 6.2831853f;
              float wx = hitX + std::cos(a) * paint.brushRadius;
              float wz = hitZ + std::sin(a) * paint.brushRadius;
              float h = hitY;
              if (rt->terrainHm) h = rt->terrainHm->sampleHeight(wx, wz);
              glm::vec4 cp = rt->scene.camera.viewProj() * glm::vec4(wx, h + 0.15f, wz, 1.0f);
              if (glm::abs(cp.w) > 1e-6f) {
                pts[valid++] =
                    ImVec2((cp.x / cp.w * 0.5f + 0.5f) * float(rt->window->width()),
                           (1.0f - (cp.y / cp.w * 0.5f + 0.5f)) * float(rt->window->height()));
              }
            }
            if (valid >= 3)
              dl->AddPolyline(pts, valid, IM_COL32(140, 255, 140, 200), ImDrawFlags_Closed, 2.0f);
          }
        }
      }
    }

    if (rt->cameraNavigation) {
      updateEditorCamera(rt);
    }

    rt->input->endFrame();

    // World streaming (WM-9): drive per frame if active.
    // NOTE: streaming is started explicitly by the World Outliner tool or via API.
    // It does NOT auto-start — production worlds are authored by artists, not procedural.
    if (rt->streamingActive && rt->streamer) {
      rt->streamObserver.position = rt->scene.camera.position();
      rt->streamer->setObservers({rt->streamObserver});
      rt->streamer->update(rt->deltaSeconds * 1000.0f);
    }

    // Phase timings. "The editor feels slow" needs a breakdown to act on: the host sees only the
    // total, which hides whether the cost is simulation, GPU work, or waiting on presentation.
    using Clock = std::chrono::steady_clock;
    const auto tSimEnd = Clock::now();

    auto* cmd = rt->device->beginFrame();
    const auto tBeginEnd = Clock::now();
    auto& bb = rt->swapChain->backBuffer();
    const auto tAcquireEnd = Clock::now();

    const auto tVegStart = Clock::now();
    if (rt->vegEnabled && rt->vegRenderer) {
      auto& vegSys = veg::VegetationSystem::instance();
      auto& paint = vegSys.paint();
      if (paint.meshDirty) {
        if (auto* t = vegSys.type(uint32_t(paint.typeId))) {
          if (!t->meshPath.empty())
            rt->vegRenderer->loadMeshForType(uint32_t(paint.typeId), t->meshPath);
        }
        paint.meshDirty = false;
      }

      veg::VegetationInteraction::instance().clearInteractionPoints();
      const glm::vec3 camPos = rt->scene.camera.position();
      veg::VegetationInteraction::instance().addInteractionPoint(camPos, 3.0f, 1.0f);

      veg::WindSystem::instance().update(rt->deltaSeconds);
      rt->vegRenderer->uploadFromSystem(vegSys, camPos, 200.0f, rt->deltaSeconds);
      veg::VegDispatch::recordDispatch(
          *rt->device, *cmd, *rt->vegRenderer, rt->scene.camera.viewProj(), camPos, 200.0f,
          rt->renderer->hizOcclusionMip(), rt->window->width(), rt->window->height());
      rt->scene.instanceClouds.clear();
      rt->vegRenderer->submitClouds(rt->scene.instanceClouds);
    }
    const auto tVegEnd = Clock::now();

    rt->renderer->render(cmd, bb, rt->scene);
    const auto tSceneEnd = Clock::now();
    rt->debugUI->endFrame(*cmd, bb);
    cmd->transition(bb, tucano::rhi::ResourceState::Present);
    const auto tRecordEnd = Clock::now();
    rt->device->endFrame(*rt->swapChain);
    const auto tPresentEnd = Clock::now();

    auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<float, std::milli>(b - a).count();
    };
    rt->phaseSimMs = ms(tFrameStart, tSimEnd);
    rt->phaseGpuWaitMs = ms(tSimEnd, tBeginEnd) + ms(tBeginEnd, tAcquireEnd);
    rt->phaseVegMs = ms(tVegStart, tVegEnd);
    rt->phaseRecordMs = ms(tVegEnd, tSceneEnd);
    rt->phaseUiMs = ms(tSceneEnd, tRecordEnd);
    rt->phasePresentMs = ms(tRecordEnd, tPresentEnd);
    return true;
  } catch (...) {
    rt->alive = false;
    return false;
  }
}

// ── Vegetation C API ──────────────────────────────────

TUCANO_API void tucano_veg_scatter(TucanoRuntime* rt, int cellX, int cellZ,
                                    uint32_t typeId, uint32_t count, uint32_t seed) {
  if (!rt) return;
  using namespace tucano;
  veg::VegetationSystem::instance().scatter(cellX, cellZ, typeId, count, seed);
}

TUCANO_API uint32_t tucano_veg_register_type(TucanoRuntime* rt, const char* name,
                                              float minScale, float maxScale,
                                              float windFlex, float windHeight,
                                              float cullDist) {
  if (!rt) return UINT32_MAX;
  using namespace tucano;
  veg::VegetationType t;
  t.name = name ? name : "Veg";
  t.minScale = minScale;
  t.maxScale = maxScale;
  t.windFlexibility = windFlex;
  t.windHeight = windHeight;
  t.cullDistance = cullDist;
  return veg::VegetationSystem::instance().registerType(t);
}

TUCANO_API uint32_t tucano_veg_register_mesh_type(TucanoRuntime* rt, const char* name,
                                                   const char* meshPath) {
  if (!rt || !name) return UINT32_MAX;
  using namespace tucano;
  veg::VegetationType t;
  t.name = name;
  t.meshPath = meshPath ? meshPath : "";
  t.proceduralKind = 2;
  uint32_t id = veg::VegetationSystem::instance().registerType(t);
  if (meshPath && meshPath[0] && rt->vegRenderer) {
    rt->vegRenderer->loadMeshForType(id, meshPath);
  } else {
    veg::VegetationSystem::instance().paint().meshDirty = true;
    veg::VegetationSystem::instance().paint().typeId = int(id);
  }
  return id;
}

TUCANO_API void tucano_veg_set_enabled(TucanoRuntime* rt, bool enabled) {
  if (!rt) return;
  rt->vegEnabled = enabled && rt->vegRenderer != nullptr;
  if (!rt->vegEnabled) rt->scene.instanceClouds.clear();
}

TUCANO_API bool tucano_veg_get_enabled(TucanoRuntime* rt) { return rt && rt->vegEnabled; }

TUCANO_API void tucano_veg_set_paint(TucanoRuntime* rt, bool enabled, int mode, int typeId,
                                      float radius, float strength) {
  if (!rt) return;
  using namespace tucano;
  auto& p = veg::VegetationSystem::instance().paint();
  p.enabled = enabled;
  p.mode = veg::VegetationPaintState::Mode(mode);
  p.typeId = typeId;
  p.brushRadius = radius;
  p.brushStrength = strength;
}

TUCANO_API void tucano_veg_set_wind(TucanoRuntime* rt, float strength, float speed) {
  if (!rt) return;
  using namespace tucano;
  veg::WindParams p;
  p.strength = strength;
  p.speed = speed;
  veg::WindSystem::instance().configure(p);
}

TUCANO_API uint32_t tucano_veg_instance_count(TucanoRuntime* rt) {
  if (!rt) return 0;
  using namespace tucano;
  return veg::VegetationSystem::instance().instanceCount();
}

TUCANO_API void tucano_veg_toggle_tool(void) {
  using namespace tucano;
  veg::VegetationTool::instance().toggle();
}

TUCANO_API bool tucano_veg_tool_visible(void) {
  using namespace tucano;
  return veg::VegetationTool::instance().isOpen();
}

TUCANO_API bool tucano_runtime_screenshot(TucanoRuntime* rt, const char* pngPath) {
  if (!rt || !rt->device || !rt->swapChain || !rt->renderer || !pngPath) return false;
  try {
    rt->window->pollEvents();
    // Run the ImGui frame too, so the capture shows what the viewport actually shows (gizmo and,
    // when enabled, the perf HUD) rather than the bare scene.
    rt->debugUI->beginFrame();
    if (rt->overlayVisible) {
      rt->debugUI->drawPerfHud(rt->renderer->lastFrameMs(), rt->renderer->drawCalls(),
                               rt->window->width(), rt->window->height());
    }
    updateGizmo(rt);
    drawEditorOverlay(rt);

    auto* cmd = rt->device->beginFrame();
    auto& bb = rt->swapChain->backBuffer();
    rt->renderer->render(cmd, bb, rt->scene);
    rt->debugUI->endFrame(*cmd, bb);
    auto shot = tucano::beginScreenshot(*rt->device, *cmd, bb);
    cmd->transition(bb, tucano::rhi::ResourceState::Present);
    rt->device->endFrame(*rt->swapChain);
    if (!shot.impl) return false;
    rt->device->waitIdle();
    tucano::finalizeScreenshot(shot, pngPath);
    return true;
  } catch (...) {
    return false;
  }
}

// ── Scene ────────────────────────────────────────────

TUCANO_API TucanoScene* tucano_scene_create(TucanoRuntime* rt) {
  if (!rt) return nullptr;
  return new TucanoScene{rt, rt->scene};
}

TUCANO_API void tucano_scene_destroy(TucanoScene* scene) {
  if (!scene) return;
  // Destroying the scene releases every mesh and texture it owns. The host tears the scene down
  // before the runtime, so the device is still alive here and still has frames in flight — drain
  // it first or the driver faults while the last frames are reading resources being freed.
  if (scene->runtime && scene->runtime->device) {
    scene->runtime->device->waitIdle();
  }
  delete scene;
}

TUCANO_API void tucano_scene_load_skylab(TucanoScene* scene) {
  if (!scene || !scene->runtime) return;
  buildCleanScene(*scene->runtime->device, scene->data);
  // buildCleanScene lays down one ground plane followed by the starting box; mirror that so the
  // source list stays 1:1 with the objects.
  auto& src = scene->runtime->sources;
  src.assign(1, ObjectSource{ObjectSource::Kind::BuiltinGround, {}});
  src.resize(scene->data.objects.size(), ObjectSource{ObjectSource::Kind::Cube, {}});
  scene->runtime->selectedObject = -1;
}

// ── Camera ───────────────────────────────────────────

TUCANO_API void tucano_camera_set_position(TucanoScene* scene, TucanoVec3 pos) {
  if (!scene) return;
  scene->data.camera.setPosition({pos.x, pos.y, pos.z});
}

TUCANO_API void tucano_camera_get_position(TucanoScene* scene, TucanoVec3* pos) {
  if (!scene || !pos) return;
  auto& p = scene->data.camera.position();
  pos->x = p.x;
  pos->y = p.y;
  pos->z = p.z;
}

TUCANO_API void tucano_camera_get_forward(TucanoScene* scene, TucanoVec3* dir) {
  if (!dir) return;
  if (!scene) {
    *dir = {0.0f, 0.0f, -1.0f};
    return;
  }
  const glm::vec3 f = scene->data.camera.forward();
  dir->x = f.x;
  dir->y = f.y;
  dir->z = f.z;
}

TUCANO_API void tucano_camera_look_at(TucanoScene* scene, TucanoVec3 target) {
  if (!scene) return;
  scene->data.camera.lookAt({target.x, target.y, target.z});
}

TUCANO_API void tucano_camera_set_fov_y_rad(TucanoScene* scene, float fovYRad) {
  if (!scene) return;
  scene->data.camera.setPerspective(
      fovYRad, 16.0f / 9.0f, scene->data.camera.nearPlane(), scene->data.camera.farPlane());
}

TUCANO_API void tucano_camera_fly(TucanoScene* scene,
                                   float dt, TucanoVec3 move,
                                   float yawDelta, float pitchDelta) {
  if (!scene) return;
  scene->data.camera.fly(dt, {move.x, move.y, move.z}, yawDelta, pitchDelta);
}

TUCANO_API void tucano_camera_set_perspective(TucanoScene* scene,
                                               float fovYRad, float aspect,
                                               float zNear, float zFar) {
  if (!scene) return;
  scene->data.camera.setPerspective(fovYRad, aspect, zNear, zFar);
}

// ── Scene objects ────────────────────────────────────

TUCANO_API uint32_t tucano_scene_object_count(TucanoScene* scene) {
  if (!scene) return 0;
  return static_cast<uint32_t>(scene->data.objects.size());
}

TUCANO_API TucanoVec3 tucano_scene_object_position(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.objects.size()) return {0, 0, 0};
  auto& t = scene->data.objects[index].transform.translation;
  return {t.x, t.y, t.z};
}

TUCANO_API const char* tucano_scene_object_name(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.objects.size()) return "";
  return scene->data.objects[index].name.c_str();
}

// ── Scene authoring ──────────────────────────────────

namespace {

} // namespace

TUCANO_API uint32_t tucano_scene_spawn_primitive(TucanoScene* scene, TucanoPrimitive prim, TucanoVec3 pos,
                                                 float scale) {
  if (!scene || !scene->runtime || !scene->runtime->device) return 0xFFFFFFFFu;
  auto& rt = *scene->runtime;
  auto& dev = *rt.device;
  if (scale <= 0.0f) scale = 1.0f;

  // Cache one mesh per primitive type — spawning must not rebuild geometry every call.
  std::shared_ptr<tucano::Mesh> mesh;
  const char* baseName = "Object";
  ObjectSource source{ObjectSource::Kind::Cube, {}};
  switch (prim) {
    case TUCANO_PRIM_SPHERE:
      source.kind = ObjectSource::Kind::Sphere;
      if (!rt.primSphere) rt.primSphere = makeMarkerSphere(dev, 0.5f, 24);
      mesh = rt.primSphere;
      baseName = "Sphere";
      break;
    case TUCANO_PRIM_PLANE:
      source.kind = ObjectSource::Kind::Plane;
      if (!rt.primPlane) rt.primPlane = makeGroundPlane(dev, 0.5f, 2);
      mesh = rt.primPlane;
      baseName = "Plane";
      break;
    case TUCANO_PRIM_CUBE:
    default:
      if (!rt.primCube) rt.primCube = makeCubeMesh(dev, 1.0f);
      mesh = rt.primCube;
      baseName = "Cube";
      break;
  }
  if (!mesh) return 0xFFFFFFFFu;

  auto mat = std::make_shared<tucano::Material>();
  mat->name = baseName;
  // Grid dev-texture rather than flat grey: without it a blocked-out level gives no sense of
  // scale, orientation or tiling, and every primitive reads as the same untextured blob.
  mat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  mat->albedo = tucano::devtex::defaultAlbedo(dev);
  mat->normal = tucano::devtex::defaultNormal(dev);
  mat->roughnessFactor = 0.72f;
  mat->metallicFactor = 0.0f;

  tucano::RenderObject obj;
  obj.name = std::string(baseName) + std::to_string(scene->data.objects.size());
  obj.mesh = mesh;
  obj.materials = {mat};
  // transform is the authoring source of truth (what the inspector reads); worldMatrix is what the
  // renderer consumes. Keep the two in step on every mutation.
  obj.transform.translation = {pos.x, pos.y, pos.z};
  obj.transform.scale = glm::vec3(scale);
  obj.worldMatrix = obj.transform.matrix();
  scene->data.objects.push_back(std::move(obj));
  rt.sources.push_back(std::move(source));
  return uint32_t(scene->data.objects.size() - 1);
}

// ── Animation (Phase I-1) ────────────────────────────

namespace {

ObjectAnimation* animationFor(TucanoScene* scene, uint32_t object) {
  if (!scene || !scene->runtime) return nullptr;
  auto it = scene->runtime->animations.find(object);
  return it == scene->runtime->animations.end() ? nullptr : &it->second;
}

} // namespace

TUCANO_API uint32_t tucano_scene_import_animated_mesh(TucanoScene* scene, const char* path,
                                                      TucanoVec3 position, float scale) {
  if (!scene || !scene->runtime || !path) return 0xFFFFFFFFu;

  // Geometry first — this is also the fallback when the file carries no rig.
  const uint32_t first = tucano_scene_import_mesh(scene, path, position, scale);
  if (first == 0xFFFFFFFFu) return first;

  std::string err;
  auto asset = tucano::loadGLTFSkinnedAsset(path, &err);
  if (!asset.valid()) return first; // static mesh: nothing more to do

  // Attach the rig to every object the import produced; a skinned glTF usually yields one, but a
  // multi-primitive mesh yields several sharing the same skeleton.
  auto& rt = *scene->runtime;
  for (uint32_t i = first; i < uint32_t(scene->data.objects.size()); ++i) {
    ObjectAnimation animation;
    animation.skeleton = asset.skeleton;
    animation.clips = asset.clips;
    for (auto& clip : animation.clips) {
      if (clip) clip->resolveBones(animation.skeleton);
    }
    animation.pose = animation.skeleton.bindPose();
    // Bind pose immediately, so the mesh renders correctly before anything is played.
    animation.skeleton.computeSkinningMatrices(animation.pose,
                                               scene->data.objects[i].skinningMatrices);
    rt.animations[i] = std::move(animation);
  }
  return first;
}

TUCANO_API uint32_t tucano_anim_clip_count(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a ? uint32_t(a->clips.size()) : 0;
}

TUCANO_API const char* tucano_anim_clip_name(TucanoScene* scene, uint32_t object, uint32_t clip) {
  auto* a = animationFor(scene, object);
  if (!a || clip >= a->clips.size() || !a->clips[clip]) return "";
  return a->clips[clip]->name().c_str();
}

TUCANO_API float tucano_anim_clip_duration(TucanoScene* scene, uint32_t object, uint32_t clip) {
  auto* a = animationFor(scene, object);
  if (!a || clip >= a->clips.size() || !a->clips[clip]) return 0.0f;
  return a->clips[clip]->duration();
}

TUCANO_API uint32_t tucano_anim_bone_count(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a ? uint32_t(a->skeleton.boneCount()) : 0;
}

TUCANO_API void tucano_anim_play(TucanoScene* scene, uint32_t object, uint32_t clip, bool loop) {
  auto* a = animationFor(scene, object);
  if (!a || clip >= a->clips.size()) return;
  a->player.play(a->clips[clip].get(), loop);
  a->currentClip = int(clip);
}

TUCANO_API void tucano_anim_stop(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  if (!a || !scene || object >= scene->data.objects.size()) return;
  a->player.stop();
  a->currentClip = -1;
  // Snap back to the bind pose so stopping looks deliberate rather than frozen mid-motion.
  a->pose = a->skeleton.bindPose();
  a->skeleton.computeSkinningMatrices(a->pose, scene->data.objects[object].skinningMatrices);
}

TUCANO_API void tucano_anim_pause(TucanoScene* scene, uint32_t object, bool paused) {
  auto* a = animationFor(scene, object);
  if (!a) return;
  if (paused) a->player.pause(); else a->player.resume();
}

TUCANO_API bool tucano_anim_is_playing(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a && a->player.isPlaying();
}

TUCANO_API int32_t tucano_anim_current_clip(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a ? a->currentClip : -1;
}

TUCANO_API float tucano_anim_get_time(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a ? a->player.time() : 0.0f;
}

TUCANO_API void tucano_anim_set_time(TucanoScene* scene, uint32_t object, float time) {
  auto* a = animationFor(scene, object);
  if (!a || !scene || object >= scene->data.objects.size()) return;
  a->player.setTime(time);
  // Re-evaluate now so scrubbing the timeline updates the viewport even while paused.
  a->pose = a->skeleton.bindPose();
  a->player.evaluate(a->pose);
  a->skeleton.computeSkinningMatrices(a->pose, scene->data.objects[object].skinningMatrices);
}

TUCANO_API float tucano_anim_get_speed(TucanoScene* scene, uint32_t object) {
  auto* a = animationFor(scene, object);
  return a ? a->player.speed() : 1.0f;
}

TUCANO_API void tucano_anim_set_speed(TucanoScene* scene, uint32_t object, float speed) {
  auto* a = animationFor(scene, object);
  if (a) a->player.setSpeed(speed);
}

// ── Skinning (Phase I-1) ─────────────────────────────

TUCANO_API void tucano_scene_set_skinning_matrices(TucanoScene* scene, uint32_t index,
                                                   const float* matrices, uint32_t count) {
  if (!scene || index >= scene->data.objects.size()) return;
  auto& dst = scene->data.objects[index].skinningMatrices;
  if (!matrices || count == 0) {
    dst.clear();
    return;
  }
  dst.resize(count);
  std::memcpy(dst.data(), matrices, size_t(count) * sizeof(glm::mat4));
}

TUCANO_API uint32_t tucano_scene_get_skinning_bone_count(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.objects.size()) return 0;
  return uint32_t(scene->data.objects[index].skinningMatrices.size());
}

TUCANO_API uint32_t tucano_scene_spawn_skinned_test(TucanoScene* scene, TucanoVec3 pos,
                                                    uint32_t segments) {
  if (!scene || !scene->runtime || !scene->runtime->device) return 0xFFFFFFFFu;
  segments = glm::clamp(segments, 2u, 64u);

  auto& rt = *scene->runtime;
  auto& dev = *rt.device;

  // A vertical box divided into rings. Ring i is bound fully to bone i, so bending bone N rotates
  // everything from that ring up — the classic bend test for a skinning pipeline.
  std::vector<tucano::Vertex> verts;
  std::vector<uint32_t> indices;
  const float halfWidth = 0.25f;
  const float segmentHeight = 1.0f;

  for (uint32_t ring = 0; ring <= segments; ++ring) {
    const float y = float(ring) * segmentHeight;
    const uint8_t bone = uint8_t(std::min(ring, segments - 1));
    const uint8_t idx[4] = {bone, 0, 0, 0};

    // Four corners per ring.
    const glm::vec3 corners[4] = {
        {-halfWidth, y, -halfWidth}, {halfWidth, y, -halfWidth},
        {halfWidth, y, halfWidth},   {-halfWidth, y, halfWidth},
    };
    for (int c = 0; c < 4; ++c) {
      tucano::Vertex v{};
      v.position = corners[c];
      v.normal = glm::normalize(glm::vec3(corners[c].x, 0.0f, corners[c].z));
      v.tangent = {0, 1, 0, 1};
      v.uv = {float(c) * 0.25f, float(ring) / float(segments)};
      v.color = {1, 1, 1, 1};
      v.setSkinning(idx, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
      verts.push_back(v);
    }
  }

  for (uint32_t ring = 0; ring < segments; ++ring) {
    const uint32_t a = ring * 4;
    const uint32_t b = (ring + 1) * 4;
    for (uint32_t side = 0; side < 4; ++side) {
      const uint32_t s0 = side;
      const uint32_t s1 = (side + 1) % 4;
      indices.insert(indices.end(), {a + s0, b + s0, b + s1, a + s0, b + s1, a + s1});
    }
  }

  tucano::SubMesh sub{};
  sub.indexCount = uint32_t(indices.size());
  sub.aabbMin = {-halfWidth, 0.0f, -halfWidth};
  sub.aabbMax = {halfWidth, float(segments) * segmentHeight, halfWidth};

  auto mat = std::make_shared<tucano::Material>();
  mat->name = "SkinnedTest";
  mat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  mat->albedo = tucano::devtex::defaultAlbedo(dev);
  mat->normal = tucano::devtex::defaultNormal(dev);
  mat->roughnessFactor = 0.6f;
  mat->metallicFactor = 0.0f;

  tucano::RenderObject obj;
  obj.name = "SkinnedTest";
  obj.mesh = tucano::Mesh::create(dev, verts, indices, {sub});
  obj.materials = {mat};
  obj.transform.translation = {pos.x, pos.y, pos.z};
  obj.worldMatrix = obj.transform.matrix();
  // Start in bind pose so the object is visible before any animation drives it.
  obj.skinningMatrices.assign(segments, glm::mat4(1.0f));

  scene->data.objects.push_back(std::move(obj));
  rt.sources.push_back(ObjectSource{ObjectSource::Kind::Cube, {}});
  return uint32_t(scene->data.objects.size() - 1);
}

TUCANO_API void tucano_scene_set_object_position(TucanoScene* scene, uint32_t index, TucanoVec3 pos) {
  if (!scene || index >= scene->data.objects.size()) return;
  auto& obj = scene->data.objects[index];
  obj.transform.translation = {pos.x, pos.y, pos.z};
  obj.worldMatrix = obj.transform.matrix();
}

TUCANO_API void tucano_scene_set_object_color(TucanoScene* scene, uint32_t index, TucanoVec3 rgb) {
  if (!scene || index >= scene->data.objects.size()) return;
  auto& mats = scene->data.objects[index].materials;
  if (mats.empty() || !mats[0]) return;
  // The selected object's material holds the amber tint, not its real colour — write through to
  // the stored original and let applySelection re-tint, or the tint would be baked in permanently.
  if (scene->runtime && scene->runtime->selectedObject == int(index)) {
    scene->runtime->selectedBaseColor = {rgb.x, rgb.y, rgb.z};
    const int sel = scene->runtime->selectedObject;
    scene->runtime->selectedObject = -1; // force applySelection to redo the tint
    applySelection(scene->runtime, sel);
    return;
  }
  mats[0]->baseColorFactor = {rgb.x, rgb.y, rgb.z, 1.0f};
}

TUCANO_API void tucano_scene_remove_object(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.objects.size()) return;

  // Erasing the object runs its destructor, which releases the mesh's vertex/index buffers and
  // its textures. Those can still be referenced by command lists the GPU has not finished — with
  // two frames in flight, deleting something that was on screen a moment ago faults inside the
  // driver. Drain first: deletion is a rare, user-initiated action, so the stall costs nothing.
  if (scene->runtime && scene->runtime->device) {
    scene->runtime->device->waitIdle();
  }

  if (auto* rt = scene->runtime) {
    // Restore the tint before the object goes away, then keep the selection pointing at the same
    // object now that the indices after it have shifted down.
    if (rt->selectedObject == int(index)) {
      applySelection(rt, -1);
    } else if (rt->selectedObject > int(index)) {
      --rt->selectedObject;
    }
    rt->dragging = false;
    if (index < rt->sources.size()) rt->sources.erase(rt->sources.begin() + index);
  }
  scene->data.objects.erase(scene->data.objects.begin() + index);
}

TUCANO_API float tucano_scene_fit_objects(TucanoScene* scene, uint32_t firstIndex, uint32_t count,
                                          float maxSize, TucanoVec3 anchor) {
  if (!scene || count == 0 || maxSize <= 0.0f) return 1.0f;
  auto& objects = scene->data.objects;
  if (firstIndex >= objects.size()) return 1.0f;
  const uint32_t last = std::min<uint32_t>(firstIndex + count, uint32_t(objects.size()));

  // World-space bounds of the whole import: each object's local AABB corners pushed through its
  // world matrix, because a rotated node's axis-aligned local box is not axis-aligned in world.
  glm::vec3 lo(std::numeric_limits<float>::max());
  glm::vec3 hi(std::numeric_limits<float>::lowest());
  bool any = false;
  for (uint32_t i = firstIndex; i < last; ++i) {
    glm::vec3 l, h;
    if (!objectLocalBounds(objects[i], l, h)) continue;
    const glm::mat4& m = objects[i].worldMatrix;
    for (int c = 0; c < 8; ++c) {
      const glm::vec3 corner((c & 1) ? h.x : l.x, (c & 2) ? h.y : l.y, (c & 4) ? h.z : l.z);
      const glm::vec3 w = glm::vec3(m * glm::vec4(corner, 1.0f));
      lo = glm::min(lo, w);
      hi = glm::max(hi, w);
    }
    any = true;
  }
  if (!any) return 1.0f;

  const glm::vec3 extent = hi - lo;
  const float largest = std::max({extent.x, extent.y, extent.z});
  if (largest <= maxSize || largest <= 1e-5f) return 1.0f; // already small enough: leave it alone

  // Scale about the drop point so the model stays where it was put, and sit it on that point
  // rather than centred on it — dropped objects should look like they are standing there.
  const float factor = maxSize / largest;
  const glm::vec3 pivot{anchor.x, anchor.y, anchor.z};
  const glm::vec3 centre = (lo + hi) * 0.5f;
  const glm::vec3 groundOffset(0.0f, (centre.y - lo.y) * factor, 0.0f);

  for (uint32_t i = firstIndex; i < last; ++i) {
    auto& obj = objects[i];
    const glm::mat4 fit = glm::translate(glm::mat4(1.0f), pivot + groundOffset) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(factor)) *
                          glm::translate(glm::mat4(1.0f), -centre);
    obj.worldMatrix = fit * obj.worldMatrix;
    obj.transform.translation = glm::vec3(obj.worldMatrix[3]);
    obj.transform.scale *= factor;
  }
  return factor;
}

TUCANO_API void tucano_scene_clear(TucanoScene* scene) {
  if (!scene) return;
  // Same hazard as remove_object: these destructors free GPU resources that may be in flight.
  if (scene->runtime && scene->runtime->device) {
    scene->runtime->device->waitIdle();
  }
  scene->data.objects.clear();
  if (scene->runtime) {
    scene->runtime->sources.clear();
    scene->runtime->selectedObject = -1;
    scene->runtime->dragging = false;
  }
}

// ── Selection & transforms ───────────────────────────

TUCANO_API int32_t tucano_scene_get_selected(TucanoScene* scene) {
  if (!scene || !scene->runtime) return -1;
  const int sel = scene->runtime->selectedObject;
  return sel < int(scene->data.objects.size()) ? int32_t(sel) : -1;
}

TUCANO_API void tucano_scene_set_selected(TucanoScene* scene, int32_t index) {
  if (!scene || !scene->runtime) return;
  if (index < 0 || index >= int32_t(scene->data.objects.size())) index = -1;
  applySelection(scene->runtime, int(index));
}

TUCANO_API void tucano_scene_get_object_transform(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3* position, TucanoVec3* eulerRad,
                                                  TucanoVec3* scale) {
  if (!scene || index >= scene->data.objects.size()) return;
  const auto& t = scene->data.objects[index].transform;
  if (position) *position = {t.translation.x, t.translation.y, t.translation.z};
  if (scale) *scale = {t.scale.x, t.scale.y, t.scale.z};
  if (eulerRad) {
    const glm::vec3 e = glm::eulerAngles(t.rotation);
    *eulerRad = {e.x, e.y, e.z};
  }
}

TUCANO_API void tucano_scene_set_object_transform(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3 position, TucanoVec3 eulerRad,
                                                  TucanoVec3 scale) {
  if (!scene || index >= scene->data.objects.size()) return;
  auto& obj = scene->data.objects[index];
  obj.transform.translation = {position.x, position.y, position.z};
  obj.transform.rotation = glm::quat(glm::vec3(eulerRad.x, eulerRad.y, eulerRad.z));
  // A zero axis would collapse the mesh and make it unpickable, with no way back from the UI.
  obj.transform.scale = {scale.x != 0.0f ? scale.x : 1.0f,
                         scale.y != 0.0f ? scale.y : 1.0f,
                         scale.z != 0.0f ? scale.z : 1.0f};
  obj.worldMatrix = obj.transform.matrix();
}

TUCANO_API void tucano_scene_get_object_color(TucanoScene* scene, uint32_t index, TucanoVec3* rgb) {
  if (!scene || !rgb || index >= scene->data.objects.size()) return;
  if (scene->runtime && scene->runtime->selectedObject == int(index)) {
    const glm::vec3& c = scene->runtime->selectedBaseColor; // untinted
    *rgb = {c.r, c.g, c.b};
    return;
  }
  const auto& mats = scene->data.objects[index].materials;
  if (mats.empty() || !mats[0]) return;
  const auto& c = mats[0]->baseColorFactor;
  *rgb = {c.r, c.g, c.b};
}

// ── Asset import ─────────────────────────────────────

TUCANO_API uint32_t tucano_scene_import_mesh(TucanoScene* scene, const char* path,
                                             TucanoVec3 position, float scale) {
  if (!scene || !scene->runtime || !scene->runtime->device || !path || !*path) return 0xFFFFFFFFu;
  if (scale <= 0.0f) scale = 1.0f;

  // Cooked assets carry a single mesh and no scene graph, so they take a direct path rather than
  // going through the glTF parser (which would reject the binary outright).
  {
    std::string p(path);
    const size_t dot = p.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : p.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".tuasset") {
      std::string err;
      std::shared_ptr<tucano::Material> material;
      auto mesh = tucano::loadTuassetMesh(*scene->runtime->device, p, &err, &material);
      if (!mesh) {
        std::cerr << "[Tuasset] " << err << "\n";
        return 0xFFFFFFFFu;
      }
      tucano::RenderObject obj;
      obj.mesh = std::move(mesh);
      // Falls back to a default material only when the asset carries none.
      obj.materials.push_back(material ? material : std::make_shared<tucano::Material>());
      obj.name = std::filesystem::path(p).stem().string();
      obj.transform.translation = {position.x, position.y, position.z};
      obj.transform.scale = glm::vec3(scale);
      obj.worldMatrix = obj.transform.matrix();
      const uint32_t index = uint32_t(scene->data.objects.size());
      scene->data.objects.push_back(std::move(obj));
      scene->runtime->sources.push_back(ObjectSource{ObjectSource::Kind::Gltf, p});
      return index;
    }
  }

  // loadGLTFScene fills a whole Scene; import into a scratch one and move its objects over so the
  // file's own lights/camera don't clobber the level being edited.
  tucano::Scene imported;
  try {
    if (!tucano::loadGLTFScene(*scene->runtime->device, path, imported)) return 0xFFFFFFFFu;
  } catch (...) {
    return 0xFFFFFFFFu;
  }
  if (imported.objects.empty()) return 0xFFFFFFFFu;

  const uint32_t first = uint32_t(scene->data.objects.size());
  const glm::vec3 pos{position.x, position.y, position.z};
  for (auto& obj : imported.objects) {
    // Keep the file's own placement, then offset the whole import to where it was dropped.
    obj.transform.translation = obj.transform.translation * scale + pos;
    obj.transform.scale *= scale;
    obj.worldMatrix = obj.transform.matrix();
    scene->data.objects.push_back(std::move(obj));
    scene->runtime->sources.push_back(ObjectSource{ObjectSource::Kind::Gltf, path});
  }
  return first;
}

// ── Physics & Play mode ──────────────────────────────

TUCANO_API void tucano_scene_set_object_physics(TucanoScene* scene, uint32_t index,
                                                TucanoPhysicsKind kind, float mass) {
  if (!scene || !scene->runtime) return;
  auto& src = scene->runtime->sources;
  if (index >= src.size()) return;
  src[index].physics = int(kind);
  src[index].mass = mass > 0.0f ? mass : 1.0f;
}

TUCANO_API TucanoPhysicsKind tucano_scene_get_object_physics(TucanoScene* scene, uint32_t index) {
  if (!scene || !scene->runtime || index >= scene->runtime->sources.size()) {
    return TUCANO_PHYSICS_NONE;
  }
  return TucanoPhysicsKind(scene->runtime->sources[index].physics);
}

TUCANO_API float tucano_scene_get_object_mass(TucanoScene* scene, uint32_t index) {
  if (!scene || !scene->runtime || index >= scene->runtime->sources.size()) return 1.0f;
  return scene->runtime->sources[index].mass;
}

TUCANO_API bool tucano_play_start(TucanoScene* scene) {
  if (!scene || !scene->runtime) return false;
  auto& rt = *scene->runtime;
  if (rt.playState == TUCANO_PLAY_RUNNING) return true;
  if (rt.playState == TUCANO_PLAY_PAUSED) {
    rt.playState = TUCANO_PLAY_RUNNING;
    return true;
  }

  auto& objects = scene->data.objects;
  rt.sources.resize(objects.size()); // defensive: keep the two in step

  try {
    rt.physics = std::make_unique<tucano::physics::PhysicsWorld>();
  } catch (...) {
    rt.physics.reset();
    return false;
  }

  // Remember the authored placement before the simulation moves anything.
  rt.editTransforms.clear();
  rt.editTransforms.reserve(objects.size());
  for (const auto& o : objects) rt.editTransforms.push_back(o.transform);

  rt.bodies.assign(objects.size(), JPH::BodyID());
  for (size_t i = 0; i < objects.size(); ++i) {
    const int kind = rt.sources[i].physics;
    if (kind == TUCANO_PHYSICS_NONE) continue;

    const auto& obj = objects[i];
    glm::vec3 half, offset;
    if (!objectWorldHalfExtent(obj, half, offset)) continue;

    const glm::vec3 pos = obj.transform.translation + offset;
    const glm::quat rot = obj.transform.rotation;
    // Spheres get a sphere shape so they roll; everything else is boxed. Good enough for blocking
    // out a level — per-triangle colliders would need the mesh CPU data uploaded per object.
    const bool isSphere = rt.sources[i].kind == ObjectSource::Kind::Sphere ||
                          rt.sources[i].kind == ObjectSource::Kind::BuiltinMarker;
    const float radius = std::max({half.x, half.y, half.z});

    if (kind == TUCANO_PHYSICS_STATIC) {
      rt.bodies[i] = isSphere ? rt.physics->createStaticSphere(radius, pos)
                              : rt.physics->createStaticBox(half, pos, rot);
    } else {
      const float mass = rt.sources[i].mass;
      rt.bodies[i] = isSphere ? rt.physics->createDynamicSphere(radius, pos, mass)
                              : rt.physics->createDynamicBox(half, pos, rot, mass);
      // Discrete collision tunnels straight through thin geometry: a ground plane is ~2 cm thick
      // while a body falling from a few metres covers ~25 cm per 60 Hz step. Linear-cast sweeps
      // the motion instead, which is what makes dropped objects actually land.
      if (!rt.bodies[i].IsInvalid()) {
        rt.physics->bodyInterface().SetMotionQuality(rt.bodies[i], JPH::EMotionQuality::LinearCast);
      }
    }
  }

  rt.collidersBuilt = 0;
  rt.collidersFailed = 0;
  for (size_t i = 0; i < rt.bodies.size(); ++i) {
    if (i < rt.sources.size() && rt.sources[i].physics == TUCANO_PHYSICS_NONE) continue;
    if (rt.bodies[i].IsInvalid()) ++rt.collidersFailed; else ++rt.collidersBuilt;
  }

  rt.playAccumulator = 0.0f;
  rt.playState = TUCANO_PLAY_RUNNING;
  return true;
}

TUCANO_API void tucano_play_pause(TucanoRuntime* rt, bool paused) {
  if (!rt || rt->playState == TUCANO_PLAY_STOPPED) return;
  rt->playState = paused ? TUCANO_PLAY_PAUSED : TUCANO_PLAY_RUNNING;
}

TUCANO_API void tucano_play_stop(TucanoScene* scene) {
  if (!scene || !scene->runtime) return;
  auto& rt = *scene->runtime;
  if (rt.playState == TUCANO_PLAY_STOPPED) return;

  // Restore the authored transforms — playing must never mutate the level.
  auto& objects = scene->data.objects;
  const size_t n = std::min(objects.size(), rt.editTransforms.size());
  for (size_t i = 0; i < n; ++i) {
    objects[i].transform = rt.editTransforms[i];
    objects[i].worldMatrix = objects[i].transform.matrix();
  }

  rt.physics.reset();
  rt.bodies.clear();
  rt.editTransforms.clear();
  rt.playState = TUCANO_PLAY_STOPPED;
  rt.playAccumulator = 0.0f;
}

TUCANO_API uint32_t tucano_play_collider_count(TucanoRuntime* rt) {
  return rt ? rt->collidersBuilt : 0;
}

TUCANO_API uint32_t tucano_play_failed_collider_count(TucanoRuntime* rt) {
  return rt ? rt->collidersFailed : 0;
}

TUCANO_API TucanoPlayState tucano_play_state(TucanoRuntime* rt) {
  return rt ? TucanoPlayState(rt->playState) : TUCANO_PLAY_STOPPED;
}

// ── Save / load ──────────────────────────────────────

namespace {

const char* sourceKindName(ObjectSource::Kind k) {
  switch (k) {
    case ObjectSource::Kind::Sphere: return "sphere";
    case ObjectSource::Kind::Plane: return "plane";
    case ObjectSource::Kind::Gltf: return "gltf";
    case ObjectSource::Kind::BuiltinGround: return "builtin_ground";
    case ObjectSource::Kind::BuiltinMarker: return "builtin_marker";
    case ObjectSource::Kind::Cube: break;
  }
  return "cube";
}

ObjectSource::Kind sourceKindFromName(const std::string& n) {
  if (n == "sphere") return ObjectSource::Kind::Sphere;
  if (n == "plane") return ObjectSource::Kind::Plane;
  if (n == "gltf") return ObjectSource::Kind::Gltf;
  if (n == "builtin_ground") return ObjectSource::Kind::BuiltinGround;
  if (n == "builtin_marker") return ObjectSource::Kind::BuiltinMarker;
  return ObjectSource::Kind::Cube;
}

// Minimal writer — Core/Json.h only parses.
void jsonEscape(std::string& out, const std::string& s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void jsonNum(std::string& out, float v) {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.6g", double(v));
  out += buf;
}

void jsonVec3(std::string& out, const glm::vec3& v) {
  out += '[';
  jsonNum(out, v.x); out += ',';
  jsonNum(out, v.y); out += ',';
  jsonNum(out, v.z);
  out += ']';
}

glm::vec3 readVec3(const tucano::core::JsonValue* v, const glm::vec3& def) {
  if (!v || !v->isArray() || v->arr.size() < 3) return def;
  return {v->arr[0].asFloat(def.x), v->arr[1].asFloat(def.y), v->arr[2].asFloat(def.z)};
}

// Rebuilds the mesh described by a source entry. Returns null when the file can't be loaded.
std::shared_ptr<tucano::Mesh> meshForSource(TucanoRuntime& rt, const ObjectSource& src) {
  auto& dev = *rt.device;
  switch (src.kind) {
    case ObjectSource::Kind::Sphere:
      if (!rt.primSphere) rt.primSphere = makeMarkerSphere(dev, 0.5f, 24);
      return rt.primSphere;
    case ObjectSource::Kind::Plane:
      if (!rt.primPlane) rt.primPlane = makeGroundPlane(dev, 0.5f, 2);
      return rt.primPlane;
    case ObjectSource::Kind::BuiltinGround:
      return makeGroundPlane(dev, 800.0f, 32);
    case ObjectSource::Kind::BuiltinMarker:
      return makeMarkerSphere(dev, 0.6f, 20);
    case ObjectSource::Kind::Gltf: {
      tucano::Scene tmp;
      try {
        if (!tucano::loadGLTFScene(dev, src.path, tmp) || tmp.objects.empty()) return nullptr;
      } catch (...) {
        return nullptr;
      }
      return tmp.objects.front().mesh;
    }
    case ObjectSource::Kind::Cube:
      break;
  }
  if (!rt.primCube) rt.primCube = makeCubeMesh(dev, 1.0f);
  return rt.primCube;
}

} // namespace

TUCANO_API bool tucano_scene_save(TucanoScene* scene, const char* path) {
  if (!scene || !scene->runtime || !path) return false;
  auto& rt = *scene->runtime;
  const auto& sc = scene->data;

  std::string out;
  out.reserve(4096);
  out += "{\n  \"version\": 1,\n";

  // Camera
  out += "  \"camera\": { \"position\": ";
  jsonVec3(out, sc.camera.position());
  out += ", \"forward\": ";
  jsonVec3(out, sc.camera.forward());
  out += " },\n";

  // Environment + rain, reusing the C ABI structs so there is a single source of truth.
  TucanoEnvironment env{};
  tucano_env_get(&rt, &env);
  TucanoRain rain{};
  tucano_rain_get(&rt, &rain);
  out += "  \"environment\": { ";
  out += "\"timeOfDay\": "; jsonNum(out, env.timeOfDay);
  out += ", \"turbidity\": "; jsonNum(out, env.turbidity);
  out += ", \"fogDensity\": "; jsonNum(out, env.fogDensity);
  out += ", \"fogHeight\": "; jsonNum(out, env.fogHeight);
  out += ", \"wind\": "; jsonVec3(out, {env.wind.x, env.wind.y, env.wind.z});
  out += ", \"clouds\": " + std::string(env.enableClouds ? "true" : "false");
  out += ", \"cloudCoverage\": "; jsonNum(out, env.cloudCoverage);
  out += ", \"cloudDensity\": "; jsonNum(out, env.cloudDensity);
  out += ", \"cloudAltitude\": "; jsonNum(out, env.cloudAltitude);
  out += ", \"cloudThickness\": "; jsonNum(out, env.cloudThickness);
  out += ", \"cloudStorminess\": "; jsonNum(out, env.cloudStorminess);
  out += ", \"atmosphere\": " + std::string(env.enableAtmosphere ? "true" : "false");
  out += ", \"bloom\": " + std::string(env.enableBloom ? "true" : "false");
  out += ", \"ao\": " + std::string(env.enableAO ? "true" : "false");
  out += ", \"shadows\": " + std::string(env.enableShadows ? "true" : "false");
  out += ", \"exposureTarget\": "; jsonNum(out, env.exposureTarget);
  out += " },\n";

  out += "  \"rain\": { ";
  out += "\"enabled\": " + std::string(rain.enabled ? "true" : "false");
  out += ", \"amount\": "; jsonNum(out, rain.amount);
  out += ", \"streakIntensity\": "; jsonNum(out, rain.streakIntensity);
  out += ", \"puddlesAmount\": "; jsonNum(out, rain.puddlesAmount);
  out += ", \"mistAmount\": "; jsonNum(out, rain.mistAmount);
  out += ", \"sceneRain\": " + std::string(rain.enableSceneRain ? "true" : "false");
  out += ", \"splashes\": " + std::string(rain.enableWorldSplashes ? "true" : "false");
  out += ", \"color\": "; jsonVec3(out, {rain.color.x, rain.color.y, rain.color.z});
  out += ", \"wind\": "; jsonVec3(out, {rain.wind.x, rain.wind.y, rain.wind.z});
  out += " },\n";

  // Lights
  out += "  \"lights\": [\n";
  for (size_t i = 0; i < sc.lights.size(); ++i) {
    const auto& l = sc.lights[i];
    out += "    { \"type\": " + std::to_string(int(l.type));
    out += ", \"position\": "; jsonVec3(out, l.position);
    out += ", \"direction\": "; jsonVec3(out, l.direction);
    out += ", \"color\": "; jsonVec3(out, l.color);
    out += ", \"intensity\": "; jsonNum(out, l.intensity);
    out += ", \"range\": "; jsonNum(out, l.range);
    out += ", \"innerCone\": "; jsonNum(out, l.innerCone);
    out += ", \"outerCone\": "; jsonNum(out, l.outerCone);
    out += ", \"castShadows\": " + std::string(l.castShadows ? "true" : "false");
    out += " }";
    if (i + 1 < sc.lights.size()) out += ',';
    out += '\n';
  }
  out += "  ],\n";

  // Objects
  out += "  \"objects\": [\n";
  for (size_t i = 0; i < sc.objects.size(); ++i) {
    const auto& o = sc.objects[i];
    const ObjectSource& src = i < rt.sources.size() ? rt.sources[i] : ObjectSource{};
    out += "    { \"name\": ";
    jsonEscape(out, o.name);
    out += ", \"source\": \"" + std::string(sourceKindName(src.kind)) + "\"";
    if (src.kind == ObjectSource::Kind::Gltf) {
      out += ", \"path\": ";
      jsonEscape(out, src.path);
    }
    if (src.physics != 0) {
      out += ", \"physics\": " + std::to_string(src.physics);
      out += ", \"mass\": "; jsonNum(out, src.mass);
    }
    if (!src.folder.empty()) {
      out += ", \"folder\": ";
      jsonEscape(out, src.folder);
    }
    // Only written when hidden, so the common case stays terse and older scenes still load.
    if (!o.visible) out += ", \"visible\": false";
    out += ", \"position\": "; jsonVec3(out, o.transform.translation);
    out += ", \"rotation\": [";
    jsonNum(out, o.transform.rotation.x); out += ',';
    jsonNum(out, o.transform.rotation.y); out += ',';
    jsonNum(out, o.transform.rotation.z); out += ',';
    jsonNum(out, o.transform.rotation.w);
    out += ']';
    out += ", \"scale\": "; jsonVec3(out, o.transform.scale);

    // Material — write the untinted colour when this object happens to be selected.
    if (!o.materials.empty() && o.materials[0]) {
      const auto& m = *o.materials[0];
      glm::vec3 base{m.baseColorFactor};
      if (rt.selectedObject == int(i)) base = rt.selectedBaseColor;
      out += ", \"material\": { \"baseColor\": ";
      jsonVec3(out, base);
      out += ", \"emissive\": "; jsonVec3(out, m.emissiveFactor);
      out += ", \"metallic\": "; jsonNum(out, m.metallicFactor);
      out += ", \"roughness\": "; jsonNum(out, m.roughnessFactor);
      out += ", \"alpha\": "; jsonNum(out, m.baseColorFactor.a);
      out += " }";
    }
    out += " }";
    if (i + 1 < sc.objects.size()) out += ',';
    out += '\n';
  }
  out += "  ]\n}\n";

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(out.data(), std::streamsize(out.size()));
  return f.good();
}

TUCANO_API bool tucano_scene_load(TucanoScene* scene, const char* path) {
  if (!scene || !scene->runtime || !scene->runtime->device || !path) return false;
  auto& rt = *scene->runtime;

  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  tucano::core::JsonValue root;
  if (!tucano::core::JsonValue::parse(text, root) || !root.isObject()) return false;

  // Build into scratch containers first: a parse failure or a missing asset must not leave the
  // editor holding half a scene.
  std::vector<tucano::RenderObject> objects;
  std::vector<ObjectSource> sources;
  std::vector<tucano::Light> lights;

  if (const auto* jobjs = root.find("objects"); jobjs && jobjs->isArray()) {
    for (const auto& jo : jobjs->arr) {
      if (!jo.isObject()) continue;
      ObjectSource src;
      const auto* jsrc = jo.find("source");
      src.kind = sourceKindFromName(jsrc ? jsrc->asString() : std::string("cube"));
      if (const auto* jpath = jo.find("path")) src.path = jpath->asString();
      if (const auto* jphys = jo.find("physics")) src.physics = glm::clamp(jphys->asInt(0), 0, 2);
      if (const auto* jfold = jo.find("folder")) src.folder = jfold->asString();
      if (const auto* jmass = jo.find("mass")) src.mass = std::max(jmass->asFloat(1.0f), 0.001f);

      auto mesh = meshForSource(rt, src);
      if (!mesh) continue; // asset went missing — skip rather than abort the whole load

      tucano::RenderObject obj;
      if (const auto* jname = jo.find("name")) obj.name = jname->asString();
      obj.mesh = std::move(mesh);
      obj.transform.translation = readVec3(jo.find("position"), glm::vec3(0.0f));
      obj.transform.scale = readVec3(jo.find("scale"), glm::vec3(1.0f));
      if (const auto* jrot = jo.find("rotation"); jrot && jrot->isArray() && jrot->arr.size() >= 4) {
        obj.transform.rotation = glm::quat(jrot->arr[3].asFloat(1.0f), jrot->arr[0].asFloat(0.0f),
                                           jrot->arr[1].asFloat(0.0f), jrot->arr[2].asFloat(0.0f));
      }
      obj.worldMatrix = obj.transform.matrix();
      if (const auto* jvis = jo.find("visible")) obj.visible = jvis->asBool(true);

      auto mat = std::make_shared<tucano::Material>();
      mat->name = obj.name;
      // Primitives get the dev grid back on load; imported meshes carry their own textures.
      if (src.kind != ObjectSource::Kind::Gltf) {
        mat->albedo = tucano::devtex::defaultAlbedo(*rt.device);
        mat->normal = tucano::devtex::defaultNormal(*rt.device);
      }
      if (const auto* jm = jo.find("material"); jm && jm->isObject()) {
        const glm::vec3 base = readVec3(jm->find("baseColor"), glm::vec3(0.72f, 0.72f, 0.75f));
        const auto* ja = jm->find("alpha");
        mat->baseColorFactor = glm::vec4(base, ja ? ja->asFloat(1.0f) : 1.0f);
        mat->emissiveFactor = readVec3(jm->find("emissive"), glm::vec3(0.0f));
        if (const auto* jmet = jm->find("metallic")) mat->metallicFactor = jmet->asFloat(0.0f);
        if (const auto* jr = jm->find("roughness")) mat->roughnessFactor = jr->asFloat(0.55f);
      } else {
        mat->baseColorFactor = {0.72f, 0.72f, 0.75f, 1.0f};
        mat->roughnessFactor = 0.55f;
        mat->metallicFactor = 0.0f;
      }
      obj.materials = {std::move(mat)};

      objects.push_back(std::move(obj));
      sources.push_back(std::move(src));
    }
  }

  if (const auto* jlights = root.find("lights"); jlights && jlights->isArray()) {
    for (const auto& jl : jlights->arr) {
      if (!jl.isObject()) continue;
      tucano::Light l;
      const auto* jt = jl.find("type");
      l.type = tucano::LightType(glm::clamp(jt ? jt->asInt(0) : 0, 0, 2));
      l.position = readVec3(jl.find("position"), glm::vec3(0.0f));
      const glm::vec3 dir = readVec3(jl.find("direction"), glm::vec3(0, -1, 0));
      l.direction = glm::dot(dir, dir) > 1e-8f ? glm::normalize(dir) : glm::vec3(0, -1, 0);
      l.color = readVec3(jl.find("color"), glm::vec3(1.0f));
      if (const auto* v = jl.find("intensity")) l.intensity = v->asFloat(1.0f);
      if (const auto* v = jl.find("range")) l.range = v->asFloat(10.0f);
      if (const auto* v = jl.find("innerCone")) l.innerCone = v->asFloat(0.0f);
      if (const auto* v = jl.find("outerCone")) l.outerCone = v->asFloat(0.0f);
      if (const auto* v = jl.find("castShadows")) l.castShadows = v->asBool(true);
      lights.push_back(l);
    }
  }

  // Commit.
  applySelection(&rt, -1);
  scene->data.objects = std::move(objects);
  scene->data.lights = std::move(lights);
  rt.sources = std::move(sources);
  rt.dragging = false;

  if (const auto* jenv = root.find("environment"); jenv && jenv->isObject()) {
    TucanoEnvironment env{};
    tucano_env_get(&rt, &env);
    if (const auto* v = jenv->find("timeOfDay")) env.timeOfDay = v->asFloat(env.timeOfDay);
    if (const auto* v = jenv->find("turbidity")) env.turbidity = v->asFloat(env.turbidity);
    if (const auto* v = jenv->find("fogDensity")) env.fogDensity = v->asFloat(env.fogDensity);
    if (const auto* v = jenv->find("fogHeight")) env.fogHeight = v->asFloat(env.fogHeight);
    const glm::vec3 w = readVec3(jenv->find("wind"), {env.wind.x, env.wind.y, env.wind.z});
    env.wind = {w.x, w.y, w.z};
    if (const auto* v = jenv->find("clouds")) env.enableClouds = v->asBool(true) ? 1 : 0;
    if (const auto* v = jenv->find("cloudCoverage")) env.cloudCoverage = v->asFloat(env.cloudCoverage);
    if (const auto* v = jenv->find("cloudDensity")) env.cloudDensity = v->asFloat(env.cloudDensity);
    if (const auto* v = jenv->find("cloudAltitude")) env.cloudAltitude = v->asFloat(env.cloudAltitude);
    if (const auto* v = jenv->find("cloudThickness")) env.cloudThickness = v->asFloat(env.cloudThickness);
    if (const auto* v = jenv->find("cloudStorminess")) env.cloudStorminess = v->asFloat(env.cloudStorminess);
    if (const auto* v = jenv->find("atmosphere")) env.enableAtmosphere = v->asBool(true) ? 1 : 0;
    if (const auto* v = jenv->find("bloom")) env.enableBloom = v->asBool(true) ? 1 : 0;
    if (const auto* v = jenv->find("ao")) env.enableAO = v->asBool(true) ? 1 : 0;
    if (const auto* v = jenv->find("shadows")) env.enableShadows = v->asBool(true) ? 1 : 0;
    if (const auto* v = jenv->find("exposureTarget")) env.exposureTarget = v->asFloat(env.exposureTarget);
    tucano_env_set(&rt, &env);
  }

  if (const auto* jrain = root.find("rain"); jrain && jrain->isObject()) {
    TucanoRain rain{};
    tucano_rain_get(&rt, &rain);
    if (const auto* v = jrain->find("enabled")) rain.enabled = v->asBool(false) ? 1 : 0;
    if (const auto* v = jrain->find("amount")) rain.amount = v->asFloat(rain.amount);
    if (const auto* v = jrain->find("streakIntensity")) rain.streakIntensity = v->asFloat(rain.streakIntensity);
    if (const auto* v = jrain->find("puddlesAmount")) rain.puddlesAmount = v->asFloat(rain.puddlesAmount);
    if (const auto* v = jrain->find("mistAmount")) rain.mistAmount = v->asFloat(rain.mistAmount);
    if (const auto* v = jrain->find("sceneRain")) rain.enableSceneRain = v->asBool(true) ? 1 : 0;
    if (const auto* v = jrain->find("splashes")) rain.enableWorldSplashes = v->asBool(true) ? 1 : 0;
    const glm::vec3 rc = readVec3(jrain->find("color"), {rain.color.x, rain.color.y, rain.color.z});
    rain.color = {rc.x, rc.y, rc.z};
    const glm::vec3 rw = readVec3(jrain->find("wind"), {rain.wind.x, rain.wind.y, rain.wind.z});
    rain.wind = {rw.x, rw.y, rw.z};
    tucano_rain_set(&rt, &rain);
  }

  if (const auto* jcam = root.find("camera"); jcam && jcam->isObject()) {
    const glm::vec3 p = readVec3(jcam->find("position"), scene->data.camera.position());
    const glm::vec3 fwd = readVec3(jcam->find("forward"), glm::vec3(0, 0, -1));
    scene->data.camera.setPosition(p);
    if (glm::dot(fwd, fwd) > 1e-8f) scene->data.camera.lookAt(p + glm::normalize(fwd));
  }
  return true;
}

// ── Lights ───────────────────────────────────────────

TUCANO_API uint32_t tucano_scene_light_count(TucanoScene* scene) {
  return scene ? uint32_t(scene->data.lights.size()) : 0;
}

TUCANO_API uint32_t tucano_scene_add_light(TucanoScene* scene, const TucanoLight* light) {
  if (!scene || !light) return 0xFFFFFFFFu;
  tucano::Light l;
  l.type = tucano::LightType(glm::clamp(light->type, 0, 2));
  l.position = {light->position.x, light->position.y, light->position.z};
  const glm::vec3 dir{light->direction.x, light->direction.y, light->direction.z};
  l.direction = glm::dot(dir, dir) > 1e-8f ? glm::normalize(dir) : glm::vec3(0, -1, 0);
  l.color = {light->color.x, light->color.y, light->color.z};
  l.intensity = light->intensity;
  l.range = std::max(light->range, 0.01f);
  l.innerCone = light->innerCone;
  l.outerCone = light->outerCone;
  l.castShadows = light->castShadows != 0;
  scene->data.lights.push_back(l);
  return uint32_t(scene->data.lights.size() - 1);
}

TUCANO_API void tucano_scene_get_light(TucanoScene* scene, uint32_t index, TucanoLight* out) {
  if (!scene || !out || index >= scene->data.lights.size()) return;
  const auto& l = scene->data.lights[index];
  out->type = int32_t(l.type);
  out->castShadows = l.castShadows ? 1 : 0;
  out->position = {l.position.x, l.position.y, l.position.z};
  out->direction = {l.direction.x, l.direction.y, l.direction.z};
  out->color = {l.color.x, l.color.y, l.color.z};
  out->intensity = l.intensity;
  out->range = l.range;
  out->innerCone = l.innerCone;
  out->outerCone = l.outerCone;
}

TUCANO_API void tucano_scene_set_light(TucanoScene* scene, uint32_t index, const TucanoLight* light) {
  if (!scene || !light || index >= scene->data.lights.size()) return;
  auto& l = scene->data.lights[index];
  l.type = tucano::LightType(glm::clamp(light->type, 0, 2));
  l.position = {light->position.x, light->position.y, light->position.z};
  const glm::vec3 dir{light->direction.x, light->direction.y, light->direction.z};
  if (glm::dot(dir, dir) > 1e-8f) l.direction = glm::normalize(dir);
  l.color = {light->color.x, light->color.y, light->color.z};
  l.intensity = std::max(light->intensity, 0.0f);
  l.range = std::max(light->range, 0.01f);
  l.innerCone = light->innerCone;
  l.outerCone = light->outerCone;
  l.castShadows = light->castShadows != 0;
}

TUCANO_API void tucano_scene_remove_light(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.lights.size()) return;
  scene->data.lights.erase(scene->data.lights.begin() + index);
}

// ── Materials ────────────────────────────────────────

TUCANO_API void tucano_scene_get_object_material(TucanoScene* scene, uint32_t index,
                                                 TucanoMaterial* out) {
  if (!scene || !out || index >= scene->data.objects.size()) return;
  const auto& mats = scene->data.objects[index].materials;
  if (mats.empty() || !mats[0]) return;
  const auto& m = *mats[0];
  // The selection tint lives in the material, so report the stored original instead.
  glm::vec3 base{m.baseColorFactor};
  if (scene->runtime && scene->runtime->selectedObject == int(index)) {
    base = scene->runtime->selectedBaseColor;
  }
  out->baseColor = {base.r, base.g, base.b};
  out->emissive = {m.emissiveFactor.r, m.emissiveFactor.g, m.emissiveFactor.b};
  out->metallic = m.metallicFactor;
  out->roughness = m.roughnessFactor;
  out->alpha = m.baseColorFactor.a;
}

TUCANO_API void tucano_scene_set_object_material(TucanoScene* scene, uint32_t index,
                                                 const TucanoMaterial* mat) {
  if (!scene || !mat || index >= scene->data.objects.size()) return;
  auto& mats = scene->data.objects[index].materials;
  if (mats.empty() || !mats[0]) return;
  auto& m = *mats[0];
  m.metallicFactor = glm::clamp(mat->metallic, 0.0f, 1.0f);
  m.roughnessFactor = glm::clamp(mat->roughness, 0.02f, 1.0f);
  m.emissiveFactor = {mat->emissive.x, mat->emissive.y, mat->emissive.z};
  const float alpha = glm::clamp(mat->alpha, 0.0f, 1.0f);
  const glm::vec3 base{mat->baseColor.x, mat->baseColor.y, mat->baseColor.z};
  if (scene->runtime && scene->runtime->selectedObject == int(index)) {
    scene->runtime->selectedBaseColor = base;
    const int sel = scene->runtime->selectedObject;
    scene->runtime->selectedObject = -1;
    applySelection(scene->runtime, sel); // re-tint over the new colour
    m.baseColorFactor.a = alpha;
  } else {
    m.baseColorFactor = glm::vec4(base, alpha);
  }
}

TUCANO_API uint32_t tucano_scene_duplicate_object(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3 offset) {
  if (!scene || !scene->runtime || index >= scene->data.objects.size()) return 0xFFFFFFFFu;
  tucano::RenderObject copy = scene->data.objects[index]; // shares the mesh, which is intended
  // Clone materials so editing the copy doesn't repaint the original.
  for (auto& m : copy.materials) {
    if (m) m = std::make_shared<tucano::Material>(*m);
  }
  // The source may be tinted by the selection; the copy must start from the real colour.
  if (scene->runtime->selectedObject == int(index) && !copy.materials.empty() && copy.materials[0]) {
    copy.materials[0]->baseColorFactor =
        glm::vec4(scene->runtime->selectedBaseColor, copy.materials[0]->baseColorFactor.a);
  }
  copy.name += "_copy";
  copy.transform.translation += glm::vec3(offset.x, offset.y, offset.z);
  copy.worldMatrix = copy.transform.matrix();
  scene->data.objects.push_back(std::move(copy));
  if (index < scene->runtime->sources.size()) {
    scene->runtime->sources.push_back(scene->runtime->sources[index]);
  } else {
    scene->runtime->sources.push_back(ObjectSource{});
  }
  return uint32_t(scene->data.objects.size() - 1);
}

TUCANO_API void tucano_scene_set_object_name(TucanoScene* scene, uint32_t index, const char* name) {
  if (!scene || !name || index >= scene->data.objects.size()) return;
  scene->data.objects[index].name = name;
}

TUCANO_API void tucano_scene_set_object_visible(TucanoScene* scene, uint32_t index, bool visible) {
  if (!scene || index >= scene->data.objects.size()) return;
  scene->data.objects[index].visible = visible;
  // A hidden object must not stay selected: the gizmo would float over nothing.
  if (!visible && scene->runtime && scene->runtime->selectedObject == int(index)) {
    applySelection(scene->runtime, -1);
  }
}

TUCANO_API bool tucano_scene_get_object_visible(TucanoScene* scene, uint32_t index) {
  if (!scene || index >= scene->data.objects.size()) return true;
  return scene->data.objects[index].visible;
}

TUCANO_API void tucano_scene_set_object_folder(TucanoScene* scene, uint32_t index,
                                               const char* folder) {
  if (!scene || !scene->runtime || index >= scene->runtime->sources.size()) return;
  scene->runtime->sources[index].folder = folder ? folder : "";
}

TUCANO_API const char* tucano_scene_get_object_folder(TucanoScene* scene, uint32_t index) {
  if (!scene || !scene->runtime || index >= scene->runtime->sources.size()) return "";
  return scene->runtime->sources[index].folder.c_str();
}

// ── Input (Phase I-0) ────────────────────────────────

TUCANO_API bool tucano_input_is_button_down(TucanoRuntime* rt, int buttonCode) {
  if (!rt) return false;
  const auto idx = static_cast<size_t>(buttonCode);
  if (idx >= static_cast<size_t>(tucano::input::ButtonCode::Count)) return false;
  // "Down" means the edge: held now, not held on the previous frame.
  return rt->inputSnapshot.down[idx] && !rt->prevInputSnapshot.down[idx];
}

TUCANO_API bool tucano_input_is_button_held(TucanoRuntime* rt, int buttonCode) {
  if (!rt) return false;
  const auto idx = static_cast<size_t>(buttonCode);
  if (idx >= static_cast<size_t>(tucano::input::ButtonCode::Count)) return false;
  return rt->inputSnapshot.down[idx];
}

TUCANO_API void tucano_input_get_mouse_delta(TucanoRuntime* rt, float* dx, float* dy) {
  if (!rt) return;
  if (dx) *dx = rt->inputSnapshot.axis[size_t(tucano::input::InputAxis::MouseX)];
  if (dy) *dy = rt->inputSnapshot.axis[size_t(tucano::input::InputAxis::MouseY)];
}

TUCANO_API float tucano_input_get_scroll(TucanoRuntime* rt) {
  return rt ? rt->inputSnapshot.axis[size_t(tucano::input::InputAxis::MouseWheel)] : 0.0f;
}

TUCANO_API bool tucano_input_is_virtual_button_down(TucanoRuntime* rt, const char* name) {
  return rt && name && rt->virtualInput.isButtonDown(name);
}

TUCANO_API bool tucano_input_is_virtual_button_held(TucanoRuntime* rt, const char* name) {
  return rt && name && rt->virtualInput.isButtonHeld(name);
}

TUCANO_API bool tucano_input_is_virtual_button_up(TucanoRuntime* rt, const char* name) {
  return rt && name && rt->virtualInput.isButtonUp(name);
}

TUCANO_API float tucano_input_get_virtual_axis(TucanoRuntime* rt, const char* name) {
  return rt && name ? rt->virtualInput.axisValue(name) : 0.0f;
}

TUCANO_API void tucano_input_bind_button(TucanoRuntime* rt, const char* name, int buttonCode,
                                         int modifiers, bool repeatable) {
  if (!rt || !name) return;
  if (buttonCode < 0 || buttonCode >= int(tucano::input::ButtonCode::Count)) return;
  rt->virtualInput.configuration().registerButton(
      name, static_cast<tucano::input::ButtonCode>(buttonCode),
      static_cast<tucano::input::ButtonModifier>(modifiers), repeatable);
}

TUCANO_API void tucano_input_unbind_button(TucanoRuntime* rt, const char* name) {
  if (rt && name) rt->virtualInput.configuration().unregisterButton(name);
}

TUCANO_API void tucano_input_bind_axis(TucanoRuntime* rt, const char* name, int axis,
                                       float deadZone, float sensitivity, bool invert,
                                       bool normalize) {
  if (!rt || !name) return;
  if (axis < 0 || axis >= int(tucano::input::InputAxis::Count)) return;
  tucano::input::VirtualAxisBinding b;
  b.axis = static_cast<tucano::input::InputAxis>(axis);
  b.deadZone = std::max(deadZone, 0.0f);
  b.sensitivity = sensitivity;
  b.invert = invert;
  b.normalize = normalize;
  rt->virtualInput.configuration().registerAxis(name, b);
}

TUCANO_API void tucano_input_reset_bindings(TucanoRuntime* rt) {
  if (rt) rt->virtualInput.setConfiguration(tucano::input::InputConfiguration::makeDefault());
}

TUCANO_API const char* tucano_input_button_name(int buttonCode) {
  if (buttonCode < 0 || buttonCode >= int(tucano::input::ButtonCode::Count)) return "Unassigned";
  return tucano::input::buttonCodeName(static_cast<tucano::input::ButtonCode>(buttonCode));
}

TUCANO_API int tucano_input_button_from_name(const char* name) {
  return int(tucano::input::buttonCodeFromName(name));
}

TUCANO_API int tucano_input_button_count(void) {
  return int(tucano::input::ButtonCode::Count);
}

// ── Skybox (Phase I-1) ───────────────────────────────

TUCANO_API bool tucano_skybox_set_texture(TucanoRuntime* rt, const char* hdriPath) {
  if (!rt || !rt->renderer || !hdriPath) return false;
  return rt->renderer->reloadIBL(hdriPath);
}

TUCANO_API const char* tucano_skybox_get_texture(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return "";
  return rt->renderer->settings().hdriPath.c_str();
}

TUCANO_API void tucano_skybox_set_brightness(TucanoRuntime* rt, float brightness) {
  if (rt && rt->renderer) rt->renderer->setIblExposure(std::max(brightness, 0.0f));
}

TUCANO_API float tucano_skybox_get_brightness(TucanoRuntime* rt) {
  return rt && rt->renderer ? rt->renderer->iblExposure() : 0.0f;
}

// ── Celestial bodies ─────────────────────────────────

TUCANO_API TucanoVec3 tucano_sky_sun_direction(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return TucanoVec3{0.0f, -1.0f, 0.0f};
  const auto& d = rt->renderer->celestial().sunDir;
  return TucanoVec3{d.x, d.y, d.z};
}

TUCANO_API TucanoVec3 tucano_sky_moon_direction(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return TucanoVec3{0.0f, -1.0f, 0.0f};
  const auto& d = rt->renderer->celestial().moonDir;
  return TucanoVec3{d.x, d.y, d.z};
}

TUCANO_API float tucano_sky_moon_phase(TucanoRuntime* rt) {
  return rt && rt->renderer ? rt->renderer->celestial().moonPhase : 0.0f;
}

TUCANO_API float tucano_sky_moon_illumination(TucanoRuntime* rt) {
  return rt && rt->renderer ? rt->renderer->celestial().moonIllumination : 0.0f;
}

// ── Environment ──────────────────────────────────────


// ── Viewport overlay ─────────────────────────────────

TUCANO_API void tucano_overlay_set_flags(TucanoRuntime* rt, uint32_t flags) {
  if (rt) rt->overlayFlags = flags;
}

TUCANO_API uint32_t tucano_overlay_get_flags(TucanoRuntime* rt) {
  return rt ? rt->overlayFlags : 0u;
}

TUCANO_API void tucano_overlay_set_drop_hint(TucanoRuntime* rt, const char* text) {
  if (!rt) return;
  rt->dropHint = (text && *text) ? text : std::string();
}

// ── Reflected parameter blocks ───────────────────────

namespace {

/// One editable block: a display name, the type description, and where the live instance lives.
/// The instance is resolved per call rather than cached, because the renderer can be recreated
/// after a device loss and a stale pointer would be written into freed memory.
struct ReflectedBlock {
  const char* name;
  const tucano::reflect::TypeDesc& (*describe)();
  void* (*instance)(TucanoRuntime*);
};

const ReflectedBlock& reflectedBlocks(uint32_t index, uint32_t* outCount) {
  static const ReflectedBlock kBlocks[] = {
      {"Water",
       []() -> const tucano::reflect::TypeDesc& { return tucano::reflect::describe<tucano::WaterParams>(); },
       [](TucanoRuntime* rt) -> void* { return rt && rt->renderer ? &rt->renderer->water() : nullptr; }},
      {"Fog",
       []() -> const tucano::reflect::TypeDesc& { return tucano::reflect::describe<tucano::FogParams>(); },
       [](TucanoRuntime* rt) -> void* { return rt && rt->renderer ? &rt->renderer->fog() : nullptr; }},
  };
  if (outCount) *outCount = uint32_t(sizeof(kBlocks) / sizeof(kBlocks[0]));
  static const ReflectedBlock kNull{"", nullptr, nullptr};
  const uint32_t count = uint32_t(sizeof(kBlocks) / sizeof(kBlocks[0]));
  return index < count ? kBlocks[index] : kNull;
}

} // namespace

TUCANO_API uint32_t tucano_reflect_block_count(TucanoRuntime* rt) {
  if (!rt) return 0;
  uint32_t count = 0;
  reflectedBlocks(0, &count);
  return count;
}

TUCANO_API const char* tucano_reflect_block_name(TucanoRuntime* rt, uint32_t block) {
  if (!rt) return "";
  return reflectedBlocks(block, nullptr).name;
}

TUCANO_API uint32_t tucano_reflect_field_count(TucanoRuntime* rt, uint32_t block) {
  if (!rt) return 0;
  const auto& b = reflectedBlocks(block, nullptr);
  return b.describe ? b.describe().fieldCount : 0;
}

TUCANO_API bool tucano_reflect_field_info(TucanoRuntime* rt, uint32_t block, uint32_t field,
                                          TucanoFieldInfo* out) {
  if (!rt || !out) return false;
  const auto& b = reflectedBlocks(block, nullptr);
  if (!b.describe) return false;
  const auto& type = b.describe();
  if (field >= type.fieldCount) return false;

  const auto& f = type.fields[field];
  out->name = f.name;
  out->label = f.label;
  out->tooltip = f.tooltip;
  out->type = uint32_t(f.type);
  out->components = tucano::reflect::componentCount(f.type);
  out->minValue = f.minValue;
  out->maxValue = f.maxValue;
  out->step = f.step;
  return true;
}

TUCANO_API float tucano_reflect_get(TucanoRuntime* rt, uint32_t block, uint32_t field,
                                    uint32_t component) {
  if (!rt) return 0.0f;
  const auto& b = reflectedBlocks(block, nullptr);
  if (!b.describe || !b.instance) return 0.0f;
  return tucano::reflect::getScalar(b.describe(), field, b.instance(rt), component);
}

TUCANO_API void tucano_reflect_set(TucanoRuntime* rt, uint32_t block, uint32_t field,
                                   uint32_t component, float value) {
  if (!rt) return;
  const auto& b = reflectedBlocks(block, nullptr);
  if (!b.describe || !b.instance) return;
  tucano::reflect::setScalar(b.describe(), field, b.instance(rt), value, component);
}

TUCANO_API void tucano_env_get(TucanoRuntime* rt, TucanoEnvironment* out) {
  if (!rt || !rt->renderer || !out) return;
  const auto& s = rt->renderer->settings();
  *out = {};
  out->enableAtmosphere = s.enableAtmosphere;
  out->useBrunetonAtmosphere = s.useBrunetonAtmosphere;
  out->atmosphereDrivesSun = s.atmosphereDrivesSun;
  out->timeOfDay = s.timeOfDay;
  out->turbidity = s.turbidity;
  out->fogDensity = s.fogDensity;
  out->fogHeight = s.fogHeight;
  out->wind = {s.wind.x, s.wind.y, s.wind.z};

  out->enableClouds = s.enableClouds;
  out->enableCloudShadows = s.enableCloudShadows;
  out->enableCloudGodRays = s.enableCloudGodRays;
  out->cloudsDriveRain = s.cloudsDriveRain;
  out->cloudCoverage = s.cloudCoverage;
  out->cloudDensity = s.cloudDensity;
  out->cloudAltitude = s.cloudAltitude;
  out->cloudThickness = s.cloudThickness;
  out->cloudShadowStrength = s.cloudShadowStrength;
  out->cloudGodRayStrength = s.cloudGodRayStrength;
  out->cloudStorminess = s.cloudStorminess;

  out->enableBloom = s.enableBloom;
  out->enableAO = s.enableAO;
  out->enableTonemap = s.enableTonemap;
  out->enableAutoExposure = s.enableAutoExposure;
  out->bloomStrength = s.bloomStrength;
  out->aoIntensity = s.aoIntensity;
  out->aoRadius = s.aoRadius;
  out->exposureMin = s.exposureMin;
  out->exposureMax = s.exposureMax;
  out->exposureAdapt = s.exposureAdapt;
  out->exposureTarget = s.exposureTarget;

  out->enableShadows = s.enableShadows;
  out->enablePCSS = s.enablePCSS;
  out->enableContactShadows = s.enableContactShadows;
  out->enableSSR = s.enableSSR;
  out->enableRTShadows = s.enableRTShadows;
  out->enableRTReflections = s.enableRTReflections;
  out->enableVoxelGI = s.enableVoxelGI;
  out->enableIBL = s.enableIBL;
  out->giTier = int32_t(s.giTier);
  out->shadowMapSize = int32_t(s.shadowMapSize);
  out->pcssLightSize = s.pcssLightSize;

  out->enableMoon = s.enableMoon ? 1 : 0;
  out->enableStars = s.enableStars ? 1 : 0;
  out->moonIntensity = s.moonIntensity;
  out->moonDiscBrightness = s.moonDiscBrightness;
  out->moonAngularRadiusDeg = s.moonAngularRadiusDeg;
  out->starIntensity = s.starIntensity;
  out->starTwinkle = s.starTwinkle;
  out->starSizeDeg = s.starSizeDeg;
  out->purkinjeStrength = s.purkinjeStrength;
  out->latitudeDeg = s.latitudeDeg;
  out->dayOfYear = s.dayOfYear;

  const auto& f = rt->renderer->fog();
  out->enableFog = f.enabled ? 1 : 0;
  out->volumetricFog = f.volumetric ? 1 : 0;
  out->fogDensityVol = f.density;
  out->fogBaseHeight = f.baseHeight;
  out->fogHeightFalloff = f.heightFalloff;
  out->fogAlbedo = f.albedo;
  out->fogAnisotropy = f.anisotropy;
  out->fogScatterR = f.scatteringColor.r;
  out->fogScatterG = f.scatteringColor.g;
  out->fogScatterB = f.scatteringColor.b;
  out->fogSunIntensity = f.sunIntensity;
  out->fogAmbientIntensity = f.ambientIntensity;
  out->fogShadowStrength = f.shadowStrength;
  out->fogNoiseStrength = f.noiseStrength;
  out->fogNoiseScale = f.noiseScale;
  out->fogNoiseSpeed = f.noiseSpeed;
  out->fogMaxDistance = f.maxDistance;
}

TUCANO_API void tucano_env_set(TucanoRuntime* rt, const TucanoEnvironment* env) {
  if (!rt || !rt->renderer || !env) return;
  auto& s = rt->renderer->settings();
  s.enableAtmosphere = env->enableAtmosphere != 0;
  s.useBrunetonAtmosphere = env->useBrunetonAtmosphere != 0;
  s.atmosphereDrivesSun = env->atmosphereDrivesSun != 0;
  s.timeOfDay = glm::clamp(env->timeOfDay, 0.0f, 1.0f);
  s.turbidity = glm::clamp(env->turbidity, 1.0f, 10.0f);
  s.fogDensity = std::max(env->fogDensity, 0.0f);
  s.fogHeight = env->fogHeight;
  s.wind = {env->wind.x, env->wind.y, env->wind.z};

  s.enableClouds = env->enableClouds != 0;
  s.enableCloudShadows = env->enableCloudShadows != 0;
  s.enableCloudGodRays = env->enableCloudGodRays != 0;
  s.cloudsDriveRain = env->cloudsDriveRain != 0;
  s.cloudCoverage = glm::clamp(env->cloudCoverage, 0.0f, 1.0f);
  s.cloudDensity = std::max(env->cloudDensity, 0.0f);
  s.cloudAltitude = std::max(env->cloudAltitude, 100.0f);
  s.cloudThickness = std::max(env->cloudThickness, 100.0f);
  s.cloudShadowStrength = glm::clamp(env->cloudShadowStrength, 0.0f, 1.0f);
  s.cloudGodRayStrength = glm::clamp(env->cloudGodRayStrength, 0.0f, 2.0f);
  s.cloudStorminess = glm::clamp(env->cloudStorminess, 0.0f, 1.0f);

  s.enableBloom = env->enableBloom != 0;
  s.enableAO = env->enableAO != 0;
  s.enableTonemap = env->enableTonemap != 0;
  s.enableAutoExposure = env->enableAutoExposure != 0;
  s.bloomStrength = std::max(env->bloomStrength, 0.0f);
  s.aoIntensity = std::max(env->aoIntensity, 0.0f);
  s.aoRadius = std::max(env->aoRadius, 0.01f);
  s.exposureMin = std::max(env->exposureMin, 0.001f);
  s.exposureMax = std::max(env->exposureMax, s.exposureMin);
  s.exposureAdapt = std::max(env->exposureAdapt, 0.0f);
  s.exposureTarget = std::max(env->exposureTarget, 0.001f);

  s.enableShadows = env->enableShadows != 0;
  s.enablePCSS = env->enablePCSS != 0;
  s.enableContactShadows = env->enableContactShadows != 0;
  s.enableSSR = env->enableSSR != 0;
  s.enableRTShadows = env->enableRTShadows != 0;
  s.enableRTReflections = env->enableRTReflections != 0;
  s.enableVoxelGI = env->enableVoxelGI != 0;
  s.enableIBL = env->enableIBL != 0;
  s.giTier = tucano::GITier(glm::clamp(env->giTier, 0, 3));
  s.pcssLightSize = std::max(env->pcssLightSize, 0.0f);

  s.enableMoon = env->enableMoon != 0;
  s.enableStars = env->enableStars != 0;
  s.moonIntensity = std::max(env->moonIntensity, 0.0f);
  s.moonDiscBrightness = std::max(env->moonDiscBrightness, 0.0f);
  // Clamped: a moon smaller than a pixel flickers, and one larger than a few degrees stops
  // reading as the moon.
  s.moonAngularRadiusDeg = std::clamp(env->moonAngularRadiusDeg, 0.05f, 5.0f);
  s.starIntensity = std::max(env->starIntensity, 0.0f);
  s.starTwinkle = std::clamp(env->starTwinkle, 0.0f, 1.0f);
  s.starSizeDeg = std::clamp(env->starSizeDeg, 0.005f, 0.5f);
  s.purkinjeStrength = std::clamp(env->purkinjeStrength, 0.0f, 1.0f);
  s.latitudeDeg = std::clamp(env->latitudeDeg, -90.0f, 90.0f);
  s.dayOfYear = std::clamp(env->dayOfYear, 0.0f, 366.0f);
  // shadowMapSize is a resource dimension — changing it mid-flight would need a realloc, so it is
  // deliberately read-only here.

  auto& f = rt->renderer->fog();
  f.enabled = env->enableFog != 0;
  f.volumetric = env->volumetricFog != 0;
  f.density = std::clamp(env->fogDensityVol, 0.0f, 1.0f);
  f.baseHeight = env->fogBaseHeight;
  f.heightFalloff = std::max(env->fogHeightFalloff, 0.1f);
  f.albedo = std::clamp(env->fogAlbedo, 0.0f, 1.0f);
  f.anisotropy = std::clamp(env->fogAnisotropy, -0.95f, 0.95f);
  f.scatteringColor = {std::clamp(env->fogScatterR, 0.0f, 4.0f),
                       std::clamp(env->fogScatterG, 0.0f, 4.0f),
                       std::clamp(env->fogScatterB, 0.0f, 4.0f)};
  f.sunIntensity = std::clamp(env->fogSunIntensity, 0.0f, 20.0f);
  f.ambientIntensity = std::clamp(env->fogAmbientIntensity, 0.0f, 20.0f);
  f.shadowStrength = std::clamp(env->fogShadowStrength, 0.0f, 1.0f);
  f.noiseStrength = std::clamp(env->fogNoiseStrength, 0.0f, 1.0f);
  f.noiseScale = std::max(env->fogNoiseScale, 1.0f);
  f.noiseSpeed = std::clamp(env->fogNoiseSpeed, 0.0f, 50.0f);
  f.maxDistance = std::clamp(env->fogMaxDistance, 10.0f, 5000.0f);
}

TUCANO_API void tucano_rain_get(TucanoRuntime* rt, TucanoRain* out) {
  if (!rt || !rt->renderer || !out) return;
  const auto& r = rt->renderer->rain();
  *out = {};
  out->enabled = r.enabled;
  out->enableSceneRain = r.enableSceneRain;
  out->enableWorldSplashes = r.enableWorldSplashes;
  out->amount = r.amount;
  out->maxViewDist = r.maxViewDist;
  out->diffuseDarkening = r.diffuseDarkening;
  out->puddlesAmount = r.puddlesAmount;
  out->puddlesMask = r.puddlesMask;
  out->puddlesRipple = r.puddlesRipple;
  out->puddlesSSR = r.puddlesSSR;
  out->splashesAmount = r.splashesAmount;
  out->rainDropsAmount = r.rainDropsAmount;
  out->rainDropsSpeed = r.rainDropsSpeed;
  out->rainDropsLighting = r.rainDropsLighting;
  out->streakIntensity = r.streakIntensity;
  out->streakSpeed = r.streakSpeed;
  out->streakLayers = r.streakLayers;
  out->mistAmount = r.mistAmount;
  out->glossBoost = r.glossBoost;
  out->sceneRainIntensity = r.sceneRainIntensity;
  out->radius = r.radius;
  out->color = {r.color.x, r.color.y, r.color.z};
  out->wind = {r.wind.x, r.wind.y, r.wind.z};
}

TUCANO_API void tucano_rain_set(TucanoRuntime* rt, const TucanoRain* rain) {
  if (!rt || !rt->renderer || !rain) return;
  auto& r = rt->renderer->rain();
  r.enabled = rain->enabled != 0;
  r.enableSceneRain = rain->enableSceneRain != 0;
  r.enableWorldSplashes = rain->enableWorldSplashes != 0;
  r.amount = glm::clamp(rain->amount, 0.0f, 1.0f);
  r.maxViewDist = std::max(rain->maxViewDist, 1.0f);
  r.diffuseDarkening = glm::clamp(rain->diffuseDarkening, 0.0f, 1.0f);
  r.puddlesAmount = std::max(rain->puddlesAmount, 0.0f);
  r.puddlesMask = glm::clamp(rain->puddlesMask, 0.0f, 1.0f);
  r.puddlesRipple = std::max(rain->puddlesRipple, 0.0f);
  r.puddlesSSR = std::max(rain->puddlesSSR, 0.0f);
  r.splashesAmount = std::max(rain->splashesAmount, 0.0f);
  r.rainDropsAmount = std::max(rain->rainDropsAmount, 0.0f);
  r.rainDropsSpeed = std::max(rain->rainDropsSpeed, 0.0f);
  r.rainDropsLighting = std::max(rain->rainDropsLighting, 0.0f);
  r.streakIntensity = std::max(rain->streakIntensity, 0.0f);
  r.streakSpeed = std::max(rain->streakSpeed, 0.0f);
  r.streakLayers = glm::clamp(rain->streakLayers, 1.0f, 8.0f);
  r.mistAmount = std::max(rain->mistAmount, 0.0f);
  r.glossBoost = std::max(rain->glossBoost, 0.0f);
  r.sceneRainIntensity = std::max(rain->sceneRainIntensity, 0.0f);
  r.radius = std::max(rain->radius, 1.0f);
  r.color = {rain->color.x, rain->color.y, rain->color.z};
  r.wind = {rain->wind.x, rain->wind.y, rain->wind.z};
}

// ── Transform gizmo ──────────────────────────────────

TUCANO_API void tucano_gizmo_set_operation(TucanoRuntime* rt, TucanoGizmoOp op) {
  if (!rt) return;
  switch (op) {
    case TUCANO_GIZMO_ROTATE: rt->gizmoOp = tucano::DebugUI::GizmoOp::Rotate; break;
    case TUCANO_GIZMO_SCALE: rt->gizmoOp = tucano::DebugUI::GizmoOp::Scale; break;
    case TUCANO_GIZMO_TRANSLATE:
    default: rt->gizmoOp = tucano::DebugUI::GizmoOp::Translate; break;
  }
}

TUCANO_API TucanoGizmoOp tucano_gizmo_get_operation(TucanoRuntime* rt) {
  if (!rt) return TUCANO_GIZMO_TRANSLATE;
  switch (rt->gizmoOp) {
    case tucano::DebugUI::GizmoOp::Rotate: return TUCANO_GIZMO_ROTATE;
    case tucano::DebugUI::GizmoOp::Scale: return TUCANO_GIZMO_SCALE;
    case tucano::DebugUI::GizmoOp::Translate: break;
  }
  return TUCANO_GIZMO_TRANSLATE;
}

TUCANO_API void tucano_gizmo_set_enabled(TucanoRuntime* rt, bool enabled) {
  if (rt) rt->gizmoEnabled = enabled;
}

TUCANO_API bool tucano_gizmo_get_enabled(TucanoRuntime* rt) {
  return rt && rt->gizmoEnabled;
}

TUCANO_API void tucano_gizmo_set_world_space(TucanoRuntime* rt, bool worldSpace) {
  if (rt) rt->gizmoWorldSpace = worldSpace;
}

TUCANO_API bool tucano_gizmo_get_world_space(TucanoRuntime* rt) {
  return rt && rt->gizmoWorldSpace;
}

TUCANO_API void tucano_gizmo_set_snap(TucanoRuntime* rt, float snap) {
  if (rt) rt->gizmoSnap = snap > 0.0f ? snap : 0.0f;
}

TUCANO_API float tucano_gizmo_get_snap(TucanoRuntime* rt) {
  return rt ? rt->gizmoSnap : 0.0f;
}

TUCANO_API void tucano_gizmo_set_scale(TucanoRuntime* rt, float scale) {
  if (rt) rt->gizmoScale = scale > 0.0f ? scale : 1.0f;
}

TUCANO_API float tucano_gizmo_get_scale(TucanoRuntime* rt) {
  return rt ? rt->gizmoScale : 1.0f;
}

// ── Stats ────────────────────────────────────────────

TUCANO_API float tucano_runtime_last_frame_ms(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return 0.0f;
  return rt->renderer->lastFrameMs();
}

TUCANO_API uint32_t tucano_runtime_draw_calls(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return 0;
  return rt->renderer->drawCalls();
}

TUCANO_API uint32_t tucano_runtime_meshlets_drawn(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return 0;
  return rt->renderer->meshletsDrawn();
}

// ── Renderer settings ────────────────────────────────

TUCANO_API void tucano_settings_set_time_of_day(TucanoRuntime* rt, float tod) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().timeOfDay = tod;
}

TUCANO_API float tucano_settings_get_time_of_day(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return 0.0f;
  return rt->renderer->settings().timeOfDay;
}

TUCANO_API void tucano_settings_set_cloud_coverage(TucanoRuntime* rt, float c) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().cloudCoverage = c;
}

TUCANO_API float tucano_settings_get_cloud_coverage(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return 0.0f;
  return rt->renderer->settings().cloudCoverage;
}

TUCANO_API void tucano_settings_set_enable_bloom(TucanoRuntime* rt, bool b) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().enableBloom = b;
}

TUCANO_API bool tucano_settings_get_enable_bloom(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return false;
  return rt->renderer->settings().enableBloom;
}

TUCANO_API void tucano_settings_set_enable_ao(TucanoRuntime* rt, bool b) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().enableAO = b;
}

TUCANO_API bool tucano_settings_get_enable_ao(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return false;
  return rt->renderer->settings().enableAO;
}

TUCANO_API void tucano_settings_set_enable_clouds(TucanoRuntime* rt, bool b) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().enableClouds = b;
}

TUCANO_API bool tucano_settings_get_enable_clouds(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return false;
  return rt->renderer->settings().enableClouds;
}

TUCANO_API void tucano_settings_set_enable_atmosphere(TucanoRuntime* rt, bool b) {
  if (!rt || !rt->renderer) return;
  rt->renderer->settings().enableAtmosphere = b;
}

TUCANO_API bool tucano_settings_get_enable_atmosphere(TucanoRuntime* rt) {
  if (!rt || !rt->renderer) return false;
  return rt->renderer->settings().enableAtmosphere;
}

// ── World Machine: GPU cell-cull parity self-test (WM-4) ─────────────────────
// Builds a known set of cell boxes and a known camera, culls them on the CPU (the reference) and
// on the GPU (CellCull.hlsl via GpuCellCuller), and returns the number of DISAGREEMENTS between
// the two visible sets. Zero means the compute shader matches the reference exactly — the proof
// that the GPU path is correct. Exposed here because the parity test needs a live device, which
// only exists inside the runtime.
TUCANO_API int tucano_world_cull_selftest(TucanoRuntime* rt) {
  if (!rt || !rt->device) return -1;

  using namespace tucano::world;

  // A deterministic field of cells spread across a volume, plus a camera looking down +z. Some
  // cells sit inside the frustum, some outside, some straddling an edge — the cases that catch a
  // plane-convention bug.
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  const float cell = grid.cellSize(10);
  std::vector<CellId> ids;
  std::vector<glm::vec3> mins, maxs;
  for (int z = -6; z <= 40; ++z) {
    for (int y = -6; y <= 6; ++y) {
      for (int x = -20; x <= 20; ++x) {
        const CellId id{x, y, z, 10};
        glm::vec3 bmin, bmax;
        grid.boundsOf(id, bmin, bmax);
        ids.push_back(id);
        mins.push_back(bmin);
        maxs.push_back(bmax);
      }
    }
  }

  // Camera at the origin looking down +z (left-handed), matching the engine's projection.
  const glm::vec3 eye(cell * 0.5f, cell * 0.5f, -cell);
  const glm::mat4 view = glm::lookAtLH(eye, eye + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 5000.0f);
  const glm::mat4 viewProj = proj * view;

  CullConfig cfg;
  cfg.lodStep = 200.0f;
  cfg.maxLod = 4;
  cfg.maxDistance = 3000.0f;

  // Reference.
  WorldCuller cpuCuller;
  std::vector<VisibleCell> cpu;
  cpuCuller.cullBoxes(ids, mins, maxs, viewProj, eye, cfg, cpu);

  // GPU. Build a throwaway compute root signature + PSO for CellCull.
  std::shared_ptr<tucano::rhi::RootSignature> root;
  std::shared_ptr<tucano::rhi::PipelineState> pso;
  try {
    root = rt->device->createComputeRootSignature();
    tucano::rhi::ComputePipelineDesc pd;
    pd.rootSignature = root;
    pd.cs = tucano::rhi::ShaderBytecode::loadFromFile(
        std::string(TUCANO_SHADER_DIR) + "/CellCull_CSMain.cso");
    pso = rt->device->createComputePipeline(pd);
  } catch (...) {
    return -2; // shader missing or PSO creation failed
  }

  GpuCellCuller gpuCuller(*rt->device, *root, *pso);
  std::vector<VisibleCell> gpu = gpuCuller.cull(ids, mins, maxs, viewProj, eye, cfg);
  std::fprintf(stderr, "[cullselftest] cells=%zu cpuVisible=%zu gpuVisible=%zu\n", ids.size(),
               cpu.size(), gpu.size());

  // Compare as sets keyed by cell, checking LOD agrees too. A mismatch is any cell one side found
  // and the other did not, or a cell both found but assigned a different LOD.
  std::unordered_map<uint64_t, uint32_t> cpuMap, gpuMap;
  for (const auto& v : cpu) cpuMap[v.id.key()] = v.lod;
  for (const auto& v : gpu) gpuMap[v.id.key()] = v.lod;

  int mismatches = 0;
  for (const auto& [key, lod] : cpuMap) {
    auto it = gpuMap.find(key);
    if (it == gpuMap.end() || it->second != lod) ++mismatches;
  }
  for (const auto& [key, lod] : gpuMap) {
    if (cpuMap.find(key) == cpuMap.end()) ++mismatches;
  }

  // Encode both the mismatch count and a sanity signal: if the CPU found nothing, the scene was
  // degenerate and the test proves nothing — report that as a failure rather than a false pass.
  if (cpu.empty()) return -3;
  return mismatches;
}

// Returns how many cells the CPU reference finds visible in the self-test scene — lets the caller
// confirm the test actually exercised the frustum (a non-trivial visible count).
TUCANO_API int tucano_world_cull_selftest_visible_count(TucanoRuntime* rt) {
  if (!rt || !rt->device) return -1;
  using namespace tucano::world;
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  std::vector<CellId> ids;
  std::vector<glm::vec3> mins, maxs;
  for (int z = -6; z <= 40; ++z)
    for (int y = -6; y <= 6; ++y)
      for (int x = -20; x <= 20; ++x) {
        const CellId id{x, y, z, 10};
        glm::vec3 bmin, bmax;
        grid.boundsOf(id, bmin, bmax);
        ids.push_back(id);
        mins.push_back(bmin);
        maxs.push_back(bmax);
      }
  const float c = grid.cellSize(10);
  const glm::vec3 eye(c * 0.5f, c * 0.5f, -c);
  const glm::mat4 view = glm::lookAtLH(eye, eye + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 5000.0f);
  CullConfig cfg;
  cfg.maxDistance = 3000.0f;
  WorldCuller cpuCuller;
  std::vector<VisibleCell> cpu;
  cpuCuller.cullBoxes(ids, mins, maxs, proj * view, eye, cfg, cpu);
  return int(cpu.size());
}

// ── Audio (Phase I-2) ────────────────────────────────

TUCANO_API bool tucano_audio_init(TucanoRuntime* rt) {
  if (!rt) return false;
  tucano::Audio::instance().init();
  return tucano::Audio::instance().isInitialized();
}

TUCANO_API void tucano_audio_shutdown(TucanoRuntime* rt) {
  (void)rt;
  tucano::Audio::instance().shutdown();
}

TUCANO_API bool tucano_audio_is_initialized(TucanoRuntime* rt) {
  (void)rt;
  return tucano::Audio::instance().isInitialized();
}

TUCANO_API void tucano_audio_set_master_volume(TucanoRuntime* rt, float volume) {
  (void)rt;
  tucano::Audio::instance().setMasterVolume(glm::clamp(volume, 0.0f, 1.0f));
}

TUCANO_API float tucano_audio_get_master_volume(TucanoRuntime* rt) {
  (void)rt;
  return tucano::Audio::instance().masterVolume();
}

TUCANO_API void tucano_audio_set_paused(TucanoRuntime* rt, bool paused) {
  (void)rt;
  tucano::Audio::instance().setPaused(paused);
}

TUCANO_API bool tucano_audio_is_paused(TucanoRuntime* rt) {
  (void)rt;
  return tucano::Audio::instance().isPaused();
}

TUCANO_API int32_t tucano_audio_load_clip(TucanoRuntime* rt, const char* path) {
  if (!rt || !path) return -1;
  auto* clip = tucano::AudioClip::loadWav(path);
  if (!clip) return -1;
  rt->audioClips.push_back(clip);
  return static_cast<int32_t>(rt->audioClips.size() - 1);
}

TUCANO_API void tucano_audio_unload_clip(TucanoRuntime* rt, int32_t clipId) {
  if (!rt || clipId < 0 || clipId >= static_cast<int32_t>(rt->audioClips.size())) return;
  if (rt->audioClips[clipId]) {
    rt->audioClips[clipId]->release();
    rt->audioClips[clipId] = nullptr;
  }
}

TUCANO_API float tucano_audio_clip_duration(TucanoRuntime* rt, int32_t clipId) {
  if (!rt || clipId < 0 || clipId >= static_cast<int32_t>(rt->audioClips.size())) return 0.0f;
  return rt->audioClips[clipId] ? rt->audioClips[clipId]->durationSeconds() : 0.0f;
}

TUCANO_API int32_t tucano_audio_create_source(TucanoRuntime* rt) {
  if (!rt) return -1;
  auto src = std::make_unique<tucano::AudioSource>();
  rt->audioSources.push_back(std::move(src));
  return static_cast<int32_t>(rt->audioSources.size() - 1);
}

TUCANO_API void tucano_audio_destroy_source(TucanoRuntime* rt, int32_t sourceId) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId].reset();
}

TUCANO_API void tucano_audio_source_play(TucanoRuntime* rt, int32_t sourceId, int32_t clipId,
                                          float volume, bool loop) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  auto* clip = (clipId >= 0 && clipId < static_cast<int32_t>(rt->audioClips.size()))
                    ? rt->audioClips[clipId] : nullptr;
  if (!clip) return;
  rt->audioSources[sourceId]->play(clip, volume, loop);
}

TUCANO_API void tucano_audio_source_stop(TucanoRuntime* rt, int32_t sourceId) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->stop();
}

TUCANO_API void tucano_audio_source_pause(TucanoRuntime* rt, int32_t sourceId) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->pause();
}

TUCANO_API void tucano_audio_source_resume(TucanoRuntime* rt, int32_t sourceId) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->resume();
}

TUCANO_API bool tucano_audio_source_is_playing(TucanoRuntime* rt, int32_t sourceId) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return false;
  return rt->audioSources[sourceId]->isPlaying();
}

TUCANO_API void tucano_audio_source_set_position(TucanoRuntime* rt, int32_t sourceId, TucanoVec3 pos) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->setPosition({pos.x, pos.y, pos.z});
}

TUCANO_API void tucano_audio_source_set_volume(TucanoRuntime* rt, int32_t sourceId, float volume) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->setVolume(glm::clamp(volume, 0.0f, 1.0f));
}

TUCANO_API void tucano_audio_source_set_looping(TucanoRuntime* rt, int32_t sourceId, bool loop) {
  if (!rt || sourceId < 0 || sourceId >= static_cast<int32_t>(rt->audioSources.size())) return;
  rt->audioSources[sourceId]->setLooping(loop);
}

TUCANO_API void tucano_audio_listener_set_position(TucanoRuntime* rt, TucanoVec3 pos) {
  (void)rt;
  tucano::AudioListener::instance().setPosition({pos.x, pos.y, pos.z});
}

TUCANO_API void tucano_audio_listener_set_orientation(TucanoRuntime* rt, TucanoVec3 forward, TucanoVec3 up) {
  (void)rt;
  tucano::AudioListener::instance().setOrientation({forward.x, forward.y, forward.z}, {up.x, up.y, up.z});
}

// ── Terrain ──────────────────────────────────────────

TUCANO_API bool tucano_terrain_create_procedural(TucanoRuntime* rt, uint32_t resolution, float worldSize,
                                                  uint32_t octaves, float persistence, float amplitude,
                                                  float baseHeight, uint32_t seed) {
  if (!rt || !rt->device) return false;
  tucano::terrain::TerrainGenParams params;
  params.resolution = resolution;
  params.worldSize = worldSize;
  params.octaves = octaves;
  params.persistence = persistence;
  params.baseAmplitude = amplitude;
  params.baseHeight = baseHeight;
  params.seed = seed;
  rt->terrainHm = tucano::terrain::TerrainGenerator::generate(*rt->device, params);
  if (!rt->terrainHm) return false;
  auto mat = std::make_shared<tucano::Material>();
  mat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
  mat->albedo = tucano::devtex::defaultFloor(*rt->device);
  mat->normal = tucano::devtex::defaultNormal(*rt->device);
  mat->roughnessFactor = 0.85f;
  mat->metallicFactor = 0.0f;
  rt->terrainComp = std::make_unique<tucano::terrain::TerrainComponent>(*rt->device, rt->terrainHm, mat);
  if (rt->physics) rt->terrainComp->createPhysicsBody(*rt->physics);
  rt->scene.objects.push_back(rt->terrainComp->createRenderObject());
  rt->terrainObjectIndex = int(rt->scene.objects.size()) - 1;
  rt->terrainBrushes = std::make_unique<tucano::terrain::BrushSystem>();
  rt->terrainBrushes->setMaxUndoDepth(256);
  rt->terrainErosion = std::make_unique<tucano::terrain::ErosionSimulation>();
  return true;
}

TUCANO_API void tucano_terrain_destroy(TucanoRuntime* rt) {
  if (!rt) return;
  if (rt->terrainComp && rt->physics) rt->terrainComp->removePhysicsBody(*rt->physics);
  rt->terrainComp.reset();
  rt->terrainHm.reset();
  rt->terrainBrushes.reset();
  rt->terrainErosion.reset();
}

TUCANO_API bool tucano_terrain_undo(TucanoRuntime* rt) {
  if (!rt || !rt->terrainHm || !rt->terrainBrushes || !rt->terrainComp) return false;
  rt->terrainBrushes->undo(*rt->terrainHm);
  rt->terrainHm->uploadToGPU(*rt->device);
  rt->terrainComp->regenerateMesh(*rt->device);
  if (rt->terrainObjectIndex >= 0 && size_t(rt->terrainObjectIndex) < rt->scene.objects.size()) rt->scene.objects[rt->terrainObjectIndex].mesh = rt->terrainComp->mesh();
  return true;
}

TUCANO_API bool tucano_terrain_redo(TucanoRuntime* rt) {
  if (!rt || !rt->terrainHm || !rt->terrainBrushes || !rt->terrainComp) return false;
  rt->terrainBrushes->redo(*rt->terrainHm);
  rt->terrainHm->uploadToGPU(*rt->device);
  rt->terrainComp->regenerateMesh(*rt->device);
  if (rt->terrainObjectIndex >= 0 && size_t(rt->terrainObjectIndex) < rt->scene.objects.size()) rt->scene.objects[rt->terrainObjectIndex].mesh = rt->terrainComp->mesh();
  return true;
}

TUCANO_API bool tucano_terrain_export(TucanoRuntime* rt, const char* path) {
  if (!rt || !rt->terrainHm || !path) return false;
  return tucano::terrain::Heightmap::saveToFile(*rt->terrainHm, path);
}

TUCANO_API bool tucano_terrain_erode(TucanoRuntime* rt, uint32_t iterations, float erosionRate) {
  if (!rt || !rt->terrainHm || !rt->terrainErosion || !rt->terrainComp) return false;
  tucano::terrain::ErosionParams ep;
  ep.iterations = iterations;
  ep.erosionRate = erosionRate;
  rt->terrainErosion->erode(*rt->terrainHm, ep);
  rt->terrainHm->uploadToGPU(*rt->device);
  rt->terrainComp->regenerateMesh(*rt->device);
  if (rt->terrainObjectIndex >= 0 && size_t(rt->terrainObjectIndex) < rt->scene.objects.size()) rt->scene.objects[rt->terrainObjectIndex].mesh = rt->terrainComp->mesh();
  return true;
}

TUCANO_API bool tucano_terrain_thermal_erode(TucanoRuntime* rt, float talusAngle, uint32_t iterations) {
  if (!rt || !rt->terrainHm || !rt->terrainErosion || !rt->terrainComp) return false;
  rt->terrainErosion->applyThermalErosion(*rt->terrainHm, talusAngle, iterations);
  rt->terrainHm->uploadToGPU(*rt->device);
  rt->terrainComp->regenerateMesh(*rt->device);
  if (rt->terrainObjectIndex >= 0 && size_t(rt->terrainObjectIndex) < rt->scene.objects.size()) rt->scene.objects[rt->terrainObjectIndex].mesh = rt->terrainComp->mesh();
  return true;
}

TUCANO_API bool tucano_terrain_get_info(TucanoRuntime* rt, TucanoTerrainInfo* outInfo) {
  if (!rt || !rt->terrainHm || !outInfo) return false;
  outInfo->resolution = rt->terrainHm->resolution();
  outInfo->worldSize = rt->terrainHm->worldSize();
  outInfo->minHeight = rt->terrainHm->minHeight();
  outInfo->maxHeight = rt->terrainHm->maxHeight();
  outInfo->undoCount = rt->terrainBrushes ? uint32_t(rt->terrainBrushes->undoCount()) : 0;
  outInfo->redoCount = rt->terrainBrushes ? uint32_t(rt->terrainBrushes->redoCount()) : 0;
  return true;
}

TUCANO_API void tucano_terrain_set_sculpt_active(TucanoRuntime* rt, bool active) {
  if (rt) rt->terrainSculptActive = active;
}

TUCANO_API bool tucano_terrain_get_sculpt_active(TucanoRuntime* rt) {
  return rt && rt->terrainSculptActive;
}

TUCANO_API void tucano_terrain_set_sculpt_params(TucanoRuntime* rt, float radius, float strength, int tool) {
  if (!rt) return;
  rt->terrainBrushRadius = radius;
  rt->terrainBrushStrength = strength;
  rt->terrainBrushTool = tool;
}

TUCANO_API void tucano_material_graph_open(TucanoRuntime* rt) {
  if (!rt) return;
  if (!rt->materialNodeEditor)
    rt->materialNodeEditor = std::make_unique<tucano::terrain::MaterialNodeEditor>();
  rt->materialNodeEditor->open(tucano::terrain::MaterialGraphData::createDefault());
  rt->materialGraphOpen = true;
}

TUCANO_API void tucano_material_graph_close(TucanoRuntime* rt) {
  if (!rt) return;
  rt->materialGraphOpen = false;
}

// ── World streaming / Outliner (WM-9) ────────────────

TUCANO_API bool tucano_world_stream_start(TucanoRuntime* rt, int32_t extent, float loadRadius,
                                          uint32_t density) {
  if (!rt || !rt->device) return false;
  tucano_world_stream_stop(rt); // clean any previous session

  const tucano::world::WorldGridDesc gd{glm::vec3(0.0f), 65536.0f, 10};
  rt->streamWorldRoot = "editor_stream_world";

  // Bake a procedural world on first run. Skip if already exists.
  {
    tucano::world::WorldGrid bakeGrid(gd);
    tucano::world::WorldGenSettings gen;
    gen.outputRoot = rt->streamWorldRoot;
    gen.level = gd.streamLevel;
    gen.extentCells = extent > 0 ? extent : 6;
    gen.gameplayPerCell = std::max(1u, density / 2);
    gen.visualPerCell = std::max(1u, density / 2);
    gen.detailPerCell = std::max(1u, density);

    // Check if world already exists -- skip slow generation
    std::error_code ec;
    if (!std::filesystem::exists(rt->streamWorldRoot, ec) ||
        std::filesystem::is_empty(rt->streamWorldRoot, ec)) {
      tucano::world::WorldGenStats gs;
      if (!tucano::world::generateWorld(gen, bakeGrid, gs)) return false;
    }
  }

  rt->streamGrid = std::make_unique<tucano::world::WorldGrid>(gd);
  rt->streamTasks = std::make_unique<tucano::core::TaskScheduler>();
  rt->streamBudget = std::make_unique<tucano::world::StreamingBudget>();
  rt->streamProvider = std::make_unique<tucano::world::SceneCellProvider>(*rt->device, rt->scene,
                                                                          rt->streamWorldRoot);
  tucano::world::StreamingSchedulerDesc sd;
  sd.streamLevel = gd.streamLevel;
  sd.maxConcurrentLoads = 24;
  sd.unloadGraceFrames = 30;
  rt->streamer = std::make_unique<tucano::world::StreamingScheduler>(
      *rt->streamGrid, *rt->streamTasks, *rt->streamBudget, *rt->streamProvider, sd);

  rt->streamObserver = {};
  rt->streamObserver.id = 1;
  rt->streamObserver.loadRadius = loadRadius > 0.0f ? loadRadius : 200.0f;
  rt->streamObserver.unloadRadius = rt->streamObserver.loadRadius * 1.5f;
  rt->streamingActive = true;
  return true;
}

TUCANO_API void tucano_world_stream_stop(TucanoRuntime* rt) {
  if (!rt) return;
  rt->streamingActive = false;
  if (rt->streamer) {
    // Unload cleanly so the streamed objects leave the scene rather than lingering.
    rt->streamer->setObservers({});
    for (int i = 0; i < 400 && rt->streamProvider && rt->streamProvider->liveObjectCount() > 0; ++i) {
      rt->streamer->update(16.0f);
    }
  }
  rt->streamer.reset();
  rt->streamProvider.reset();
  rt->streamBudget.reset();
  rt->streamTasks.reset();
  rt->streamGrid.reset();
}

TUCANO_API bool tucano_world_stream_active(TucanoRuntime* rt) {
  return rt && rt->streamingActive;
}

TUCANO_API void tucano_world_stream_get_stats(TucanoRuntime* rt, TucanoStreamStats* out) {
  if (!out) return;
  *out = {};
  if (!rt || !rt->streamer) return;
  const auto& s = rt->streamer->stats();
  out->cellsResident = s.cellsResident;
  out->cellsLoading = s.cellsLoading;
  out->layersLoaded = s.layersLoaded;
  out->cellsWanted = s.cellsWanted;
  out->cellsMissing = s.cellsMissing;
  out->loadsStarted = s.loadsStarted;
  out->loadsCompleted = s.loadsCompleted;
  out->unloadsIssued = s.unloadsIssued;
  out->cancellations = s.cancellations;
  out->lodSwitches = s.lodSwitches;
  out->reloadsIssued = s.reloadsIssued;
  out->budgetRejections = s.budgetRejections;
  out->cpuBytes = s.cpuBytes;
  out->gpuBytes = s.gpuBytes;
  if (rt->streamProvider) {
    out->liveObjects = uint32_t(rt->streamProvider->liveObjectCount());
    out->liveBodies = uint32_t(rt->streamProvider->liveBodyCount());
  }
}

TUCANO_API uint32_t tucano_world_stream_resident_cells(TucanoRuntime* rt, TucanoStreamCell* out,
                                                       uint32_t max) {
  if (!rt || !rt->streamer || !out || max == 0) return 0;
  std::vector<tucano::world::StreamingScheduler::ResidentCellInfo> cells;
  rt->streamer->snapshotResident(cells);
  const uint32_t n = std::min<uint32_t>(max, uint32_t(cells.size()));
  for (uint32_t i = 0; i < n; ++i) {
    const auto& c = cells[i];
    out[i].x = c.id.x;
    out[i].y = c.id.y;
    out[i].z = c.id.z;
    out[i].level = c.id.level;
    out[i].layerMask = c.layerMask;
    out[i].loadingMask = c.loadingMask;
    out[i].lod = c.lod;
    out[i].distance = c.distance;
  }
  return n;
}

TUCANO_API void tucano_world_stream_reload_cell(TucanoRuntime* rt, int32_t x, int32_t y, int32_t z,
                                                uint32_t level) {
  if (!rt || !rt->streamer) return;
  rt->streamer->requestReload(tucano::world::CellId{x, y, z, level});
}


TUCANO_API int tucano_asset_import(const char* path, const char* outputDir) {
  if (!path || !outputDir) return 0;
  return tucano::importModelAsTuasset(path, outputDir);
}

TUCANO_API void tucano_asset_import_async(TucanoRuntime* rt, const char* path, const char* outputDir) {
  if (!rt || !path || !outputDir) return;
  std::string p(path), o(outputDir);
  rt->importProgress.store(0.0f);
  rt->importDone.store(false);
  rt->importCount.store(0);
  {
    std::lock_guard<std::mutex> lock(rt->importMutex);
    rt->importStatus = "Parsing...";
  }

  if (rt->importThread.joinable()) rt->importThread.join();

  rt->importThread = std::thread([rt, p, o]() {
    {
      std::lock_guard<std::mutex> lock(rt->importMutex);
      rt->importStatus = "Converting...";
    }
    int count = tucano::importModelAsTuasset(p, o);
    rt->importProgress.store(1.0f);
    rt->importCount.store(count);
    {
      std::lock_guard<std::mutex> lock(rt->importMutex);
      rt->importStatus = count > 0 ? ("Done: " + std::to_string(count) + " assets") : "Failed";
    }
    rt->importDone.store(true);
  });
}

TUCANO_API float tucano_asset_import_progress(TucanoRuntime* rt) {
  return rt ? rt->importProgress.load() : 0.0f;
}

TUCANO_API bool tucano_asset_import_is_done(TucanoRuntime* rt) {
  return rt ? rt->importDone.load() : true;
}

TUCANO_API int tucano_asset_import_count(TucanoRuntime* rt) {
  return rt ? rt->importCount.load() : 0;
}
