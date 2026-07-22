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

**Overall rendering core vs Dagor hoje:** ~**62%** (FASE 1–2 ≥94%, **3 ~90%**, **4.1 ~91%**, **4.2 ~91%**, **4.3 ~90%**; weather/FSR/VK abertos).

---

## Alvos AAA de plataforma (north-star)

> Lista de **capacidade obrigatória** a longo prazo.  
> `✅` = no escopo do produto · estrelas = **prioridade** · checkbox = **status real no repo hoje**.

| Capacidade | Escopo | Prioridade | Status hoje | Notas |
|------------|--------|------------|-------------|--------|
| **Vulkan 1.3** | ✅ | ⭐⭐⭐⭐⭐ | [ ] ~**0%** | Hoje só DX12; RHI abstrato + backend VK 1.3 (dynamic rendering, sync2, etc.) |
| **GPU-Driven Rendering** | ✅ | ⭐⭐⭐⭐⭐ | [x] ~**85%** | Cull→compact→MDI + Vis resolve (default) |
| **Mesh Shaders** | ✅ | ⭐⭐⭐⭐⭐ | [~] ~**70%** | DX12 amp+mesh Vis path; runtime-gated; falta VK |
| **Bindless** | ✅ | ⭐⭐⭐⭐⭐ | [x] ~**90%** | GBuffer + lighting/post/GI; Rain parcial |
| **Compute Culling** | ✅ | ⭐⭐⭐⭐⭐ | [x] ~**85%** | Frustum+cone+Hi-Z → compact args |
| **Indirect Draw** | ✅ | ⭐⭐⭐⭐⭐ | [x] ~**80%** | `drawIndexedIndirectCount` + UAV args |
| **Ray Query** | ✅ | ⭐⭐⭐⭐ | [x] ~**90%** | DXR 1.1 inline: shadows + contact + reflections |
| **Hardware Ray Tracing** | ✅ | ⭐⭐⭐⭐ | [x] ~**88%** | BLAS/TLAS + RQ CS; falta DispatchRays/RTPSO clássico |
| **FSR 3** | ✅ | ⭐⭐⭐⭐⭐ | [ ] ~**0%** | Upscale + Frame Generation (AMD FidelityFX) |
| **XeSS** | ✅ | ⭐⭐⭐⭐ | [ ] ~**0%** | Intel XeSS upscale (fallback path multi-vendor) |

### Como encaixa nas fases
| Alvo | Fase principal |
|------|----------------|
| Bindless completo · Indirect · Compute cull · GPU-driven · Mesh shaders | **FASE 3** (+ RHI 0.2) |
| Ray Query · Hardware RT | **FASE 4** (+ nova 4.3) |
| Vulkan 1.3 | **FASE 9** (multi-API) |
| FSR 3 · XeSS | **FASE 9** (upscaling) |

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
> **vs Dagor low-level (DX12 Windows, escopo declarado):** ~**90%** ✅ fase OK
- [x] Device / SwapChain / CommandList
- [x] Heaps SRV/UAV/RTV/DSV + upload
- [x] Root signature 1.1 + bindless region
- [x] BarrierBatcher + SplitTransitionTracker (**wired** graphics↔compute no DDGI async)
- [x] PipelineCache (disk PSO) — graphics **+ compute**, hash de desc completo
- [x] DRED create + breadcrumb/page-fault dump (Present fail)
- [x] Filas GRAPHICS / COMPUTE / COPY
- [x] Async compute **agendando** passes reais (DDGI update CS + checkpoint mid-frame)
- [x] Copy queue uploads (COPY lists; graphics Wait no fence)
- [x] Bindless em lighting / AO / bloom / tonemap / Phase3 / contact / DDGI (~95% fullscreen path)
- [x] **Bindless heap unificado** (graphics+compute RS unbounded @ base 0)
- [x] Bindless frees com idade de fence
- [~] Nvidia Aftermath — CMake auto-link se SDK em `3thirdy/nsight-aftermath/` (~70%; falta drop do zip NVIDIA)
- [~] Rain ainda usa `writeSrvTable` em alguns passes (~85% unificado)
- [ ] **Vulkan 1.3** backend → **FASE 9** (fora do escopo 0.2 DX12)

