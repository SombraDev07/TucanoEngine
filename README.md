# Tucano Engine

**Greenfield real-time rendering engine** — C++20 · Direct3D 12 · CMake · glTF 2.0

Deferred PBR, meshlet GPU-driven pipeline, volumetric clouds, rain system, Bruneton atmosphere, virtual shadow maps, DDGI, World SDF, Jolt physics, archetype ECS, and a production-quality RHI with DXR 1.1.

---

## Highlights

| System | Status | Description |
|--------|--------|-------------|
| **RHI** | DX12 | Bindless, barrier coalescing, async compute, PSO cache, TDR recovery, DXR 1.1 |
| **Deferred PBR** | Production | GGX + Burley + clearcoat + sheen, IBL split-sum, ACES tonemap |
| **Shadows** | Toroidal + VSM | 4-cascade toroidal CSM, octahedral point atlas, virtual shadow maps, PCSS, ESM |
| **GI** | DDGI + Voxel + SDF | DDGI probes, voxel GI, World SDF with GPU Jump Flooding, SSGI, reflection probes |
| **RenderGraph** | Full frame | 12-stage compilation, resource aliasing, async compute hints, barrier auto-insertion |
| **GPU-Driven** | Meshlet pipeline | meshoptimizer clusters, compute culling, Hi-Z occlusion, indirect draw, mesh shaders SM 6.5 |
| **Volumetric Clouds** | Raymarch | Half-res 40-step FBM raymarch, temporal AA, weather map, god rays |
| **Rain** | Multi-pass | GBuffer wetness, puddles, ripples, streaks, mist, lens drops, volumetric cones |
| **Water + Fog** | SSR + volumetrics | Water with SSR reflections, height fog, volumetric fog |
| **Atmosphere** | Bruneton | Physically-based transmittance + scattering + irradiance LUTs, time-of-day driven |
| **ECS** | Archetype SoA | 16KB chunks, bloom-filter queries, MT queries, events, JSON templates |
| **Editor** | Native ImGui | Tool framework, dockspaces, undo, dialogs, borderless chrome |
| **Reflection** | libclang codegen | Annotations on the struct generate the registry and the property grid |
| **Physics** | Jolt 5.6 | Rigid bodies, character controller, raycast, mesh collision |
| **Terrain** | Clipmap + VT | Continuous-LOD clipmap geometry, virtual texturing, material atlas, erosion |
| **Vegetation** | GPU-instanced | Frustum + Hi-Z culling, LOD/impostor, wind, seasons, procedural scatter |
| **World Streaming** | Cell-based | 3-stage pipeline, 16-shard grid, movement prediction, HLOD, cell persistence |
| **Asset Pipeline** | glTF + Cook | glTF 2.0 via cgltf, FBX via OpenFBX, Draco compression, DDS, pack system |

---

## Quick Start

### Prerequisites
- **Windows 10/11 x64** · **MSVC 2022** · **CMake >= 3.28** · **GPU with DX12**

```powershell
git clone https://github.com/SombraDev07/TucanoEngine.git
cd TucanoEngine

cmake --preset=windows-release
cmake --build --preset=windows-release
```

Dependencies are fetched automatically via CMake FetchContent — no package manager needed.

### Run a Sample

```powershell
# Deferred PBR test grid
.\build\windows-release\Samples\PBRTest\PBRTest.exe

# Sponza + rain + volumetric clouds + full pipeline
.\build\windows-release\Samples\SponzaViewer\SponzaViewer.exe

# Sponza with Jolt physics
.\build\windows-release\Samples\SponzaPhysics\SponzaPhysics.exe

# Physics sandbox — cubes, character controller, raycast
.\build\windows-release\Samples\PhysicsDemo\PhysicsDemo.exe

# Sky & atmosphere lab
.\build\windows-release\Samples\SkyLab\SkyLab.exe

# Water rendering lab
.\build\windows-release\Samples\WaterLab\WaterLab.exe
```

### Sponza Assets

Sponza is not stored in this repo. Download manually:

```powershell
# Place the Khronos glTF Sponza under:
#   Assets/Sponza/Sponza.gltf
#   Assets/Sponza/textures/
```

---

## Controls (SponzaViewer)

