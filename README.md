# Tucano Engine

> Engine brasileira para jogos **Open World**, derivada (fork) da **B3DFramework**, com foco em performance, modularidade e qualidade **AAA**.

![Tucano Engine](Documentation/logo.svg)

---

## Visão Geral

A **Tucano Engine** é uma engine de jogos moderna construída sobre a arquitetura multi-threaded da B3DFramework, com backend de renderização **Vulkan** e suporte a **D3D12**. O projeto visa entregar uma plataforma acessível para desenvolvedores independentes e equipes maiores, sem abrir mão de recursos de nível AAA.

### Diferenciais

- **Terrain System AAA** — sistema de terreno inspirado na Dagor Engine (Gaijin Games), com Geo-Clipmap LOD, heightmap comprimido com hierarquia min/max, e raycasting adaptativo
- **Sky Procedural** — céu analítico Preetham gerado em GPU via compute shaders
- **FSR 3.1** — FidelityFX Super Resolution integrado (opt-in)
- **NRD** — NVIDIA Real-time Denoiser para ray tracing (opt-in)
- **Integração nativa com IA** — planejada para acelerar criação de conteúdo (shaders, materiais, cenários, scripts)
- **Visual Scripting** — planejado para reduzir programação manual em tarefas comuns

---

## Sistema de Terrain AAA

Implementação de terreno inspirada na **Dagor Engine 6.5** (licença MIT, Gaijin Games KFT), adaptada à arquitetura da Tucano Engine.

### Arquitetura

```
Source/Engine/Core/Terrain/
├── B3DHeightmapData.h/.cpp          ← Heightmap + hierarquia min/max + raycasting O(log n)
├── B3DTerrainLodGrid.h/.cpp         ← Geo-Clipmap LOD grid com morph e frustum culling
├── B3DTerrainVirtualTexture.h/.cpp   ← Virtual Texture (Clipmap) — atlas + indirection + LRU
├── B3DTerrainRenderer.h/.cpp         ← Renderer GPU com instancing + materiais dedicados
├── B3DTerrainMaterials.h             ← TerrainMaterial + TerrainDepthMaterial (RendererMaterial)
├── B3DTerrainUniformBuffers.h        ← Uniform buffers do terrain (frame/patch/light)
├── B3DTerrainSystem.h/.cpp           ← Module singleton — ponto de entrada
├── B3DTerrainLandClass.h/.cpp        ← Land classes + splat mask + biomes
├── B3DTerrainErosion.h/.cpp          ← Erosão procedural GPU (hydraulic + thermal)

Data/Shaders/Terrain/
├── TerrainHeightmap.bsl             ← Geo-Clipmap VS + Virtual Texture PS
├── TerrainDepth.bsl                 ← Depth-only pass (shadow maps)
└── TerrainHydraulicErosion.bsl      ← Compute shader de erosão
```

### Técnicas Implementadas

| Técnica | Fonte de Inspiração | Status |
|---|---|---|
| **Geo-Clipmap (LodGrid)** | Dagor `lodGrid.h` + `cull_lod_grid()` | ✅ Fase 1 |
| **Morph geométrico** | Dagor heightmap shaders | ✅ Shader pronto |
| **Hierarquia min/max** | Dagor `CompressedHeightmap` | ✅ Fase 1 |
| **Raycasting adaptativo** | Dagor `dag_hmlTraceRay.h` | ✅ Fase 1 |
| **Virtual Texture (Clipmap)** | Dagor `Clipmap` / `ClipmapImpl` | 🔄 Stub (Fase 3) |
| **Erosão procedural GPU** | Original (Mei & Decaudin 2007) | 🔄 Stub (Fase 6) |

### API de Uso

```cpp
// Startup
TerrainSystem::StartUp();
TerrainSystem::Settings settings;
settings.mHeightmapPath = "terrain.raw";  // ou vazio para procedural
settings.mCellSizeMeters = 1.0f;
settings.mHeightScale = 100.0f;
TerrainSystem::Instance().Init(settings);

// Per frame
TerrainSystem::Instance().Update(camera);
TerrainSystem::Instance().Render(camera);

// Gameplay queries
float height;
TerrainSystem::Instance().GetHeightAtPoint(Vector2(x, z), height);

float hitDist = TerrainSystem::Instance().TraceRay(ray, 1000.0f);

// Shutdown
TerrainSystem::Instance().Shutdown();
TerrainSystem::ShutDown();
```

---

## Compilação

### Pré-requisitos

