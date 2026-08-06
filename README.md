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
│   ├── Core/           Job system, JSON parser, reflection
│   ├── Platform/       Window, input, filesystem
│   ├── Audio/          miniaudio-based sound
│   ├── Lua/            Lua 5.4 scripting bindings
│   ├── Network/        UDP networking, replication, RPC
│   ├── Editor/         Native ImGui editor (dockspace, outliner, inspector, console)
│   └── Runtime/        ImGui backend, transform gizmo, screenshot
├── Samples/            HelloTriangle · PBRTest · SponzaViewer · SponzaPhysics
│                       PhysicsDemo · SkyLab · WaterLab · TestEditor
├── EngineAssets/       Engine textures (rain DDS, IBL, sky catalog)
├── Tools/              Benchmark · ECSTest · AssetTest · RHITest
├── docs/               Architecture Decision Records
├── cmake/              Build configuration, dependency declarations
└── test/               Unit tests
```

---

## Editor

The native editor runs in-process with the engine using Dear ImGui (docking branch).
No external dependencies — no .NET runtime, no C ABI bridge.

```powershell
# Launch the editor
.\build\windows-release\Samples\TestEditor\TestEditor.exe
```

| Feature | Status |
|---------|--------|
| Dockspace layout | Done |
| Outliner (scene hierarchy) | Planned |
| Inspector (properties via reflection) | Planned |
| Content browser | Planned |
| Console (log sink) | Done |
| Transform gizmo (ImGuizmo) | Done |
| Environment/Weather panels | Planned |
| Terrain sculpting tools | Planned |
| Animation controls | Planned |
| Play/Pause/Stop | Planned |

---

## Build Details

| Preset | Configuration |
|--------|---------------|
| `windows-release` | Release (MSVC, Ninja) |

Shaders compile automatically via DXC at build time.

Dependencies (fetched by CMake): `GLFW` · `glm` · `stb` · `cgltf` · `meshoptimizer` · `Dear ImGui` · `Jolt Physics` · `miniaudio` · `ZSTD` · `Lua` · `Draco` · `OpenFBX`

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

---

## License

Tucano Engine — MIT License.

**Third-party**: GLFW (zlib) · GLM (MIT) · stb (MIT/PD) · cgltf (MIT) · meshoptimizer (MIT) · Dear ImGui (MIT) · Jolt Physics (MIT) · miniaudio (MIT) · ZSTD (BSD) · Lua (MIT) · Draco (Apache 2.0) · OpenFBX (MIT)

**Assets**: Khronos Sponza glTF sample — [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/)

---

<p align="center">
  <sub>Built from the ground up — no legacy engine code.</sub>
</p>