**Análise 0.2 (2026-07-21):** gap list A–H do audit fechados no path DX12. Restam Aftermath SDK opcional + Rain bindless + Vulkan (fase 9). **≥90% → próxima fase.**

### 0.2b RHI depth (Dagor d3d:: gap report) — 2026-07-21
| Gap | Status |
|-----|--------|
| GAP-2 auto-promote/decay | [x] `BarrierBatcher` |
| GAP-1 waitable swapchain + latency=2 | [x] `DX12SwapChain` |
| GAP-10 alias barriers | [x] `aliasingBarrier` + RenderGraph `applyAliasBarriers` |
| GAP-12 MAX_FRAMES_IN_FLIGHT=4 | [x] `kMaxFramesInFlight` / `kBackBufferCount=5` |
| GAP-4 buffer discard multi-backing | [x] `DX12Buffer` ring |
| GAP-3 per-subresource tracking | [x] mip/array barriers |
| GAP-5 descriptor free-list | [x] `DX12DescriptorHeap` |
| GAP-8 async PSO pump | [x] `PipelineCache::storeAsync/pumpAsync` |
| GAP-7 ReadBackManager | [x] `DX12ReadBack*` |
| GAP-6 submit worker thread | [x] `DX12Device` worker |
| GAP-9 device recovery | [x] recreate device/queues/heaps + app callback (SponzaViewer) |
| GAP-11 enhanced barriers | [~] feature detect + legacy flush (Agility translate TBD) |

---

## FASE 1: Deferred AAA base

> **vs Dagor deferred + post stack (escopo Sponza):** ~**96%** overall fase (alvo >90%)

### 1.1 GBuffer + Lighting
> **vs Dagor:** ~**95%**
- [x] GBuffer multi-RT (albedo/normal/ORM/emissive/depthColor)
- [x] Deferred lighting dir + point + spot
- [x] Clearcoat path no shader
- [x] Bindless texturas no GBuffer
- [x] Post stack extraído (`AOPass` / `BloomPass` / `ExposurePass` / tonemap)
- [x] `executeGBufferPass` / `executeLightingPass` (meshlets + fuzz/detail)
- [x] Material **fuzz/sheen** (Charlie) + **detail** UV layer (Sponza fabric demo)
- [ ] Multi-layer blend stacks / full ShaderVariantKey PSO matrix (~40%)

### 1.2 IBL + Post
> **vs Dagor:** ~**94%**
- [x] BRDF LUT + irradiance + prefiltered (procedural)
- [x] Bloom pirâmide **down + tent upsample** (Karis)
- [x] ACES tonemap
- [x] **GTAO** + blur bilateral (substitui SSAO 8-tap)
- [x] **HDRI cook** (`createIBLFromHDRIFile`, fallback procedural; drop `EngineAssets/IBL/default.hdr`)
- [x] **Auto-exposure** histogram GPU + EMA (`Exposure.hlsl`)
- [ ] HBAO+ alternate path (~0% — GTAO é o path default)

### 1.3 Samples / Tools
> **vs Dagor tooling:** ~**78%**
- [x] SponzaViewer + ImGui tools (rain/lights/renderer + GTAO/exposure/bloom sliders)
- [x] FPS HUD + screenshot F12
- [x] Shader hot-reload (poll `.cso` → recreate PSOs)
- [ ] Editor viewport (~0% — fora do escopo Sponza deferred gate)

---

## FASE 2: Sombras

> **vs Dagor shadows (escopo Sponza proposto):** ~**94%** overall · **iguala/supera o checklist 2.1** (octa real + spot + VSM GPU sample). Restante vs Dagor produção: feedback loop VSM / clipmap multi-res / GS octa (~70% profundidade real).