- **CMake** 3.31+
- **Visual Studio 2017+** (Windows) ou GCC/Clang (Linux/macOS)
- **Vulkan SDK** 1.4+
- Dependências: snappy, freetype, PhysX, FBX SDK, FreeImage, OpenAL

### Build

```bash
mkdir Build && cd Build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config RelWithDebInfo
```

### Executáveis

Os exemplos ficam em `Build/bin/x64/RelWithDebInfo/`:

| Executável | Descrição |
|---|---|
| **Terrain.exe** | Terreno procedural com heightmap, Sky HDRI, iluminação direcional |
| **Lighting.exe** | Demo de iluminação não-direcional + Sky procedural |
| **Physics.exe** | Demo de física (character controller, rigidbodies) |

---

## Controles do Terrain.exe

| Tecla | Ação |
|---|---|
| **WASD** | Mover câmera (com aceleração) |
| **Botão direito do mouse** | Olhar / rotacionar câmera |
| **Shift** | Movimento rápido (2x) |
| **ESC** | Sair |

---

## Estrutura do Projeto

```
B3DFramework-master/
├── Source/
│   ├── Engine/Core/Terrain/          ← Sistema de Terrain AAA
│   ├── Plugins/
│   │   ├── bsfRenderBeast/            ← Renderer (Vulkan/D3D12) + Sky + FSR3 + NRD
│   │   ├── bsfVulkanGpuBackend/       ← Backend Vulkan
│   │   └── bsfPhysX/                  ← Física (PhysX)
│   └── Tests/                         ← Test suites (incl. Terrain)
├── Data/Shaders/                      ← Shaders BSL
│   ├── Terrain/                       ← Shaders do terrain
│   └── SkyProcedural.bsl              ← Shader do céu procedural
├── Examples/Source/Terrain/           ← Exemplo Terrain.exe
├── Dependencies/                      ← FidelityFX, NRD, etc.
└── Documentation/                     ← Manuais e docs
```

---

## Roadmap

| Fase | Escopo | Status |
|---|---|---|
| **1 — Fundação** | HeightmapData, LodGrid, culling, queries, Terrain.exe | ✅ Concluída |
| **2 — LOD + Morph** | Morph geométrico no VS, edge patches, HW tessellation | 🔄 Planejada |
| **3 — Texturização** | Virtual Texture (Clipmap), splat map, land classes | 🔄 Planejada |
| **4 — Física + Culling** | TraceRay via CompressedHeightmap, HZB occlusion | 🔄 Planejada |
| **5 — Streaming** | GPU feedback, async tile updates, LOD metrics reais | 🔄 Planejada |
| **6 — Melhorias AAA** | Erosão GPU, deformação runtime, BC7, DirectStorage | 🔄 Planejada |

---

## Decisões Arquiteturais

### Por que basear-se na Dagor Engine?

A Dagor Engine (Gaijin Games, MIT license) é uma engine madura com ~20 anos de desenvolvimento, especialmente reconhecida pelo seu sistema de terrain. Suas técnicas de Geo-Clipmap, Virtual Texture e CompressedHeightmap são referência na indústria. Adaptemos a lógica matemática e algoritmos, reescrevendo toda interação com a GPU usando os tipos do B3DFramework.

### Adaptações realizadas

- `Vector2/3` usa `X/Y/Z` maiúsculos (convenção B3D)
- `TArray` usa `Add()/Resize()/Clear()` (não `push_back/resize`)
- `AABox` com `Minimum/Maximum/GetSize()` (não `Bounds3`)
- `GpuBuffer::Map(GpuMapOption)` RAII scope (não `Lock/Unlock`)
- `GpuParameterUniformBuffer::Set(GpuBufferSuballocation)` via `AllocateTransient().Map()`
- `B3D_LOG(Verbosity, LogCategory, fmt, args)` com categoria static

---

## Referências

- [Dagor Engine](https://github.com/GaijinEntertainment/DagorEngine) — MIT License, Gaijin Games KFT
- Paper: "Terrain Rendering in Frostbite" (GPU Pro 3)
- Paper: "Adaptive GPU Tessellation with Compute Shaders" (SIGGRAPH 2019)
- Mei & Decaudin (2007) — "Fast Hydraulic Erosion Simulation and Visualization on GPU"

---

## Licença

Este projeto é derivado da B3DFramework (licença MIT). Veja [LICENSE.md](LICENSE.md) para os termos completos.

O código adaptado da Dagor Engine respeita sua licença MIT (Gaijin Games KFT).