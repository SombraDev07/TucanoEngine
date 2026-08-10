#include "AssetPipeline/GLTFLoader.h"
#include "Editor/EditorShell.h"
#include "Editor/DemoTools.h"
#include "Editor/EditorTool.h"
#include "Editor/EditorContext.h"
#include "AssetPipeline/AssetRegistry.h"
#include "AssetPipeline/AssetResolver.h"
#include "ECS/RenderSync.h"
#include "Editor/PlayMode.h"
#include "Editor/SceneTool.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/PhysicsSync.h"
#include "ECS/World.h"
#include "Editor/SystemDialogs.h"
#include "Editor/ToolHost.h"
#include "Editor/UI/Gallery.h"
#include "Editor/UI/Notifications.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"
#include "Lua/LuaVM.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <iostream>
#include <memory>

using namespace tucano;

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

void setupDefaultRain(RainParams& rain) {
  rain.enabled = true;
  rain.amount = 0.95f;
  rain.streakIntensity = 1.15f;
  rain.streakSpeed = 1.85f;
  rain.streakLayers = 3.0f;
  rain.rainDropsAmount = 0.55f;
  rain.rainDropsLighting = 1.2f;
  rain.puddlesAmount = 1.45f;
  rain.splashesAmount = 0.9f;
  rain.diffuseDarkening = 0.75f;
  rain.glossBoost = 1.2f;
  rain.mistAmount = 0.35f;
  rain.wind = {0.2f, 0.0f, 0.05f};
}

} // namespace