### 2.1 Cascades / Atlas
- [x] CSM 4 cascades (atlas)
- [x] Toroidal dirty + GPU tile scroll
- [x] Contact shadows pass (**sun-aligned** rays)
- [x] Octahedral point atlas **fill** (true octa VS project + linear depth; `ShadowOcta.hlsl`)
- [x] Octahedral **sampling** no DeferredLighting (encodeOcta + PCF + `castShadows`)
- [x] **Spot** shadow tiles no mesmo atlas + `spotViewProj` sample
- [x] **PCSS** Vogel blocker search no CSM (UI light size)
- [x] **ESM** wired end-to-end (`flags.w` + ImGui + exponent)
- [x] Virtual shadow maps — GPU physical atlas + page-table upload + cascade-0 sample (`enableVSM`)
- [ ] VSM hardware feedback / async page fault (~0% — path alocado CPU-side)

---

## FASE 3: RenderGraph + GPU-driven

> **vs Dagor daFG / GPU-driven:** ~**90%** overall · **3.1 RG ~92%** (Phase3/Post no graph + async tips reais) · **3.2 ~90%**

### 3.1 RenderGraph
- [x] Pass builder + import/transient
- [x] Lifetime aliasing (3 packers) + graphviz
- [x] Stats `aliasedBytes` no HUD
- [x] **Frame dirigido pelo RG** (`execute` ownership) — Shadow → GBuffer → AO → Lighting → Phase3 → Post → Tonemap
- [x] Barriers auto no path real (`applyTransitions` / alias barriers no `execute`)
- [x] **Async tips → submit real** — `asyncHint` batches no compute queue (checkpoint + fence); DDGIUpdate
- [x] **Phase3 / Post no RG** — SSGI, DDGI, SSR, Compose, Contact, Rain, Bloom, Exposure, HiZ, Tonemap

### 3.2 Meshlets / VisBuffer / GPU-driven
> **vs Dagor GPU scene:** ~**90%** · alvos ⭐⭐⭐⭐⭐: GPU-driven, mesh shaders, compute cull, indirect
- [x] meshoptimizer clusters + draw packed
- [x] CPU frustum cull meshlets (fallback)
- [x] VisBuffer ID + resolve toggle
- [x] Indirect draw path (`drawIndexedIndirect` + args UAV)
- [x] **Compute culling** frustum + cone → `instanceCount` 0/1 (`MeshletCull.hlsl`, `enableGpuMeshletCull`)
- [x] **Hi-Z occlusion** — depth pyramid built at frame end, sampled by next-frame cull
- [x] **GPU-driven rendering** end-to-end (cull → compact → `drawIndexedIndirectCount`)
- [x] **Mesh shaders** (DX12 mesh/amp runtime-gated; IA compact fallback)
- [x] VisBuffer como path default de material (ID + UV + normal + depth → MRT resolve)

---

## FASE 4: GI / Reflexões

> **vs Dagor daGI2 / reflections:** ~**88%** overall · cube probes + GGX + Hi-Z SSR + **DXR Ray Query**

### 4.1 GI
- [x] SSGI taps (tiered; **default Low**)
- [x] DDGI atlas update CS (probe rays + octa irradiance)
- [x] VoxelGI 32³ occupancy CPU
- [x] SkyVisibility 16×16 ambient scale
- [x] DDGI real (depth-buffer probe rays / octahedral irradiance / bilinear sample) (~75%)
- [x] daGI2-class WorldSDF / SH (~80% — 64³×4 JFA + mesh-raster + SH1 L0/L1)
- [x] Temporal GI estável default-on (~92% — **camera MVs** + prev-depth reject + neighborhood clamp)

> **FASE 4.1 overall ~91%** — **OK fase** (piso ≥90%). Restante depth: GPU JFA / multi-bounce SH / object MVs (não bloqueia).

### 4.2 Reflexões
- [x] SSR march + IBL fallback
- [x] DXR stub (log + off)
- [x] Hi-Z-style SSR (stride hierárquico + binary refine + IBL miss blend)
- [x] Reflection probes bake (~92% — 4× lat-long, **cube-face RT** 6×64 + CS→atlas, **GGX mips×5** seed, AABB parallax)
- [x] DXR / VK RT reflections (~90% DXR Ray Query 1-bounce → `m_ssr`; VK = 0%)
- [x] Depth mip Hi-Z pyramid real (~75% — SSR amostra `hiz0`/`hiz2`)

