# TucanoEngine — Roadmap (C++ / DX12)

> Engine atual: **C++20 + DX12** (Sponza path). Referência arquitetural: **Dagor** (sem linkar código).  
> Estilo de tracking igual ao `ROADMAP-old.md`: checkboxes + % vs Dagor por sistema.  
> **Piso:** não marcar fase como OK até overall ≥ **90%** do que a fase promete (não “LOC race” com Dagor).

## Como ler o % vs Dagor

| % | Significado |
|---|-------------|
| **0–15** | Stub / demo |
| **16–40** | Feature wired, qualidade/robustez longe de shipping |
| **41–70** | Uso real em jogo possível; falta depth Dagor |
| **71–90** | Paridade prática no escopo declarado |
| **91–100** | Igual ou supera Dagor naquele subsystem |

**Overall rendering core vs Dagor hoje:** ~**18%**.

---

## Stack atual

| Sistema | Tech | Maturidade |
|---------|------|------------|
| **RHI** | DX12 (Graphics/Compute/Copy) | Em andamento |
| **Shaders** | HLSL 6 + DXC | Usável |
| **Janela/Input** | GLFW | Maduro |
| **Math** | GLM | Maduro |
| **UI debug** | Dear ImGui | Usável |
| **Assets** | cgltf + stb + DDS BC4/5 | Usável |
| **Meshlets** | meshoptimizer (CPU cull) | Demo |
| **Profiling** | Tracy stub | Stub |

---

## FASE 0: Fundação DX12 (feito / consolidar)

> **vs Dagor RHI bootstrap:** ~**55%**

### 0.1 Projeto
- [x] CMake presets Windows Release/Debug
- [x] Compilação DXC de shaders → `.cso`
- [x] Samples: HelloTriangle, PBRTest, SponzaViewer, Benchmark
- [ ] CI/CD GitHub Actions — builds + smoke screenshot (~20%)
- [ ] Linux/Vulkan path — **0%** (DX12-only)

### 0.2 RHI
> **vs Dagor low-level:** ~**28%**
- [x] Device / SwapChain / CommandList
- [x] Heaps SRV/UAV/RTV/DSV + upload
- [x] Root signature 1.1 + bindless region
- [x] BarrierBatcher + SplitTransitionTracker
- [x] PipelineCache (disk PSO)
- [x] DRED create + breadcrumb dump
- [x] Filas GRAPHICS / COMPUTE / COPY (existem)
- [ ] Async compute **agendando** passes reais (~5%)
- [ ] Nvidia Aftermath (~0%)
- [ ] Bindless em lighting/post (hoje só GBuffer forte) (~40%)

---

## FASE 1: Deferred AAA base

> **vs Dagor deferred + post stack (escopo Sponza):** ~**38%** overall fase

### 1.1 GBuffer + Lighting
> **vs Dagor:** ~**42%**
- [x] GBuffer multi-RT (albedo/normal/ORM/emissive/depthColor)
- [x] Deferred lighting dir + point + spot
- [x] Clearcoat path no shader
- [x] Bindless texturas no GBuffer
- [ ] Extrair `GBufferPass` / `LightingPass` do monolito `Renderer.cpp` (~30%)
- [ ] Material layering / fuzz / detail (~0%)

### 1.2 IBL + Post
> **vs Dagor:** ~**40%**
- [x] BRDF LUT + irradiance + prefiltered (procedural)
- [x] Bloom pirâmide
- [x] ACES tonemap
- [x] AO screen-space básico
- [ ] HDRI cook / cubemap real (~15%)
- [ ] GTAO / HBAO+ (~5%)
- [ ] Auto-exposure / histogram (~0%)

### 1.3 Samples / Tools
> **vs Dagor tooling:** ~**20%**
- [x] SponzaViewer + ImGui tools (rain/lights/renderer)
- [x] FPS HUD + screenshot F12
- [ ] Hot-reload shaders (~0%)
- [ ] Editor viewport (~0%)

---

## FASE 2: Sombras

> **vs Dagor shadows:** ~**30%** overall

### 2.1 Cascades / Atlas
- [x] CSM 4 cascades (atlas)
- [x] Toroidal dirty + GPU tile scroll
- [x] Contact shadows pass
- [x] Octahedral point atlas **fill**
- [ ] Octahedral **sampling** no DeferredLighting (~10% — gap crítico)
- [ ] PCSS blocker search (~5%)
- [ ] ESM wired end-to-end da UI (~25%)
- [ ] Virtual shadow maps (~0%)

---

## FASE 3: RenderGraph + GPU-driven

> **vs Dagor daFG / GPU-driven:** ~**12%** overall

### 3.1 RenderGraph
- [x] Pass builder + import/transient
- [x] Lifetime aliasing (3 packers) + graphviz
- [x] Stats `aliasedBytes` no HUD
- [ ] **Frame dirigido pelo RG** (`execute` ownership) (~5% — hoje só compile/stats)
- [ ] Barriers auto no path real (~20%)
- [ ] Async tips → submit real (~0%)

### 3.2 Meshlets / VisBuffer
> **vs Dagor GPU scene:** ~**18%**
- [x] meshoptimizer clusters + draw packed
- [x] CPU frustum cull meshlets
- [x] VisBuffer ID + resolve toggle
- [ ] Mesh shaders / amplification (~0%)
- [ ] GPU cull + indirect full (~10%)
- [ ] VisBuffer como path default de material (~15%)

---

## FASE 4: GI / Reflexões

> **vs Dagor daGI2 / reflections:** ~**12%** overall

