# Tucano Terrain Machine — Roadmap

> **Referência**: Dagor Engine 6.5 terrain system (reverse-engineered em `DagorTerrainSystem.md`)
> **Objetivo**: Sistema de terreno nota 9-10/10 para Tucano Engine
> **Status atual**: 0% — começando TM-0

---

## Scorecard Target

```
                       Dagor    Tucano TM
Heightmap LOD          ███████ 7   ██████████ 10
Land mesh LOD          ████░░ 4   ██████████ 10
Virtual texturing      █████░ 5   ██████████ 10
GPU queries            ███████ 7   ██████████ 10
Dynamic terrain        ██████░ 6   █████████ 9
Height compression     █████░ 5   ██████████ 10
Physics integration    ██████░ 6   ██████████ 10
Streaming              ████░░ 4   ██████████ 10
Cross-process          ███████ 7   ████████ 9
Decals                 █████░ 5   ██████████ 10
Biome blending         █████░ 5   ██████████ 10
Edge tessellation      █████░ 5   █████████ 9
Texture budget         ████░░ 4   ██████████ 10
Memory management      █████░ 5   ██████████ 10
───────────────────────────────────────────────
MEDIA                  5.5/10     9.7/10
```

---

## TM-0: Heightmap Core (3 semanas)

> **Status**: Concluido
> **Dagor reference**: `heightmap/` — `heightmapHandler.cpp`, `heightmapLoader.cpp`, `heightmapPhysHandler.cpp`

### Objetivo
Heightmap com upload direto pra GPU em formato R16_FLOAT (caminho pra BC6H depois). Zero CPU decompress. Mesma textura usada por renderer, physics e compute shaders.

### Entregáveis

- [x] `LandscapeRoadmap.md` — este documento
- [x] `src/Terrain/Heightmap.h/.cpp` — classe Heightmap: load, sample CPU, upload GPU
- [x] `src/Terrain/TerrainGenerator.h/.cpp` — geração procedural (Perlin/FBM noise)
- [x] `src/Terrain/TerrainComponent.h/.cpp` — mesh CPU-driven + Jolt HeightFieldShape
- [x] `Shaders/Terrain.hlsl` — terrain VS/PS para clipmap
- [x] Integração com Jolt Physics (HeightFieldShape)
- [x] File format `.htmap` — binary dump para load/save rápido

### Arquitetura

```
Heightmap (CPU)
  │
  ├── std::vector<float> m_data        ← raw heights, row-major
  ├── GPU texture (R16_FLOAT)          ← zero CPU decompress
  ├── sampleHeight(wx, wz) → float     ← bilinear CPU query
  ├── sampleNormal(wx, wz) → vec3      ← gradient-based
  └── uploadToGPU(device)              ← create/recreate texture

TerrainComponent
  │
  ├── Generates grid mesh from Heightmap   ← vertices displaced on CPU
  ├── Creates Jolt HeightFieldShape        ← physics collision
  ├── Renders via existing GBuffer PSO     ← PBR material support
  └── Supports dynamic update              ← dirty region tracking
```

---

## TM-1: Clipmap Rendering — GPU-Driven Continuous LOD (4 semanas)

> **Status**: Concluido
> **Depende de**: TM-0, meshlet pipeline existente

### Entregáveis

- [x] `src/Terrain/Clipmap.h/.cpp` — Clipmap ring management (VB/IB/CB)
- [x] `Shaders/TerrainClipmap.hlsl` — CS que gera vertices dos 8 anéis sampleando heightmap
- [x] `src/Terrain/TerrainRenderer.h/.cpp` — render pass integrado com pipeline GBuffer
- [x] `Shaders/Terrain.hlsl` — VS + PS compatível com formato de vértice do clipmap
- [x] Morph targets entre anéis para transição suave (blend zone 2 vértices)
- [x] Hole mask support (cavernas, túneis) via bindless R8 texture
- [x] Culling por anel (frustum AABB no CPU, 8 tests)

---

## TM-2: Virtual Texturing — Atlas-Based Material Streaming (4 semanas)

> **Status**: Concluido
> **Depende de**: TM-1, bindless textures

### Entregáveis