| Input | Action |
|-------|--------|
| `RMB` + mouse | Look around |
| `WASD` / arrows | Move |
| `F12` | Screenshot (PNG) |
| `Toggle keys` | Viewport overlay — shadows, IBL, GI, rain, clouds, water, fog |
| `F3` | Debug UI (ImGui tools panel) |

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Editor (Native ImGui)                           │
│  Dockspace · Outliner · Inspector · Console      │
├─────────────────────────────────────────────────┤
│  Samples & Tools                                 │
│  SponzaViewer · PhysicsDemo · SkyLab · WaterLab  │
├─────────────────────────────────────────────────┤
│  Renderer                                        │
│  Shadows → GBuffer → AO → Lighting → Clouds     │
│  → SSGI → DDGI → SSR → Rain → Bloom → Tonemap   │
├─────────────────────────────────────────────────┤
│  RenderGraph  ·  GI (DDGI/Voxel/SDF)             │
│  Weather (Rain/Clouds/Water/Fog)                 │
│  Atmosphere (Bruneton)                           │
├─────────────────────────────────────────────────┤
│  ECS (Archetype SoA)  ·  Physics (Jolt)          │
│  Terrain  ·  Vegetation  ·  Animation            │
│  World Streaming  ·  Asset Pipeline              │
├─────────────────────────────────────────────────┤
│  RHI (Direct3D 12)                               │
│  Bindless · Barriers · Async Compute · RT · PSO  │
└─────────────────────────────────────────────────┘
```

---

## Repository Structure

```
TucanoEngine/
├── Shaders/           43 HLSL shaders (DXC-compiled): GBuffer, shadows, lighting,
│                       rain, clouds, water, fog, GI, vegetation, terrain, RT
├── src/
│   ├── RHI/DX12/      Device, heaps, barriers, bindless, PSO cache, RT, crash recovery
│   ├── RHI/Null/       No-op backend for headless testing
│   ├── Renderer/       Deferred, shadows, GI, weather, atmosphere, render graph
│   ├── ECS/            Entity manager, queries, events, templates (JSON)
│   ├── Physics/        Jolt wrapper — rigid bodies, character, raycast
│   ├── World/          Streaming grid, scheduler, cell persistence, HLOD
│   ├── Terrain/        Clipmap terrain, virtual texturing, material atlas
│   ├── Vegetation/     GPU instances, LOD, wind, seasons, scatter
│   ├── Animation/      Skeleton, clips, curves, blends, controller
│   ├── AssetPipeline/  glTF, FBX, images, DDS, cooker, packs, hot-reload
│   ├── Core/           Job system, JSON parser, memory
│   ├── Core/TypeSystem/  TypeID, TypeInfo, TypeRegistry, reflection macros
│   ├── Generated/      Reflection emitted by Tools/Reflector (committed)
│   ├── Platform/       Window, input, filesystem
│   ├── Audio/          miniaudio-based sound
│   ├── Lua/            Lua 5.4 scripting bindings
│   ├── Network/        UDP networking, replication, RPC
│   ├── Editor/         Tool framework, property grid, undo, dialogs, widgets
│   └── Runtime/        ImGui backend, transform gizmo, screenshot
├── Samples/            HelloTriangle · PBRTest · SponzaViewer · SponzaPhysics
│                       PhysicsDemo · SkyLab · WaterLab · TerrainLab · WorldLab
│                       LuaLab · TestEditor
├── EngineAssets/       Engine textures (rain DDS, IBL, sky catalog) and editor fonts
├── Tools/              Reflector (codegen) · UITest · RHITest · ECSTest · AssetTest
│                       AnimationTest · InputTest · VegTest · WorldTest · WorldGpuTest
│                       Benchmark
├── docs/               Architecture Decision Records
├── cmake/              Build configuration, dependency declarations
└── test/               Unit tests
```

### Gates

The engine is verified by headless executables that exit non-zero on failure, so a regression is a
build failure rather than something to notice by eye.

```powershell
.\build\windows-release\Tools\RHITest\TucanoRHITest.exe    # RHI, barriers, state tracking
.\build\windows-release\Tools\UITest\TucanoUITest.exe      # editor: 161 assertions
.\build\windows-release\Tools\ECSTest\TucanoECSTest.exe    # archetypes, queries, events
```

`TucanoUITest` exists because ImGui draw data was once verified correct while never reaching the
screen. It probes actual rasterised pixels per draw-list kind, then asserts the editor's contracts
on top of that: undo depth, dialog state machines, reflection layout, picker scans, editing rules.

---

## Editor

The native editor runs in-process with the engine using Dear ImGui (docking branch).
No external dependencies — no .NET runtime, no C ABI bridge.

```powershell
# Editor shell over the live scene, with the real panels
.\build\windows-release\Samples\SponzaViewer\SponzaViewer.exe --scene Assets/Sponza/Sponza.gltf --scene-tool

# Borderless window chrome (custom title bar, Windows snap preserved)
.\build\windows-release\Samples\SponzaViewer\SponzaViewer.exe --scene Assets/Sponza/Sponza.gltf --borderless --scene-tool