### 4.1 GI
- [x] SSGI taps (tiered, default Off)
- [x] “DDGI” atlas update CS (lite)
- [x] VoxelGI 32³ occupancy CPU
- [x] SkyVisibility 16×16 ambient scale
- [ ] DDGI real (probe rays / irradiance) (~5%)
- [ ] daGI2-class WorldSDF / SH (~0%)
- [ ] Temporal GI estável default-on (~15%)

### 4.2 Reflexões
- [x] SSR march + IBL fallback
- [x] DXR stub (log + off)
- [ ] Hi-Z SSR qualidade (~20%)
- [ ] Reflection probes bake (~0%)
- [ ] DXR reflections quando GPU RT (~0%)

---

## FASE 5: Weather / Atmosphere

> **vs Dagor skies + weather:** ~**15%** overall | **Rain vs Cry/Dagor precip:** ~**35%**

### 5.1 Rain (Cry-inspired)
- [x] Deferred wet GBuffer (puddles / ripples / darkening)
- [x] Streaks texturizados (`rainfall`) + procedurais
- [x] Soft occlusion + mist + lens drops
- [x] ImGui presets + key `9`
- [ ] SceneRain volumétrico estável (cones) (~20% — desligado; SS planes atuais)
- [ ] Poças com SSR/espelho AAA (~40%)
- [ ] Occluder map estilo Cry (mesh rain-space) (~25%)
- [ ] Partículas splash mundo (~10%)

### 5.2 Skies / Clouds / Climate
- [ ] Atmosphere Rayleigh/Mie (~0%)
- [ ] Volumetric clouds (~0%)
- [ ] Day/night + wind global (~5%)
- [ ] Snow / fog volumes (~0%)
- [ ] Rain map world (Dagor clouds rain map) (~0%)

---

## FASE 6: Assets / Streaming

> **vs Dagor cook + streaming:** ~**18%** overall

### 6.1 Pipeline
- [x] glTF load (cgltf)
- [x] AssetCooker (meshlets, mips, BRDF)
- [x] `.tcpkg` pack + ResourceFactory + AssetWatcher
- [x] DDS BC4/BC5 loader (rain textures)
- [ ] daBuild-class cook (BC7 real, deps graph) (~15%)
- [ ] Hot-reload material/shader completo (~10%)

### 6.2 Mundo
- [ ] Chunk streaming 2D (~0%)
- [ ] Terrain heightmap / splat (~0%)
- [ ] HLOD / impostors (~0%)
- [ ] Vegetation GPU instance (~0%)

---

## FASE 7: Runtime jogo (ainda greenfield)

> **vs Dagor gameLibs / ECS:** ~**2%**

- [ ] ECS / scene graph rico (~5% — lista de meshes hoje)
- [ ] Physics (Jolt/PhysX) (~0%)
- [ ] Animation skeletal (~0%)
- [ ] Audio (~0%)
- [ ] AI / navmesh (~0%)
- [ ] Scripting (~0%)
- [ ] Netcode (~0%)

---

## FASE 8: Editor + distribuição

> **vs Dagor tools:** ~**3%**

- [ ] Editor dock (viewport, outliner, inspector) (~0%)
- [ ] Terrain/material/anim editors (~0%)
- [ ] Launcher / patches (~0%)
- [ ] Automated render/perf tests (~5% — Benchmark tool existe)

---

## Prioridade imediata (próximas fatias)

Ordem sugerida para **subir % vs Dagor** no que já começou:

1. **Octahedral sampling** no lighting (fecha gap shadows)  
2. **RG drive frame** (RenderGraph deixa de ser demo)  
3. **Rain:** planes estáveis + poças/SSR (você está aqui)  
4. **GTAO** + GI temporal default-safe  
5. **Bindless lighting/post**  
6. Atmosphere / clouds (maior salto visual open-world)

---

## Snapshot % vs Dagor (por sistema)

| Sistema | Feito (X) | Falta | **% vs Dagor** |
|---------|-----------|-------|---------------|
| RHI DX12 | filas, barriers, bindless GBuf, PSO cache, DRED | async live, Aftermath, bindless full | **28%** |
| Deferred PBR | GBuffer, lights, clearcoat | monolito→passes, materials AAA | **42%** |
| IBL + Post | LUT/IBL proc, bloom, ACES, AO lite | HDRI cook, GTAO, auto-exp | **40%** |
| Shadows | CSM, toroidal, contact, octa fill | octa sample, PCSS, VSM | **30%** |
| RenderGraph | compile + alias | **drive frame** | **12%** |
| GI | SSGI/DDGI-lite/Voxel/SkyVis | daGI2 real | **12%** |
| SSR / Meshlets / Vis | SSR, meshlets CPU, Vis demo | GPU-driven, mesh shaders | **18%** |
| Rain / Weather | wet+streaks+drops | skies/clouds, volume Cry full | **35%** rain / **8%** climate |
| Assets | glTF, cook lite, tcpkg | daBuild, streaming world | **22%** |
| Samples / ImGui | Sponza tools | editor | **20%** |
| ECS / Physics / Audio / Net | — | tudo | **0–5%** |
| **Rendering core overall** | | | **~18%** |

---

## Princípios (C++ path)

1. **Dagor de referência, não de copy-paste** — reimplementar; assets Cry só com licença/uso consciente.  
2. **Sponza é o gate visual** — feature sem path estável no viewer = não `[x]`.  
3. **RG deve dirigir o frame** antes de mais demos GI.  
4. **Qualidade > checkbox** — % vs Dagor é honestidade, não marketing.  
5. **Um job por PR** — shadows / rain / RG em fatias reviewable.

---

*Gerado a partir do estado do repo em 2026-07-21. `ROADMAP-old.md` = plano Zig legado (arquivo histórico).*
