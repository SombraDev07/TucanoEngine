# ROADMAP DE VEGETACAO AAA — TucanoEngine

> Referencias: UE5 Nanite Foliage, Horizon Forbidden West GPU-driven vegetation, Dagor Engine vegetation system, SpeedTree pipeline.

---

## Fase Veg-0: Fundacao (feito)
| Item | Status |
|------|--------|
| GPU-Driven Rendering (compute cull + indirect draw) | ✅ 85% |
| InstanceCloud system | ✅ |
| World streaming (Morton cells) | ✅ |
| VegetationSystem (types, placement, LOD, scattering) | ✅ Novo |
| WindSystem (Perlin noise, gusts, turbulence) | ✅ Novo |
| VegetationTool (ImGui: type editor, scatter, wind) | ✅ Novo |
| Lua bindings (veg.*) | ✅ Novo |
| Vegetation ECS component | ⬜ |

---

## Fase Veg-1: GPU Wind + Instanced Rendering (4-6 semanas)
```
Target: 100k grass blades at 60fps with wind animation
```

| Item | Descricao |
|------|-----------|
| **Compute Wind Shader** | HLSL compute shader que processa instancias em batch no GPU |
| **Indirect Draw** | `ExecuteIndirect` com instance buffer atualizado pelo compute |
| **Wind Texture** | Noise texture 3D pre-computada para lookup no VS |
| **LOD Cross-Fade** | Transicao suave entre LODs via dithering |
| **Frustum Culling GPU** | Culling por cell no compute shader |
| **Benchmark Scene** | 100k grass blades, 60fps target |

---

## Fase Veg-2: Hierarchical LOD (3-4 semanas)
```
Target: Vegetacao visivel ate 500m com transicoes imperceptiveis
```

| Item | Descricao |
|------|-----------|
| **Billboard Atlas** | Atlas de texturas com billboards pre-renderizados |
| **Impostor Baking** | Offline bake de impostors por angulo |
| **HLOD Tree** | Arvore hierarquica de LOD (mesh → simplified → billboard → culled) |
| **Density Scaling** | Reducao de densidade por distancia (screen-size based) |
| **Virtual Texture** | Page table update based on visible vegetation |

---

## Fase Veg-3: Biomas + Procedural (4-6 semanas)
```
Target: Biomas com regras de placement (floresta, campo, pântano)
```

| Item | Descricao |
|------|-----------|
| **Biome Rules** | JSON/YAML: slope, altitude, water proximity, density maps |
| **Density Maps** | Mapas de densidade pintaveis (R16_FLOAT, import/export) |
| **Cluster Scattering** | Grupos naturais (Poisson disk, clumping) |
| **Ground Adaptation** | Snap to terrain height, slope alignment |
| **Exclusion Zones** | Caminhos, rios, areas sem vegetacao |
| **LOD Baking Pipeline** | Ferramenta offline para bake de LOD chains |

---

## Fase Veg-4: Interacao + Dinamica (4-6 semanas)
```
Target: Vegetacao reage ao player (colisao, forca)
```

| Item | Descricao |
|------|-----------|
| **Player Interaction** | Grass bend/flatten ao redor do player |
| **Physics Collision** | Capsula de interacao que afeta offset de vertice |
| **Dynamic Wind Events** | Rajadas, explosao, helicoptero |
| **Season System** | Transicao de cores por estacao do ano |
| **Growth System** | Vegetacao cresce com o tempo (gameplay) |
| **Cutting/Destruction** | Remocao dinamica de instancias individuais |

---

## Fase Veg-5: Authoring Tools (6-8 semanas)
```
Target: Ferramentas de authoring nivel AAA
```

| Item | Descricao |
|------|-----------|
| **Vegetation Editor** | Janela dedicada: type browser, material assign, preview |
| **Paint Tool** | Pintar vegetacao no terreno (brush: size, density, falloff) |
| **Scatter Tool** | Scatter procedural com preview em tempo real |
| **Biome Editor** | Editor visual de regras de bioma |
| **Performance Heatmap** | Overlay de LOD/densidade/polycount no viewport |
| **Preset System** | Salvar/carregar presets de vegetacao |
| **Batch Baker** | Pipeline offline para LODs, impostors, billboards |

---

## Fase Veg-6: Otimizacao AAA (4-6 semanas)
```
Target: 1M+ instances at 60fps, qualidade cinematografica
```

| Item | Descricao |
|------|-----------|
| **GPU Occlusion Culling** | Hi-Z occlusion no compute shader |
| **Mesh Shaders** | Amplification + mesh shader para vegetacao |
| **Nanite-style Clusters** | Cluster-based LOD com pagina virtual de geometria |
| **Ray Traced Shadows** | DXR shadows em distancia curta |
| **Variable Rate Shading** | VRS Tier 2 em foliage distante |
| **Async Compute** | Wind + culling no compute queue em paralelo |
| **Memory Pool** | Pooled instance buffers, zero alloc per frame |
| **Profile-Guided** | Auto-tuning de LOD distances por hardware |

---

## Cronograma Estimado

| Fase | Tempo | Marco |
|------|-------|-------|
| Veg-1 GPU Wind | 4-6 sem | 100k grass @ 60fps |
| Veg-2 HLOD | 3-4 sem | Vegetacao ate 500m |
| Veg-3 Biomas | 4-6 sem | Biomas procedural |
| Veg-4 Interacao | 4-6 sem | Player interaction |
| Veg-5 Tools | 6-8 sem | Ferramentas AAA |
| Veg-6 Otimizacao | 4-6 sem | 1M+ @ 60fps |

**Total estimado: 25-36 semanas (6-9 meses, 1-2 devs)**

---

## Status Atual (Jul 2026)

Sprint interno P0–P2 (render unblock + paint + AAA mínimo + impostors/Hi-Z):

| Sprint | Status | Entrega |
|--------|--------|---------|
| **P0** | ✅ | Mesh/LOD, cull+wind CB, paint mesh/leaf/plant, Lua/C API |
| **P1** | ✅ | Ring upload, LOD GPU, foliage shading, shadows, season tint |
| **P2** | ✅ | Billboard LOD2 + atlas yaw, Hi-Z cull, WaveActiveCountBits |

```
Veg-0:  █████████░  90%  (faltam: ECS component)
Veg-1:  ████████░░  80%  (GPU wind + indirect + frustum; falta wind 3D tex)
Veg-2:  ██████░░░░  60%  (billboard/impostor procedural; falta bake offline real)
Veg-3:  ░░░░░░░░░░   0%
Veg-4:  ████░░░░░░  40%  (season/growth/destruction CPU; falta interaction física)
Veg-5:  █████░░░░░  50%  (paint + types UI; falta biome editor / heatmap)
Veg-6:  ███░░░░░░░  30%  (Hi-Z + wave compaction; falta async compute / mesh shaders)
```
