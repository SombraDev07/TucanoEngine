# Terrain System — TucanoEngine / B3DFramework

Implementação de terreno AAA para o B3DFramework, inspirada e parcialmente adaptada do **Dagor Engine 6.5** (licença MIT, Gaijin Games KFT).

---

## Arquivos

```
Source/Engine/Core/Terrain/
├── B3DHeightmapData.h/.cpp          ← Dados do heightmap + hierarquia min/max + raycasting
├── B3DTerrainLodGrid.h/.cpp         ← Geo-Clipmap LOD grid com morph e frustum culling
├── B3DTerrainVirtualTexture.h       ← Virtual Texture (atlas + indirection + LRU)
└── B3DTerrainSystem.h               ← Module singleton — ponto de entrada

Data/Shaders/Terrain/
└── TerrainHeightmap.bsl             ← Vertex+Pixel shader (morph + vtex sampling)
```

---

## Técnicas Usadas

| Técnica | Fonte de Inspiração |
|---|---|
| **Geo-Clipmap (LodGrid)** | Dagor `lodGrid.h` + `cull_lod_grid()` (MIT) |
| **Morph geométrico** | Dagor heightmap shaders (`hmap_vs.hlsl`) |
| **Hierarquia min/max** | Dagor `CompressedHeightmap::recomputeHierHeightRangeBlocks()` (MIT) |
| **Virtual Texture** | Dagor `Clipmap` / `ClipmapImpl` (MIT) |
| **Raycasting adaptativo** | Dagor `dag_hmlTraceRay.h` (MIT) |

---

## O que Ainda Falta Implementar

1. **`B3DTerrainVirtualTexture.cpp`** — Implementação do atlas LRU + feedback + upload de tiles
2. **`B3DTerrainSystem.cpp`** — Implementação do módulo (load heightmap, drive update/render)
3. **Index/Vertex buffer compartilhado** — Para o grid de patches (kPatchDim × kPatchDim quads)
4. **Integração com o `B3DRenderer`** — Registrar como `RendererExtension`
5. **GPU feedback pass** — UAV buffer para virtual texture mode `kGpuUAV`
6. **Tessellation HW** — Hull/Domain shader opcional para qualidade extra em close-up
7. **Erosão procedural** — GPU Compute (hydraulic + thermal) para geração de assets

---

## Roteiro de Implementação (Fases)

```
Fase 1 — Fundação  ✅ (headers criados)
  ✅ HeightmapData (load, query, min/max, raycast)
  ✅ TerrainLodGrid (geo-clipmap, frustum cull, morph flags)
  ✅ TerrainVirtualTexture (estrutura + API)
  ✅ TerrainSystem (module API)
  ✅ TerrainHeightmap.bsl (shader morph + vtex)

Fase 2 — Implementações concretas
  [ ] HeightmapData já implementado
  [ ] TerrainLodGrid já implementado
  [ ] TerrainVirtualTexture.cpp
  [ ] TerrainSystem.cpp
  [ ] Integração GPU buffers (patch instances)

Fase 3 — Rendering pipeline
  [ ] RendererExtension para o terrain
  [ ] Depth pre-pass
  [ ] Shadow map support
  [ ] Grass mask output

Fase 4 — Polish AAA
  [ ] HW Tessellation shader
  [ ] GPU feedback (UAV)
  [ ] BC7 atlas compression
  [ ] Deformação em runtime
```

---

## Referências no Dagor Engine

- [`prog/gameLibs/landMesh/`](../DagorEngine-main/prog/gameLibs/landMesh/) — LandMesh, VirtualTexture, Culling
- [`prog/gameLibs/heightmap/`](../DagorEngine-main/prog/gameLibs/heightmap/) — HeightmapHandler, LOD culling, Physics
- [`prog/engine/heightMapLand/`](../DagorEngine-main/prog/engine/heightMapLand/) — CompressedHeightmap
- [`prog/gameLibs/publicInclude/landMesh/virtualtexture.h`](../DagorEngine-main/prog/gameLibs/publicInclude/landMesh/virtualtexture.h) — Clipmap API
- [`prog/gameLibs/publicInclude/heightmap/lodGrid.h`](../DagorEngine-main/prog/gameLibs/publicInclude/heightmap/lodGrid.h) — LodGrid struct