int main(int argc, char** argv) {
  std::string scenePath = "Assets/Sponza/Sponza.gltf";
  std::string screenshotPath;
  std::string layoutPath = "EditorLayout.ini";
  int maxFrames = -1;
  int shotFrame = 5;
  bool editorMode = false;
  bool uiGallery = false;
  bool toolsDemo = false;
  int closeDirtyAtFrame = -1;
  bool borderless = false;
  bool sceneTool = false;
  // Selection normally comes from a click in the Outliner, which an unattended screenshot run has
  // no way to make. -1 keeps the default "nothing selected".
  int selectObject = -1;
  // Same reason as --select, for the entity path: the Inspector only shows components once
  // something is selected, and an unattended screenshot has no way to click the Outliner.
  int selectEntity = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--scene" && i + 1 < argc) {
      scenePath = argv[++i];
    } else if (a == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
    } else if (a == "--frames" && i + 1 < argc) {
      maxFrames = std::stoi(argv[++i]);
    } else if (a == "--shotframe" && i + 1 < argc) {
      // Editor UI needs a few frames to settle (dock layout build, first-use sizing), so a gate
      // that screenshots the editor wants a later frame than the renderer gate does.
      shotFrame = std::stoi(argv[++i]);
    } else if (a == "--editor") {
      editorMode = true;
    } else if (a == "--tools-demo") {
      // Implies --editor: shows two EditorTools side by side as tabs, each with its own layout.
      editorMode = true;
      toolsDemo = true;
    } else if (a == "--scene-tool") {
      // The real panels (P2-06) hosted by SceneTool, instead of the demo stand-ins.
      editorMode = true;
      sceneTool = true;
    } else if (a == "--select" && i + 1 < argc) {
      selectObject = std::stoi(argv[++i]);
    } else if (a == "--select-entity" && i + 1 < argc) {
      selectEntity = std::stoi(argv[++i]);
    } else if (a == "--borderless") {
      editorMode = true;
      borderless = true;
    } else if (a == "--tools-demo-close" && i + 1 < argc) {
      // Requests closing the dirty demo tool at a given frame, so the save prompt is on screen for a
      // screenshot. Interactive verification would need a click, which a headless run cannot make.
      editorMode = true;
      toolsDemo = true;
      closeDirtyAtFrame = std::stoi(argv[++i]);
    } else if (a == "--ui-gallery") {
      // Implies --editor: the gallery is part of the editor UI, not the viewer.
      editorMode = true;
      uiGallery = true;
    } else if (a == "--layout" && i + 1 < argc) {
      // Lets the layout gate use a throwaway ini instead of stomping the dev's own.
      layoutPath = argv[++i];
    }
  }

  try {
    Window window({1920, 1080, "Tucano — Sponza Viewer"});
    auto device = rhi::Device::create(true);
    auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
    auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
    // Keep heavy experimental passes off; RT enables automatically when DXR is present.
    renderer->settings().enableVSM = false;
    renderer->settings().enableAsyncCompute = false;
    renderer->settings().enableOctahedralPointShadows = false;
    renderer->settings().enableVisibilityBuffer = false;
    renderer->settings().enableGpuMeshletCull = false;
    renderer->settings().enableMeshShaders = false;
    renderer->settings().enableMeshlets = false;
    renderer->settings().enableHiZOcclusion = false;
    renderer->settings().enableVoxelGI = false;
    renderer->settings().enableToroidalShadows = false;
    renderer->settings().giTier = GITier::Off;
    if (device->supportsRaytracing()) {
      renderer->settings().enableRTShadows = true;
      renderer->settings().enableRTReflections = true;
      renderer->settings().enableSSR = true;
      renderer->settings().enableContactShadows = true;
      renderer->settings().enableShadows = true;
      std::cout << "[SponzaViewer] DXR ON — RT shadows + reflections + contact\n";
    } else {
      renderer->settings().enableRTShadows = false;
      renderer->settings().enableRTReflections = false;
      renderer->settings().enableSSR = true;
      renderer->settings().enableContactShadows = true;
      std::cout << "[SponzaViewer] No DXR — CSM + SSR fallback\n";
    }
    Input input(window.handle());
    DebugUI ui;
    ui.init(window, *device);

    // Editor mode (Track D0): docking shell over the live scene. Must init after DebugUI (it needs
    // the ImGui context) and before the first frame (so ImGui loads the layout ini).
    editor::EditorShell shell;
    editor::EditorContext editorContext;
    editor::EditorTool* dirtyDemoTool = nullptr;
    editor::SceneTool* sceneToolPtr = nullptr;
    if (editorMode) {
      if (shell.init(layoutPath)) {
        window.setTitle("Tucano Editor");
        std::cout << "[SponzaViewer] Editor mode — layout: " << layoutPath << "\n";
      } else {
        std::cerr << "[SponzaViewer] Editor mode unavailable (no ImGui context)\n";
      }
      // File menu wired to the OS dialogs. People already know their own file picker; an in-engine
      // browser is a worse copy that has to be maintained forever.
      shell.onOpenScene = [] {
        const std::string path =
            editor::openFileDialog("Open Scene", {{"Scenes", "*.gltf;*.glb;*.scn"}});
        if (path.empty()) {
          editor::ui::notifyInfo("Open cancelled.");
        } else {
          editor::ui::notifySuccess("Selected: %s", path.c_str());
        }
      };
      shell.onSaveScene = [] {
        const std::string path =
            editor::saveFileDialog("Save Scene", {{"Scenes", "*.scn"}}, "Untitled.scn");
        if (!path.empty()) editor::ui::notifySuccess("Would save to: %s", path.c_str());
      };
      shell.onImportAsset = [] {
        const std::vector<std::string> paths = editor::openFilesDialog(
            "Import Assets", {{"Models", "*.gltf;*.glb;*.fbx"}, {"Textures", "*.png;*.dds;*.exr"}});
        if (!paths.empty()) editor::ui::notifySuccess("%zu asset(s) selecionado(s)", paths.size());
      };

      if (borderless) {
        if (shell.enableBorderlessTitleBar(window.nativeHandle())) {
          std::cout << "[SponzaViewer] Borderless title bar ativo\n";
        } else {
          std::cerr << "[SponzaViewer] Nao foi possivel instalar o chrome sem borda\n";
        }
      }

      if (sceneTool) {
        auto tool = std::make_unique<editor::SceneTool>();
        tool->setContext(&editorContext);
        auto* scenes = static_cast<editor::SceneTool*>(shell.addTool(std::move(tool)));
        sceneToolPtr = scenes;
        scenes->setDialogs(&shell.toolHost().dialogs());
        // B-01: index the project once at startup. Sidecars are created here because this *is*
        // the project being authored; a read-only viewer would pass false.
        const size_t indexed = scenes->scanAssets("Assets", /*createMissingMeta=*/true);
        std::cout << "[SponzaViewer] Asset registry: " << indexed << " assets\n";


        // C-04: File -> New/Open/Save actually do it now. The tool owns the document — its path,
        // its dirty flag and its undo stack — so the menu delegates rather than duplicating that
        // state in the sample.
        shell.onNewScene = [scenes] {
          scenes->requestNewScene();
          editor::ui::notifyInfo("New scene.");
        };
        shell.onOpenScene = [scenes] {
          scenes->requestOpenScene();
          if (!scenes->error().empty()) {
            editor::ui::notifyError("Open failed: %s", scenes->error().c_str());
          } else if (!scenes->documentPath().empty()) {
            editor::ui::notifySuccess("Opened %s", scenes->documentPath().c_str());
          }
        };
        shell.onImportAsset = [scenes] {
          scenes->importAssetDialog();
          if (!scenes->error().empty()) {
            editor::ui::notifyError("Import failed: %s", scenes->error().c_str());
          }
        };
        shell.onSaveScene = [scenes] {
          if (scenes->saveScene()) {
            editor::ui::notifySuccess("Saved %s", scenes->documentPath().c_str());
          } else if (!scenes->error().empty()) {
            editor::ui::notifyError("Save failed: %s", scenes->error().c_str());
          }
        };
      }

      if (toolsDemo) {
        // Two tools with deliberately different layouts — the point of P2-01 is that each keeps its
        // own arrangement instead of sharing one global dockspace.
        shell.addTool(editor::makeSceneDemoTool());
        editor::EditorTool* material =
            shell.addTool(editor::makeMaterialDemoTool("Assets/Materials/Material_1.tmat"));
        // Starts dirty so the unsaved marker and the close prompt are reachable in the demo without
        // having to edit something first.
        if (material != nullptr) material->markDirty();
        dirtyDemoTool = material;
        std::cout << "[SponzaViewer] Tools demo: " << shell.tools().size() << " ferramentas\n";
      }
    }

    Scene scene;
    auto applyFabricFuzz = [&]() {
      for (auto& obj : scene.objects) {
        for (auto& mat : obj.materials) {
          if (!mat) {
            continue;
          }
          const std::string& n = mat->name;
          const bool fabric = n.find("fabric") != std::string::npos || n.find("Fabric") != std::string::npos ||
                              n.find("curtain") != std::string::npos || n.find("Curtain") != std::string::npos;
          if (fabric) {
            mat->fuzz = 0.55f;
            mat->fuzzColor = {0.92f, 0.88f, 0.82f};
            mat->detailScale = 8.0f;
            mat->detailAlbedo = mat->albedo;
            mat->detailNormal = mat->normal;
          }
        }
      }
    };

    auto reloadScene = [&]() {
      scene.objects.clear();
      if (!loadGLTFScene(*device, scenePath, scene)) {
        std::cerr << "Failed to load scene: " << scenePath << "\n";
        std::cerr << "Place Khronos Sponza glTF under Assets/Sponza/\n";
      }
      applyFabricFuzz();
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 300.0f);
      setupDefaultLights(scene);
    };

    reloadScene();

    // C-02c: the editor authors entities, and `Scene` is the render view fed from them. The glTF
    // loader still produces RenderObjects, so one entity is created per object and linked to it by
    // RenderObjectComponent::sceneIndex — the bridge the engine already had. Moving an entity in
    // the Inspector therefore moves what is drawn.
    ecs::World world;
    const auto populateWorldFromScene = [&]() {
      for (size_t i = 0; i < scene.objects.size(); ++i) {
        const RenderObject& object = scene.objects[i];
        const ecs::Entity entity = world.create();
        world.add<ecs::NameComponent>(entity)->name =
            object.name.empty() ? "Object" : std::string_view(object.name);
        auto* transform = world.add<ecs::TransformComponent>(entity);
        transform->position = object.transform.translation;
        transform->rotation = object.transform.rotation;
        transform->scale = object.transform.scale;
        // Seeded so the first interpolated frame does not lerp in from the origin.
        transform->prevPosition = transform->position;
        transform->prevRotation = transform->rotation;
        auto* meshRef = world.add<ecs::MeshComponent>(entity);
        // Referenced by identity: the registry resolves the source path to the GUID its `.tumeta`
        // sidecar carries, so renaming the file later does not orphan the entity.
        if (sceneToolPtr != nullptr) {
          if (const auto* found = sceneToolPtr->assets().findByPath(scenePath)) {
            meshRef->mesh = found->guid;
          }
        }
        meshRef->visible = object.visible;
        world.add<ecs::RenderObjectComponent>(entity)->sceneIndex = static_cast<uint32_t>(i);
      }
      for (const Light& light : scene.lights) {
        const ecs::Entity entity = world.create();
        // Named by kind: six entries all called "Light" is a list you have to click through to
        // read. The Outliner already shows a bulb icon, so the name should carry what the icon
        // cannot.
        world.add<ecs::NameComponent>(entity)->name =
            light.type == LightType::Directional ? "Directional Light"
            : light.type == LightType::Spot      ? "Spot Light"
                                                 : "Point Light";
        auto* transform = world.add<ecs::TransformComponent>(entity);
        transform->position = light.position;
        transform->prevPosition = light.position;
        // The direction has to survive the trip, because the sync back rebuilds it from this
        // rotation. Without it every directional and spot light would snap to straight down on the
        // first frame — the scene would still light, just wrongly, which is the hardest kind to
        // notice. Local -Y is the aim axis, matching `Light::direction`'s default.
        transform->rotation = ecs::lightRotationFor(light.direction);
        transform->prevRotation = transform->rotation;
        auto* lightComponent = world.add<ecs::LightComponent>(entity);
        lightComponent->type = light.type;
        lightComponent->color = light.color;
        lightComponent->intensity = light.intensity;
        lightComponent->range = light.range;
        lightComponent->innerCone = light.innerCone;
        lightComponent->outerCone = light.outerCone;
        lightComponent->castShadows = light.castShadows;
      }
    };
    populateWorldFromScene();

    // Resolves what the entities reference into what the renderer draws. Built after the registry
    // scan so it has something to resolve against, and kept alive for the whole run because its
    // texture cache is the reason the same image is not uploaded once per material.
    std::unique_ptr<AssetResolver> assetResolver;
    ecs::MaterialSyncState materialSync;
    if (sceneToolPtr != nullptr) {
      assetResolver = std::make_unique<AssetResolver>(*device, sceneToolPtr->assets(), "Assets");
    }

    // I-01: what actually runs during Play. The editor does not decide this — the host does, which
    // is why PlayMode owns the state machine and the snapshot but not the simulation. This one is
    // deliberately small and visible: everything drifts upward, so Play then Stop shows the scene
    // move and snap back to exactly where it was.
    if (sceneToolPtr != nullptr) {
      sceneToolPtr->playMode().onTick = [&world](float dt) {
        for (ecs::EntityManager::Archetype& archetype : world.entities().archetypes()) {
          const int slot = archetype.slot(ecs::kCompTransform);
          if (slot < 0) continue;
          for (uint32_t chunkIndex = 0; chunkIndex < archetype.chunks.size(); ++chunkIndex) {
            auto* transforms =
                static_cast<ecs::TransformComponent*>(archetype.column(chunkIndex, slot));
            for (uint32_t i = 0; i < archetype.chunks[chunkIndex].count; ++i) {
              transforms[i].prevPosition = transforms[i].position;
              transforms[i].position.y += dt * 0.5f;
            }
          }
        }
      };
    }

    scene.camera.setPosition({0.0f, 2.0f, 0.0f});
    scene.camera.lookAt({1.0f, 2.0f, 0.0f});
    setupDefaultRain(renderer->rain());

    LuaVM::instance().init();
    LuaVM::instance().setDevice(device.get());
    LuaVM::instance().setScene(&scene);
    LuaVM::instance().setRenderer(renderer.get());
    LuaVM::instance().setCamera(&scene.camera);
    LuaVM::instance().setInput(&input);
    LuaVM::instance().registerAllBindings();
    LuaVM::instance().loadScript("Scripts/main.lua");
    LuaVM::instance().callSetup();

    device->setDeviceLostCallback([&]() {
      std::cerr << "[SponzaViewer] Rebuilding swapchain / renderer / scene after device recovery...\n";
      const Camera cam = scene.camera;
      const RendererSettings settings = renderer->settings();
      const RainParams rain = renderer->rain();

      // The ImGui context dies with the UI, taking the shell's settings handler with it, so both
      // are torn down and rebuilt as a pair.
      shell.shutdown();
      ui.shutdown();
      renderer.reset();
      swapChain.reset();

      swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
      renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
      renderer->settings() = settings;
      renderer->rain() = rain;
      reloadScene();
      scene.camera = cam;
      ui.init(window, *device);
      if (editorMode) {
        shell.init(layoutPath);
      }
    });

    window.setResizeCallback([&](uint32_t w, uint32_t h) {
      if (!swapChain || !renderer) {
        return;
      }
      swapChain->resize(w, h);
      renderer->resize(w, h);
      scene.camera.setPerspective(glm::radians(60.0f), window.aspect(), 0.1f, 300.0f);
    });

    // ── Viewport offscreen target ──
    // The scene is rendered here instead of straight to the back buffer whenever a Viewport panel
    // asks for it. It has to be a texture because the editor cannot show the world behind its
    // panels: the tool window is docked into the shell's central node, and an occupied dock node
    // paints over anything drawn before ImGui.
    std::shared_ptr<rhi::Texture> viewportTarget;
    uint32_t viewportTargetW = 0;
    uint32_t viewportTargetH = 0;

    int frame = 0;
    bool shotDone = screenshotPath.empty();
    while (!window.shouldClose() && !shell.quitRequested()) {
      window.pollEvents();
      input.beginFrame();
      ui.beginFrame();
      // The dockspace has to be submitted before the windows that dock into it.
      // Published before the UI is built, because the panel reads it while building. The
      // descriptor points at a texture this frame will fill later — and ImGui only samples it at
      // endFrame, after the render, so the order is right.
      editorContext.sceneTexture =
          viewportTarget != nullptr ? ui.sceneTextureId(*device, *viewportTarget) : 0;
      // Cleared each frame so only a panel that actually drew this frame is asking. Otherwise
      // closing the Viewport tab would leave a stale request standing forever.
      editorContext.requestedViewportW = 0;
      editorContext.requestedViewportH = 0;
      editorContext.viewportHovered = false;

      shell.beginFrame();
      if (shell.ready()) {
        const float fps = 1000.0f / std::max(1.0f, renderer->lastFrameMs());
        shell.setStatus(std::to_string(int(fps)) + " FPS  |  " + std::to_string(renderer->drawCalls()) +
                        " draws  |  " + std::to_string(window.width()) + "x" + std::to_string(window.height()));
      } else {
        ui.drawPerfHud(renderer->lastFrameMs(), renderer->drawCalls(), window.width(), window.height());
      }
      // The weather panel is a viewer control, not an editor tool; in the tools demo it would just
      // float over the thing being demonstrated.
      // The weather panel is a viewer control; with a tool open the Environment tool window covers
      // the same ground and this would just float over it.
      if (!toolsDemo && !sceneTool) {
        ui.drawWeatherAndLights(renderer->rain(), scene, renderer->settings(),
                                /*lightsOwnedByEcs=*/true);
      }
      if (closeDirtyAtFrame >= 0 && frame == closeDirtyAtFrame && dirtyDemoTool != nullptr) {
        shell.toolHost().requestClose(dirtyDemoTool);
      }
      // The tool draws whatever the context points at; the host keeps it current.
      editorContext.world = &world;
      editorContext.scene = &scene;
      editorContext.renderer = renderer.get();
      editorContext.settings = &renderer->settings();
      editorContext.camera = &scene.camera;
      editorContext.frameMs = renderer->lastFrameMs();
      editorContext.drawCalls = renderer->drawCalls();
      editorContext.viewportW = window.width();
      editorContext.viewportH = window.height();
      // Applied every frame rather than once: --select is there to hold a selection steady for an
      // unattended screenshot, and the Outliner would otherwise be free to clear it.
      if (selectObject >= 0) editorContext.selectedObject = selectObject;
      if (selectEntity >= 0) {
        // By position in creation order, not by id: ids are an ECS implementation detail and a
        // command line that has to know them is not usable.
        int seen = 0;
        for (ecs::Entity candidate = 0; candidate < 4096; ++candidate) {
          if (!world.alive(candidate)) continue;
          if (seen++ == selectEntity) {
            editorContext.selectedEntity = candidate;
            break;
          }
        }
      }
      // Advances only while playing; a no-op otherwise, so there is nothing to branch on here.
      if (sceneToolPtr != nullptr) sceneToolPtr->playMode().tick(1.0f / 60.0f);
      // After the tick, so what is drawn is the state the simulation just produced rather than the
      // previous frame's. alpha = 1: nothing here interpolates, the entity position is the truth.
      ecs::syncTransformsToScene(world, scene, 1.0f);
      // And what it looks like: the material override and the visibility flag. Cheap per frame —
      // it only rebuilds a material when the assignment actually changed.
      if (assetResolver != nullptr) {
        ecs::syncMeshComponentsToScene(world, scene, *assetResolver, materialSync);
      }
      // Lights are rebuilt wholesale from the entities. Calling this is the declaration that the
      // ECS owns them — see the note on syncLightsToScene — which is true here because
      // populateWorldFromScene created one entity per light.
      ecs::syncLightsToScene(world, scene);
      shell.drawTools(1.0f / 60.0f);
      if (uiGallery) {
        editor::ui::drawGallery(nullptr);
      }
      // Panels nobody owns yet get their placeholder — D2/D3/D4 replace these with real content.
      shell.endFrame();
      // After the panels, so toasts stack above them.
      editor::ui::drawNotifications();

      if (!ui.wantCaptureKeyboard()) {
        if (input.keyPressed(GLFW_KEY_0)) {
          if (device->supportsMeshShaders()) {
            renderer->settings().enableMeshShaders = !renderer->settings().enableMeshShaders;
          } else {
            std::cout << "[SponzaViewer] Mesh shaders unavailable on this device\n";
          }
        }
        if (input.keyPressed(GLFW_KEY_F12) && screenshotPath.empty()) {
          screenshotPath = "sponza_capture.png";
          shotDone = false;
        }
        if (input.keyPressed(GLFW_KEY_1)) {
          renderer->settings().enableShadows = !renderer->settings().enableShadows;
        }
        if (input.keyPressed(GLFW_KEY_2)) {
          renderer->settings().enableIBL = !renderer->settings().enableIBL;
        }
        if (input.keyPressed(GLFW_KEY_3)) {
          renderer->settings().enableBloom = !renderer->settings().enableBloom;
        }
        if (input.keyPressed(GLFW_KEY_4)) {
          renderer->settings().enableAO = !renderer->settings().enableAO;
        }
        if (input.keyPressed(GLFW_KEY_5)) {
          auto& t = renderer->settings().giTier;
          t = static_cast<GITier>((static_cast<uint32_t>(t) + 1) % 4);
        }
        if (input.keyPressed(GLFW_KEY_6)) {
          renderer->settings().enableVisibilityBuffer = !renderer->settings().enableVisibilityBuffer;
        }
        if (input.keyPressed(GLFW_KEY_7)) {
          renderer->settings().enableMeshlets = !renderer->settings().enableMeshlets;
        }
        if (input.keyPressed(GLFW_KEY_8)) {
          renderer->settings().enableSSR = !renderer->settings().enableSSR;
        }
        if (input.keyPressed(GLFW_KEY_9)) {
          renderer->rain().enabled = !renderer->rain().enabled;
        }
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
      const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 12.0f : 4.0f;
      // Over the viewport image the mouse belongs to the camera, not to ImGui: WantCaptureMouse is
      // true over any ImGui window, and the scene lives inside one now.
      const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) &&
                        (!ui.wantCaptureMouse() || editorContext.viewportHovered);
      if (look) {
        scene.camera.fly(1.0f / 60.0f, move * speed, dx * 0.0025f, -dy * 0.0025f);
      } else {
        scene.camera.fly(1.0f / 60.0f, move * speed, 0, 0);
      }

      if (frame % 60 == 0 && !shell.ready()) {
        const float fps = 1000.0f / std::max(1.0f, renderer->lastFrameMs());
        window.setTitle("Tucano Sponza | " + std::to_string(int(fps)) + " FPS | rain " +
                        (renderer->rain().enabled ? "on" : "off") + " | Tools panel | 9 rain | F12");
      }
      input.endFrame();

      LuaVM::instance().tick(1.0f / 60.0f);

      // Between the UI and the render: the panel has just said how big it wants to be, and the
      // renderer must not be resized mid-frame.
      const uint32_t wantW = editorContext.requestedViewportW;
      const uint32_t wantH = editorContext.requestedViewportH;
      // A request of zero means no Viewport panel drew this frame — a closed or unselected tab. The
      // existing target is kept and still rendered into, so re-selecting the tab shows a live scene
      // instead of one frozen at the moment it was hidden.
      if (wantW > 0 && wantH > 0 && (wantW != viewportTargetW || wantH != viewportTargetH)) {
        device->waitIdle();
        rhi::TextureDesc desc;
        desc.width = wantW;
        desc.height = wantH;
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        desc.debugName = "ViewportTarget";
        viewportTarget = device->createTexture(desc);
        viewportTargetW = wantW;
        viewportTargetH = wantH;
        renderer->resize(wantW, wantH);
        // Aspect from the panel, not the window: a viewport that is not the whole window would
        // otherwise render a subtly stretched world and nobody would be able to say why.
        scene.camera.setPerspective(glm::radians(60.0f),
                                    static_cast<float>(wantW) / static_cast<float>(wantH), 0.1f,
                                    300.0f);
      }

      auto* cmd = device->beginFrame();
      auto& bb = swapChain->backBuffer();
      if (viewportTarget != nullptr) {
        renderer->render(cmd, *viewportTarget, scene);
        // The panel samples it; without this the read happens while it is still a render target.
        cmd->transition(*viewportTarget, rhi::ResourceState::ShaderResource);
        // Nothing draws the back buffer any more, so it has to be cleared — otherwise the gaps
        // between panels show whatever the last frame left in it.
        cmd->transition(bb, rhi::ResourceState::RenderTarget);
        const float clearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};
        cmd->clearRenderTarget(bb, clearColor);
      } else {
        renderer->render(cmd, bb, scene);
      }
      ui.endFrame(*cmd, bb);

      ScreenshotPending shot;
      if (!shotDone && frame >= shotFrame) {
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
    std::cerr << "[trace] loop terminou\n";
    shell.shutdown(); // flushes the layout while the ImGui context is still alive
    std::cerr << "[trace] shell.shutdown ok\n";
    ui.shutdown();
    std::cerr << "[trace] ui.shutdown ok\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << "\n";
    return 1;
  }
}