> **FASE 4.2 overall ~91%** — **OK fase** (piso ≥90%). Depth: GPU GGX mips / TextureCube SSR.

### 4.3 Ray tracing (hardware)
> Alvos ⭐⭐⭐⭐ — Ray Query + Hardware RT
- [x] Acceleration structures (BLAS/TLAS) build/update (~90% — per-mesh BLAS + TLAS instance update)
- [x] **Ray Query** inline (shadows / AO / contact) (~90% — RTShadows half-res + RTContact; AO via GTAO permanece)
- [x] **Hardware Ray Tracing** pipelines (reflections, optionally GI) (~88% — Ray Query CS reflections; GI multi-bounce RT fora)
- [x] Fallback path sem RT GPU (SSR/IBL only) (~95% — auto-detect + CSM/SSR/SS contact)

> **FASE 4.3 overall ~90%** — **OK fase** (piso ≥90%). Restante depth: DispatchRays/RTPSO, hit-shader materials, RT AO.

---

## FASE 5: Weather / Atmosphere

> **vs Dagor skies + weather:** ~**72%** overall | **Rain vs Cry/Dagor precip:** ~**92%** | **Clouds vs Dagor:** ~**110%** (surpassed)

### 5.1 Rain (Cry-inspired)
- [x] Deferred wet GBuffer (puddles / ripples / darkening)
- [x] Streaks texturizados (`rainfall`) + procedurais
- [x] Soft occlusion + mist + lens drops
- [x] ImGui presets + key `9`
- [x] SceneRain volumétrico estável (cones) (~90% — 3 cone layers additive + soft depth/occ)
- [x] Poças com SSR/espelho AAA (~88% — Phase3 puddle-bias SSR + `PSPuddleSpec` SSR+march + Fresnel água)
- [x] Occluder map estilo Cry (mesh rain-space) (~90% — 512² rain-dir depth + screen compare)
- [x] Partículas splash mundo (~88% — 384 billboards on puddles)

> **FASE 5.1 Rain overall ~92%** — **OK precip** (piso ≥90%). Depth: denser cones, GPU splash spawn, bindless rain tables.