# Widget gallery — every control the editor is built from
.\build\windows-release\Samples\SponzaViewer\SponzaViewer.exe --editor --ui-gallery
```

### Tool framework

Panels are not top-level windows. Each **tool** owns a dockspace, its own layout, its own undo
stack and its own dirty state, so opening a different tool swaps the whole workspace instead of
piling windows on top of each other.

| Feature | Status |
|---------|--------|
| Dockspace, per-tool layout persistence | Done |
| Outliner, Inspector, Content Browser, Console | Done |
| Environment / Water / Fog panels | Done |
| Stats, Tools, Animation panels | Done |
| Undo/redo with gesture coalescing (`Ctrl+Z` / `Ctrl+Y`) | Done |
| Modal dialogs, unsaved-changes flow, native file dialogs | Done |
| Borderless window chrome | Done |
| Transform gizmo (ImGuizmo) | Done |
| Terrain sculpting tools | Planned |
| Play/Pause/Stop | Planned |

### Property grid

The Inspector, Water and Fog panels have **no UI code written per field**. They are generated from
reflection: a struct that declares a property gets a complete editor — its range, tooltip,
category, undo integration and conditional visibility — the moment it is registered.

```cpp
struct TUCANO_TYPE() Material {
  TUCANO_FIELD(Color, .label = "Base color", .category = "Surface")
  glm::vec4 baseColorFactor{1, 1, 1, 1};

  TUCANO_FIELD(.label = "Metallic", .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
  float metallicFactor = 1.0f;
};
```

`Tools/Reflector/reflector.py` reads those annotations with libclang and emits
`src/Generated/Reflection.g.{h,cpp}`. The generated file is committed, so a machine without the
`libclang` package still builds; `cmake --build <dir> --target TucanoReflectCheck` fails when it no
longer matches the headers, and `--target TucanoReflect` regenerates it.

The generator emits `offsetof`/`sizeof` rather than baking numeric offsets — a generator that
hard-codes a layout is wrong the moment anything changes packing, and wrong silently.

---

## Build Details

| Preset | Configuration |
|--------|---------------|
| `windows-release` | Release (MSVC, Ninja) |

Shaders compile automatically via DXC at build time.

Dependencies (fetched by CMake): `GLFW` · `glm` · `stb` · `cgltf` · `meshoptimizer` · `Dear ImGui` · `Jolt Physics` · `miniaudio` · `ZSTD` · `Lua` · `Draco` · `OpenFBX`

### Reflection codegen

Optional, and not part of the default build — a build must never rewrite a version-controlled file
behind the author's back.

```powershell
# Regenerate src/Generated/Reflection.g.{h,cpp} after annotating a type
cmake --build build\windows-release --target TucanoReflect

# Fail if the generated file no longer matches the annotations
cmake --build build\windows-release --target TucanoReflectCheck
```

Requires `pip install libclang` for the interpreter CMake resolved (check `_Python3_EXECUTABLE` in
`CMakeCache.txt` — it is not always the one first on `PATH`). Without it, the committed output is
used and the build still works; the *check* target fails loudly rather than reporting a pass it
could not verify.

---

## Key Technical Decisions

| Decision | Rationale |
|----------|-----------|
| **Deferred over Forward** | Enables rich GI (DDGI, SSGI, SDF) and multi-pass weather without material complexity |
| **RenderGraph over manual passes** | Automatic transient resource aliasing reduces VRAM; barrier generation eliminates manual sync bugs |
| **Archetype SoA ECS** | Cache-friendly component iteration; bloom-filter queries avoid archetype scanning |
| **GPU-driven meshlet pipeline** | Single culling path for meshlet/instance/VisBuffer; zero CPU culling overhead on the critical path |
| **Bindless resource model** | All lighting, GI, and post-process shaders share a unified descriptor heap; no per-pass binding |
| **Reverse-Z depth buffer** | Maximizes precision for large outdoor scenes; standard for modern renderers |
| **No light baking** | Entire pipeline is real-time; no offline precomputation required |
| **Reflection by codegen, not by hand** | A hand-written reflection table drifts from the struct silently — seven live material parameters were unreachable from the editor for exactly that reason. The annotation now sits on the field itself |
| **Generated code committed, build only checks it** | Keeps the repo buildable without libclang, keeps the generated file diffable in review, and keeps codegen out of the critical path |
| **Editor panels generated from reflection** | Adding a setting to a struct makes it appear with its range, tooltip and undo — no per-field UI to keep in sync |

---

## License

Tucano Engine — MIT License. Full attributions in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

**Derived code**: the editor tooling — tool framework, type system, property grid, undo stack and
reflection generator — was ported from [Esoterica](https://github.com/BobbyAnguelov/Esoterica)
(MIT). It was reimplemented against Tucano's own types rather than copied; the files that derive
from it carry a `// Derived from Esoterica (MIT) — <original path>` header.

**Libraries**: GLFW (zlib) · GLM (MIT) · stb (MIT/PD) · cgltf (MIT) · meshoptimizer (MIT) · Dear ImGui (MIT) · Jolt Physics (MIT) · miniaudio (MIT) · ZSTD (BSD) · Lua (MIT) · Draco (Apache 2.0) · OpenFBX (MIT)

**Fonts**: Material Design Icons (Apache 2.0, Pictogrammers) · Roboto / Roboto Mono (Apache 2.0, Google)

**Assets**: Khronos Sponza glTF sample — [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/) — not stored in this repository.

---

<p align="center">
  <sub>Renderer, ECS, physics integration and asset pipeline written from scratch.<br/>
  Editor tooling ported from Esoterica (MIT) — see above.</sub>
</p>
