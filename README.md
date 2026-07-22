# <picture><source media="(prefers-color-scheme: dark)" srcset="https://img.shields.io/badge/Tucano-Engine-FF6600?style=for-the-badge"><img alt="Tucano Engine" src="https://img.shields.io/badge/Tucano-Engine-CC5500?style=for-the-badge"></picture>

**Greenfield AAA rendering engine** — C++20 · Direct3D 12 · Vulkan (WIP) · CMake · glTF 2.0

Deferred PBR, meshlet GPU-driven pipeline, volumetric clouds, CryEngine-parity rain, Bruneton atmosphere, Jolt physics, archetype ECS, and a production-quality RHI.

---

## 🌟 Highlights

| System | Status | Description |
|--------|--------|-------------|
| **RHI** | 🟢 DX12 complete | Bindless, barrier coalescing, async compute, PSO cache, TDR recovery, DXR 1.1 |
| **Deferred PBR** | 🟢 Production | GGX + Burley, clearcoat, IBL split-sum, ACES tonemap, auto-exposure |
| **Shadows** | 🟢 Toroidal + VSM | 4-cascade toroidal CSM, octahedral point atlas, virtual shadow maps (VSM) |
| **GI** | 🟡 DDGI + Voxel | DDGI probes, voxel GI, World SDF (Jump Flooding), reflection probes, SSGI |
| **Volumetric Clouds** | 🟢 Nubis/Hillaire | Half-res raymarch, temporal AA, weather map, god rays, disk-cached noise |
| **Rain** | 🟢 CryEngine-parity | GBuffer wetness, puddles, ripples, streaks, mist, lens drops, volumetric cones |
| **Atmosphere** | 🟢 Bruneton | Transmittance + scattering + irradiance LUTs, time-of-day driven |
| **Mesh Pipeline** | 🟢 Meshlet GPU | meshoptimizer clusters, compute-shader culling, indirect draw, mesh shaders SM 6.5 |
| **ECS** | 🟢 Archetype SoA | Component pools, bloom-filter queries, MT queries, events, JSON templates |
| **Physics** | 🟡 Jolt 5.6 | Rigid bodies, character controller, raycast, mesh collision |
| **Asset Pipeline** | 🟢 glTF + Cook | glTF 2.0 via cgltf, DDS BC4/5, pack system, hot-reload |

> 🟢 = Usable · 🟡 = Experimental/evolving · 🔴 = Planned

---

## 🚀 Quick Start

### Prerequisites
- **Windows 10/11 x64** · **MSVC 2022** · **CMake ≥ 3.28** · **GPU with DX12**
- Linux (Vulkan) — _work in progress_

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

# Sponza with Jolt physics (spheres bouncing through the scene)
.\build\windows-release\Samples\SponzaPhysics\SponzaPhysics.exe

# Physics sandbox — cubes, character controller, raycast grab
.\build\windows-release\Samples\PhysicsDemo\PhysicsDemo.exe

# Sky & atmosphere lab
.\build\windows-release\Samples\SkyLab\SkyLab.exe
```

### Sponza Assets

Sponza is not stored in this repo (size). Download manually:

```powershell
# Place the Khronos glTF Sponza under:
#   Assets/Sponza/Sponza.gltf
#   Assets/Sponza/textures/
```

---

## 🎮 Controls (SponzaViewer)

| Input | Action |
|-------|--------|
| `RMB` + mouse | Look around |
| `WASD` / arrows | Move |
| `F12` | Screenshot (PNG) |
| `Toggle keys` | Viewport overlay — toggles for shadows, IBL, GI, rain, clouds |
| `F3` | Debug UI (ImGui tools panel) |

---

## 🏗 Architecture

```
┌─────────────────────────────────────────────────┐
│  Samples & Tools                                 │
│  SponzaViewer · PhysicsDemo · SkyLab · Benchmark │
├─────────────────────────────────────────────────┤
│  Renderer                                        │
│  Shadows → GBuffer → AO → Lighting → Clouds     │
│  → SSGI → DDGI → SSR → Rain → Bloom → Tonemap   │
├─────────────────────────────────────────────────┤
│  RenderGraph  ·  GI (DDGI/Voxel/SDF)             │
│  Weather (Rain/Clouds)  ·  Atmosphere (Bruneton) │
├─────────────────────────────────────────────────┤
│  ECS (Archetype SoA)  ·  Physics (Jolt)          │
│  Asset Pipeline  ·  Platform (GLFW)              │
├─────────────────────────────────────────────────┤
│  RHI (DX12 + Vulkan WIP)                         │
│  Bindless · Barriers · Async Compute · RT · PSO  │
└─────────────────────────────────────────────────┘
```

---

## 📦 Repository Structure

```
TucanoEngine/
├── Shaders/            HLSL (GBuffer, shadows, lighting, rain, clouds, GI, …)
├── src/
│   ├── RHI/DX12/       Device, heaps, barriers, bindless, PSO cache, RT, crash recovery
│   ├── Renderer/       Deferred, shadows, GI, weather, atmosphere, render graph
│   ├── ECS/            Entity manager, queries, events, templates (JSON)
│   ├── Physics/        Jolt wrapper — rigid bodies, character, raycast
│   ├── AssetPipeline/  glTF, images, DDS, cooker, packs, resource factory
│   ├── Core/           Job system, JSON parser
│   ├── Platform/       Window, input, filesystem
│   └── Runtime/        ImGui debug UI, screenshot
├── Samples/            HelloTriangle · PBRTest · SponzaViewer · SponzaPhysics · PhysicsDemo · SkyLab · TestEditor
├── EngineAssets/       Engine textures (rain DDS — BC4/BC5)
├── Tools/              Benchmark · ECSTest · AssetCooker
├── docs/               Architecture docs, ADRs, audit reports
└── .idea/              Planning documents, roadmaps, port guides
```

---

## 🔧 Build Details

| Preset | Configuration |
|--------|---------------|
| `windows-release` | Release (MSVC, Ninja) |

Shaders compile automatically via DXC at build time → `build/*/shaders/*.cso`.

Dependencies (all fetched by CMake): `GLFW` · `glm` · `stb` · `cgltf` · `meshoptimizer` · `Dear ImGui` · `Jolt Physics`

---

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [ROADMAP.md](ROADMAP.md) | Development phases & priorities |
| `.idea/RoadmapParidade.md` | Gap analysis & parity roadmap vs Dagor |
| `.idea/AuditoriaParidade.md` | Side-by-side system audit (Tucano vs Dagor) |
| `.idea/GuiaPort.md` | Port guide — algorithms & implementation specs |
| `.idea/RoadmapEditor.md` | Editor architecture (Avalonia .NET) |
| `docs/adr/` | Architecture Decision Records |

---

## 🤝 Contributing

1. Keep changes focused — one system per PR
2. Prefer English for code/comments
3. Do **not** commit third-party engine checkouts
4. Validate with `SponzaViewer` smoke run when touching rendering

---

## 📄 License

Tucano Engine — specify your license (e.g. MIT).

**Third-party**: GLFW (zlib) · GLM (MIT) · stb (MIT/PD) · cgltf (MIT) · meshoptimizer (MIT) · Dear ImGui (MIT) · Jolt Physics (MIT)

**Assets**: Khronos Sponza glTF sample — [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/)

---

<p align="center">
  <sub>Built from the ground up — no legacy engine code.</sub>
</p>