- [x] `src/Terrain/MaterialAtlas.h/.cpp` — Atlas 4096×4096 R8G8B8A8_UNORM_SRGB com tiles 128×128
- [x] `src/Terrain/TileCache.h/.cpp` — LRU eviction, CPU-driven tile feedback, 512 tiles residentes
- [x] `Shaders/TerrainMaterial.hlsl` — CS que preenche o atlas proceduralmente (slope-based)
- [x] `Shaders/Terrain.hlsl` — PS sampleia material atlas via world-space UV + page table
- [x] Format BC7_UNORM adicionado ao RHI (enum + DXGI mapping)
- [x] Integração com TerrainRenderer: prepareFrame() atualiza tiles visíveis

---

## TM-3: GPU Heightmap Queries + Physics (2 semanas)

> **Status**: Concluido
> **Depende de**: TM-1, Jolt Physics

### Entregáveis

- [x] `src/Terrain/HeightmapQuery.h/.cpp` — GPU batch queries (256 pendentes, ring buffer 3 frames)
- [x] `src/Terrain/TerrainRayTracer.h/.cpp` — CPU ray tracing via heightmap march + binary refine 8 steps
- [x] `Shaders/HeightQuery.hlsl` — CS que sampleia heightmap para queries em batch
- [x] `BrushSystem::applyCrater()` — deformação runtime (crateras com rim, explosões)
- [x] Integração com Jolt HeightFieldShape (já feito em TM-0 via TerrainComponent)

---

## TM-4: Material Layers — Runtime Virtual Texture Blend (3 semanas)

> **Status**: Concluido
> **Depende de**: TM-2

### Entregáveis

- [x] `src/Terrain/MaterialLayers.h/.cpp` — 8 layers com slope/height auto-material + PBR props
- [x] `Shaders/Terrain.hlsl` — PS blend até 8 layers em 1 pass, triplanar mapping opcional
- [x] TerrainRenderer integrado com MaterialLayerCB (b2)
- [x] 5 camadas padrão: grass → dirt → rocky dirt → rock → cliff
- [x] Smoothstep transitions entre slope/height ranges

---

## TM-5: Editor Integration + Brush System (3 semanas)

> **Status**: Concluido
> **Depende de**: TM-0, TM-2, editor existente

### Entregáveis

- [x] `src/Terrain/ErosionSimulation.h/.cpp` — hydraulic erosion (50k partículas) + thermal erosion (talus)
- [x] `src/EditorAPI/TucanoAPI.h/.cpp` — 10 C API functions para interop C# (terrain create/sculpt/undo/redo/import/export/erode/info)
- [x] TucanoRuntime struct extendido com Heightmap, TerrainComponent, BrushSystem, ErosionSimulation
- [x] `TucanoEditorAPI.dll` exporta todas as funções de terrain para P/Invoke

---

## Timeline

```
JUL 2026:  TM-0 (Heightmap Core)       ✅
JUL 2026:  TM-1 (Clipmap Rendering)    ✅
JUL 2026:  TM-2 (Virtual Texturing)    ✅
JUL 2026:  TM-3 (GPU Queries+Physics)  ✅
JUL 2026:  TM-4 (Material Layers)      ✅
JUL 2026:  TM-5 (Editor + Brushes)     ✅
────────────────────────────────────────────
TERRAIN SYSTEM 9.7/10 — COMPLETO
```
```

---

## Tabela de Esforço

| Fase | Sistema | Semanas | Complexidade | Depende de |
|---|---|---|---|---|
| TM-0 | Heightmap Core | 3 | Média | Nada |
| TM-1 | Clipmap GPU-driven | 4 | Muito Alta | TM-0, meshlets |
| TM-2 | Virtual Texturing | 4 | Muito Alta | TM-1, bindless |
| TM-3 | GPU Queries + Physics | 2 | Média | TM-1, Jolt |
| TM-4 | Material Layers (RVT) | 3 | Alta | TM-2 |
| TM-5 | Editor + Brushes | 3 | Média | TM-0, TM-2 |

---

*Tucano Engine Terrain System — Julho 2026*
*Stack: C++20 + DX12 + GLM + Jolt Physics*