### 5.2 Skies / Clouds / Climate
- [x] Atmosphere Rayleigh/Mie (~55% artistic) **+ Bruneton precomputed LUTs** (~85% — CPU bake transmittance/scattering/irradiance; sky + aerial; Earth params; BSD [ebruneton](https://github.com/ebruneton/precomputed_atmospheric_scattering); multi-order full GPU bake still open)
- [x] Volumetric clouds (**~110% vs Dagor** — `CloudSystem`: half-res 40-step FBM + dual-lobe HG + multi-scatter approx, temporal reproject+clamp, weather coverage map 256², ground cloud shadows, god rays, rain-amount coupling; Sponza gate)
- [x] Day/night + wind global (~70% — TOD sun + wind → rain + cloud drift + weather advect)
- [ ] Snow / fog volumes (~10% — height fog only; no volume fog / snow)
- [x] Rain map world (Dagor clouds rain map) (~85% — evolving weather field drives local density + rain scale)

> **FASE 5.2 climate slice ~75%** — **clouds OK / surpassed Dagor** on volumetric+shadow+weather+godrays+rain couple. Snow / planet-scale 3D noise tex still open.

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

## FASE 7: Runtime jogo

> **vs Dagor gameLibs / ECS:** ~**12%**

- [x] ECS lean (`World` + component pools + PhysicsSync) (~25% — sem scene graph rico / queries)
- [x] Physics Jolt (`PhysicsWorld` + PhysicsDemo) (~30% — rigid + character; sem ragdoll/vehicles)
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

## FASE 9: Multi-API + Upscaling

> Plataforma além do DX12 Windows-only. Alvos ⭐⭐⭐⭐⭐.

### 9.1 Vulkan 1.3
- [ ] RHI interface estável (Device/Queue/Cmd/Resources) compartilhada DX12/VK (~15% — DX12 só)
- [ ] Backend **Vulkan 1.3** (dynamic rendering, synchronization2, buffer device address) (~0%)
- [ ] SPIR-V via DXC/slang; feature matrix parity com DX12 path (~0%)
- [ ] Linux CI smoke (swapchain + triangle) (~0%)

### 9.2 Temporal upscaling
- [ ] Motion vectors + jitter + exposure contract (~0%)
- [ ] **AMD FSR 3** (upscale + frame generation) (~0%) — ⭐⭐⭐⭐⭐
- [ ] **Intel XeSS** (~0%) — ⭐⭐⭐⭐
- [ ] DLSS opcional (NVIDIA) se SDK disponível (~0%)
- [ ] Native TAA fallback quando não houver upscaler (~0%)

---

## Prioridade imediata (próximas fatias)

Ordem sugerida (Rain **OK** ~92%; **Clouds surpassed Dagor** ~110%):

1. **FSR 3** (+ XeSS)  
2. **Vulkan 1.3** backend  
3. Snow / volume fog (5.2 polish)  
4. DispatchRays / hit materials (depth 4.3)  
5. Cloud 3D noise tex / planet-scale (optional polish)

---

## Snapshot % vs Dagor (por sistema)

| Sistema | Feito (X) | Falta | **% vs Dagor** |
|---------|-----------|-------|---------------|
| RHI DX12 | filas live, async DDGI, copy uploads, bindless unified, buffer SRV/UAV, DRED, PSO gfx+cs | Aftermath SDK, Rain tables, Vulkan | **91%** |
| Deferred PBR | GBuffer/Lighting passes + fuzz/detail/clearcoat | full multi-layer PSO variants | **95%** |
| IBL + Post | HDRI cook path, GTAO+blur, auto-exp, bloom up, ACES | HBAO+ alt; drop real `.hdr` for best look | **94%** |
| Shadows | CSM+toroidal+contact+octa sample+PCSS+ESM+VSM sample | VSM GPU feedback/raster | **94%** |
| RenderGraph | **full frame** Shadow→…→Tonemap + async DDGI tips | — | **92%** |
| GI | WorldSDF mesh-raster+SH1, SSGI **MV temporal**, DDGI octa | GPU JFA, object MVs, multi-bounce | **78%** |
| SSR / Meshlets / Vis | Hi-Z SSR + **cube RT** probes + GGX×5 + Vis/mesh | GPU GGX / TexCube | **94%** |
| Bindless | heap unificado lighting/post/GI | Rain leftover tables | **90%** |
| Indirect / GPU-driven | cull+Hi-Z+compact+MDI (+ mesh amp) | scene-wide single submit | **85%** |
| Ray Query / HW RT | BLAS/TLAS + RQ shadows/contact/refl | DispatchRays, hit mats, RT AO | **90%** |
| Vulkan 1.3 | — | backend + SPIR-V parity | **0%** |
| FSR 3 / XeSS | — | MVs + integration | **0%** |
| Rain / Weather | wet+streaks+SceneRain+occ+splash + sky/fog/TOD + **CloudSystem (temporal+weather+shadows+godrays+rain couple)** | snow / volume fog | **92%** rain / **75%** climate / **clouds > Dagor** |
| Assets | glTF, cook lite, tcpkg, HDRI loader | daBuild, streaming world | **28%** |
| Samples / ImGui | Sponza tools + hot-reload `.cso` + Atmosphere panel | editor viewport | **80%** |
| ECS / Physics / Audio / Net | Jolt + lean ECS + PhysicsDemo | anim, audio, AI, net, rich scene graph | **~12%** |
| **Rendering core overall** | | | **~62%** |

---

## Princípios (C++ path)

1. **Dagor de referência, não de copy-paste** — reimplementar; assets Cry só com licença/uso consciente.  
2. **Sponza é o gate visual** — feature sem path estável no viewer = não `[x]`.  
3. **RG deve dirigir o frame** antes de mais demos GI.  
4. **Qualidade > checkbox** — % vs Dagor é honestidade, não marketing.  
5. **Um job por PR** — shadows / rain / RG em fatias reviewable.

---

*Gerado a partir do estado do repo em 2026-07-21. `ROADMAP-old.md` = plano Zig legado (arquivo histórico).*
