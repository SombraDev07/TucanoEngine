# Tucano Engine

**Tucano** is a greenfield real-time rendering engine written in **C++20** for **Windows / Direct3D 12**.  
It targets a modern deferred AAA-style path (PBR, cascaded shadows, GI experiments, weather) with a clean RHI layer and sample viewers.

> Architectural inspiration only from production engines. **No third-party engine trees are part of this repository.**

---

## Features

### Rendering
- **Deferred PBR** — multi-RT GBuffer, directional / point / spot lights, clearcoat path  
- **IBL** — BRDF LUT, irradiance + prefiltered specular (procedural environment)  
- **Post** — bloom pyramid, ACES tonemap, screen-space AO, contact shadows  
- **Shadows** — 4-cascade CSM atlas, toroidal updates, octahedral point atlas (fill), ESM soft option  
- **GI / reflections (experimental)** — SSGI, lite DDGI atlas, VoxelGI occupancy, SkyVisibility, SSR  
- **GPU scene experiments** — meshlets (meshoptimizer), visibility buffer toggle  
- **Weather** — Cry-inspired rain: GBuffer wetness / puddles / ripples, textured + procedural streaks, mist, lens drops, ImGui tools  

### RHI (DX12)
- Graphics / compute / copy queues  
- Root signature 1.1 + bindless SRV region (GBuffer path)  
- Barrier batching, split-transition tracker, disk PSO cache, DRED breadcrumbs  

### Assets & tooling
- glTF via **cgltf**, image load via **stb**, **DDS** BC4/BC5 for weather textures  
- Asset cooker / `.tcpkg` packs / mtime watcher (early)  
- Samples: HelloTriangle, PBRTest, **SponzaViewer**, Benchmark  
- Debug: ImGui panel, FPS HUD, F12 screenshot  

See **[ROADMAP.md](ROADMAP.md)** for progress vs a production-scale reference and next priorities.

---

## Requirements

| Item | Notes |
|------|--------|
| OS | Windows 10/11 x64 |
| Compiler | MSVC (Visual Studio 2022 recommended) |
| GPU | Direct3D 12 capable |
| CMake | ≥ 3.28 |
| DXC | Windows SDK (auto-detected by CMake) |
| Git | For cloning |

Dependencies are fetched by CMake **FetchContent**: GLFW, GLM, stb, cgltf, meshoptimizer, Dear ImGui.

---

## Quick start

```powershell
git clone https://github.com/SombraDev07/TucanoEngine.git
cd TucanoEngine

cmake --preset=windows-release
cmake --build --preset=windows-release
```

### Sponza assets

Sponza is **not** stored in this repo (size). Place the Khronos glTF Sponza under:

```text
Assets/Sponza/Sponza.gltf
```

Example (manual download from [glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)):

```powershell
# After extracting the Sponza glTF folder:
# Assets/Sponza/Sponza.gltf + textures alongside
```

### Run

```powershell
# Basic triangle
.\build\windows-release\Samples\HelloTriangle\HelloTriangle.exe

# Deferred PBR grid
.\build\windows-release\Samples\PBRTest\PBRTest.exe

# Sponza + rain / tools
cd .\build\windows-release\Samples\SponzaViewer
.\SponzaViewer.exe --scene Assets/Sponza/Sponza.gltf
```

Optional flags:

```text
--screenshot path.png --frames N
```

---

## SponzaViewer controls

| Input | Action |
|-------|--------|
| **RMB** + move | Look |
| **WASD** / arrows | Move |
| **9** | Toggle rain |
| **F12** | Screenshot |
| ImGui **Tucano Tools** | Rain, renderer toggles, lights |

---

## Repository layout

```text
TucanoEngine/
├── CMakeLists.txt          # Root build + DXC shader rules
├── CMakePresets.json
├── Shaders/                # HLSL (GBuffer, lighting, rain, post, …)
├── src/
│   ├── RHI/DX12/           # Device, heaps, barriers, bindless, PSO cache
│   ├── Renderer/           # Deferred frame, shadows, GI, weather, RG
│   ├── AssetPipeline/      # glTF, images, DDS, cooker, packs
│   ├── Runtime/            # ImGui debug UI, screenshot
│   └── Platform/           # Window, input, filesystem
├── Samples/
│   ├── HelloTriangle/
│   ├── PBRTest/
│   ├── SponzaViewer/
│   └── …
├── EngineAssets/           # Engine textures (e.g. rain DDS)
├── Assets/                 # Local content (Sponza — gitignored)
├── docs/                   # Notes (Sponza gold, phase notes)
└── ROADMAP.md
```

---

## Build presets

| Preset | Config |
|--------|--------|
| `windows-release` | RelWithDebInfo / Release-style |
| `windows-debug` | Debug (if present in presets) |

```powershell
cmake --preset=windows-release
cmake --build --preset=windows-release --target SponzaViewer
```

Shaders compile automatically to `build/.../shaders/*.cso` (`TUCANO_SHADER_DIR`).

---

## Architecture (short)

```text
Shadows → GBuffer → Rain wet (GBuffer) → AO → Deferred lighting
       → GI/SSR/compose (optional) → Contact shadows
       → Rain post (puddle sheen / mist / streaks / drops)
       → Bloom → Tonemap → UI
```

- **RHI** abstracts DX12 resources; samples talk to `Renderer` + `Scene`.  
- **RenderGraph** currently compiles/aliases for stats; the live frame is still driven imperatively in `Renderer::render` (see roadmap).  
- **Weather** mutates GBuffer before lighting, then adds screen-space precipitation.

---

## Roadmap snapshot

| Area | Rough maturity |
|------|----------------|
| Deferred PBR + post | Usable on Sponza |
| Shadows | CSM + experiments; octa sampling incomplete |
| Rain | Strong demo path; climate/skies not started |
| RenderGraph / GI | Early — not production default |
| World streaming / ECS / physics | Not started |

Full checklist: **[ROADMAP.md](ROADMAP.md)**.

---

## Contributing

1. Keep changes focused (one system per PR when possible).  
2. Prefer English for code/comments; docs may be PT or EN.  
3. Do **not** commit third-party engine checkouts (`DagorEngine/`, `*CRYENGINE*`).  
4. Validate with `SponzaViewer` smoke run when touching rendering.

---

## License

Specify your project license here (e.g. MIT).  
Third-party: GLFW, GLM, stb, cgltf, meshoptimizer, ImGui — see their respective licenses.  
glTF Sample Assets (Sponza) are under their own Khronos licenses when you download them.

---

## Acknowledgments

- Khronos **Sponza** glTF sample for the primary visual gate  
- Community references for deferred / weather techniques (reimplemented, not vendored)

---

**Tucano** — building a serious DX12 renderer from the ground up.
