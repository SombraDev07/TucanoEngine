# Tucano — Vulkan / Linux Roadmap

Status: **fases 0–6 path-default verdes** (2026-08-18). HelloTriangle 0/0; PBRTest deferred 0/0; `SponzaViewer --frames 90` 0/0 com **CSM + AO + bloom + SSR + contact + auto-exposure + rain fullscreen**. Fase 7: `TestEditor --frames 8` ImGui Vulkan 0/0. Cones/partículas de chuva, clouds/water/fog, probe-bake, meshlets/DXR ainda off. **Continua só `--frames N`.**  
Codebase: `/home/bruno/TucanoEngine`  
Constraint: Direct3D 12 continua o backend de produção no Windows. Vulkan é um **segundo backend**, um `.cpp` (pasta) paralelo ao DX12 — nunca os dois no mesmo binário.

O contrato público é `src/RHI/RHI.h`. A engine **não deve** incluir `d3d12.h` nem `vulkan.h` fora do backend ativo. Fases 0–1 fecharam o vazamento DX12 no renderer e o host Linux; fase 2 é `src/RHI/Vulkan/` + `HelloTriangle` em SPIR-V.

**Não começar por mesh shaders nem DXR.** O primeiro verde (já batido) é o sample `HelloTriangle` (VS+PS) com validation layers em zero.

Lição do Esoterica (mesmo GPU, RADV NAVI22): GLFW + bindless por array tipado + mesh shaders opcionais tornam o Tucano *mais fácil de começar*. O que pesa aqui é volume do renderer e o vazamento DX12 — por isso a fase 0 fecha o contrato **antes** de ligar o Vulkan.

---

## Decisões (não reabrir)

| Decisão | Por quê |
|---|---|
| Um backend por binário (`TUCANO_RHI_DX12` **ou** `TUCANO_RHI_VULKAN` **ou** `TUCANO_RHI_NULL`) | Dois RHIs no mesmo exe duplicam `Device::create` e misturam heaps. |
| Linux default = Vulkan. Windows default = DX12. | DX12 não existe no Linux; Vulkan no Windows é extra, não o path de produção. |
| Superfície via GLFW (`glfwCreateWindowSurface`) | `Window` já é GLFW + `GLFW_NO_API`. Não copiar o Xlib cru do Esoterica. |
| Bindless = arrays tipados (`Texture2D bindlessHeap[] : register(t0, space0)`), **não** `ResourceDescriptorHeap` SM 6.6 | Já é o modelo dos 46 HLSL. Mapeia 1:1 para descriptor arrays Vulkan (`UPDATE_AFTER_BIND` + `PARTIALLY_BOUND`). |
| Mesh shaders e DXR ficam **capability flags** | `supportsMeshShaders()` / `supportsRaytracing()` já existem. Renderer já cai em VS+PS. RT no Linux é fase 8. |
| DXC emite SPIR-V (`-spirv -fspv-target-env=vulkan1.3`) | Shaders já são HLSL. Sem glslang, sem segundo shading language. |
| ImGui: `imgui_impl_glfw` + backend de render do RHI ativo | Hoje `imgui_lib` linka `imgui_impl_dx12` + `d3d12`. No Linux isso nem configura. |
| VMA na fase 3, não no triângulo | `vkAllocateMemory` mínimo basta pro HelloTriangle (igual Esoterica). |

---

## O que já ajuda (não reinventar)

- RHI virtual completa: device, command list, swapchain, bindless por índice, barriers, copies, indirect, compute, mesh (default no-op), DXR (default no-op).
- Backend Null compilando só contra `RHI.h`.
- GLFW 3.4 FetchContent, input, filesystem (`std::filesystem` — já portável).
- Sample `HelloTriangle` é o teste de fase 2 **já escrito** — só troca o bytecode `.cso` → `.spv` e o `nativeHandle()`.
- Mesh shaders opcionais (`Renderer.cpp` checa `supportsMeshShaders()`).
- rpmalloc + `memoryInitThreadHeap()` — no Esoterica o callback de validation crashou sem heap de thread; aqui o init já existe.
- Editor ImGui nativo (`EditorShell`) — não precisa inventar `main`.

## O que trava o Linux hoje

**Vazamento DX12 (obrigatório fechar na fase 0):**

`World/InstanceCloudCuller.cpp`, `World/GpuCellCuller.cpp`, `Vegetation/VegetationDispatch.cpp`, `Terrain/{MaterialAtlas,TerrainRenderer,HeightmapQuery,Clipmap}.cpp`, `Runtime/{Screenshot,DebugUI}.cpp`, `Renderer/Renderer.cpp`, `Renderer/Deferred/{GBufferPass,LightingPass}.cpp`, `Renderer/PostFX/{AOPass,BloomPass,ExposurePass}.cpp`, `Renderer/GI/WorldSDF.cpp`, `Renderer/Weather/{Rain,Cloud,Fog,Water}System.cpp`, `Renderer/RayTracing/RayTracingScene.cpp`, `Samples/LuaLab/main.cpp`.

O padrão é `static_cast<DX12Device&>` + `D3D12_CPU_DESCRIPTOR_HANDLE` + `writeSrvTable`. A RHI já tem `setGraphicsRootSrvTable(rootIndex, heapIndex)` e `bindlessIndex()` — a maioria desses casts é tabela transiente, o próprio DX12Device comenta *“prefer bindless + table base 0”*.

**Ilha Win32 (fase 1):**

| Hoje | Linux |
|---|---|
| `Window::nativeHandle()` = `glfwGetWin32Window` | `GLFWwindow*` + `glfwCreateWindowSurface` no backend Vulkan |
| `GLFW_EXPOSE_NATIVE_WIN32` incondicional | `#ifdef` WIN32 / X11, ou melhor: não expor native no Platform — o RHI Vulkan pede o `GLFWwindow*` |
| `imgui_impl_dx12` + link `d3d12 dxgi dxguid d3dcompiler` | `imgui_impl_vulkan` só no binário Vulkan |
| `Editor/SystemDialogs.cpp` (IFileDialog COM) | zenity / `portable-file-dialogs` / stub “digite o path” |
| `Editor/WindowChrome.cpp` (DWM, WndProc) | no-op: GLFW decorated=true |
| `LuaVM.cpp` `OutputDebugStringA` + log em `C:/TucanoEngine/...` | `fprintf(stderr)` + path relativo |
| CMake preset só `windows-release` (Ninja do VS) | preset `linux-vulkan` (Ninja + GCC/Clang) |
| `find_program(DXC REQUIRED)` em paths do Windows SDK | DXC Linux (Vulkan SDK ou release da DirectXShaderCompiler) |
| Nsight Aftermath | já é opcional; fica `#ifdef _WIN32` |

Root signature atual (espelhar no Vulkan, não reinventar):

```
0: 32× uint32 root constants          → push constants (128 bytes)
1: CBV b1                            → uniform / storage buffer dinâmico
2: CBV b2                            → idem
3: unbounded SRV t0 space0 + Texture3D space2  → set 0 sampled images (bindless)
4: sampler table s0 (8)              → set 1 samplers
5: SRV t0 space1 (structured, 16)    → set 2 storage buffers
```

Compute root: o DX12 tem `createComputeRootSignature()` separado — Vulkan usa o mesmo layout de sets, visibility ALL.

---

## Phase 0 — Fechar o contrato (1–2 semanas)

Nada de `vulkan.h` ainda. O objetivo é: **`TucanoRuntime` compila no Linux com o backend Null**, sem incluir DX12 fora de `src/RHI/DX12/`.

1. Switch de compile-time em `src/RHI/RHIBackend.h`:
   - `TUCANO_RHI_DX12` default `_WIN32`
   - `TUCANO_RHI_VULKAN` default Linux
   - `TUCANO_RHI_NULL` opt-in (CI headless, asset cook)
2. CMake: `TucanoRuntime` lista **ou** os `.cpp` de `RHI/DX12/` **ou** `RHI/Vulkan/` **ou** só Null. Nunca os três. `Device::create()` vive no `.cpp` do backend ativo (hoje está em `DX12Device.cpp` — mover o factory para `RHI/RHI.cpp` ou cada backend define o próprio).
3. Subir tabelas transientes para a RHI (substitui `writeSrvTable` de CPU handles):
   ```cpp
   virtual uint32_t writeTextureTable(std::span<Texture*> textures);
   virtual uint32_t writeBufferSrvTable(std::span<Buffer*> buffers);
   virtual uint32_t writeBufferUavTable(std::span<Buffer*> buffers);
   virtual uint32_t writeSamplerTable(std::span<Sampler*> samplers);
   ```
   Implementação DX12: copia os CPU handles que já existem. Renderer/World/Terrain passam a falar só `rhi::Texture*` / `rhi::Buffer*`.
4. `Screenshot.cpp` já tem `copyTextureToBuffer` na RHI — apagar os includes DX12.
5. `DebugUI.cpp`: isolar o backend ImGui atrás de `#if TUCANO_RHI_DX12` nesta fase (Vulkan chega na fase 7). No Null/Linux, DebugUI no-op ou só o contexto GLFW sem renderer.
6. Grep de saída: zero `#include "RHI/DX12` e zero `#include <d3d12` fora de `src/RHI/DX12/` e do bloco DX12 de `DebugUI`.

**Exit:** `cmake -D TUCANO_RHI=null` configura no Linux e `TucanoRuntime` + `TucanoECSTest` linkam. Windows DX12 **não muda de comportamento**. HelloTriangle no Windows continua DX12.

---

## Phase 1 — Host Linux, sem GPU de verdade (3–7 dias)

Depois da fase 0 o core já configura. Esta fase deixa o **processo** viver no Fedora.

- Preset `linux-vulkan` / `linux-null` em `CMakePresets.json` (Ninja, `CMAKE_BUILD_TYPE`, sem path do VS).
- `Window.cpp`: tirar `GLFW_EXPOSE_NATIVE_WIN32` do include global. `nativeHandle()` no Vulkan **não é HWND** — o swapchain recebe `window.handle()` (`GLFWwindow*`). DX12 Windows continua com `glfwGetWin32Window`.
- `imgui_lib`: no Linux não compilam `imgui_impl_dx12.cpp` nem linkam `d3d12`. GLFW backend fica.
- `SystemDialogs` / `WindowChrome`: stub POSIX. Editor abre, File>Open pode ser path na linha de comando até a fase 7.
- `LuaVM`: log para stderr; sem `C:/...`.
- Flags GCC/Clang (`-Wall`, sem `/W4` / `/MP`). GLM já está. Jolt, glfw, meshoptimizer, rpmalloc, lua, curl — todos têm CMake portátil; validar um a um (curl/OpenSSL do sistema).
- DXC Linux no CMake (`find_program(dxc)` + hint `$VULKAN_SDK/bin`). **Não baixar binário de terceiro sem OK explícito** — na máquina: Vulkan SDK ou `dnf`/tarball oficial da DirectXShaderCompiler.
- Target smoke: `TucanoECSTest` / `TucanoInputTest` (sem janela GPU) exit 0.

**Exit:** `cmake --preset=linux-null && cmake --build --preset=linux-null` no Fedora. Um teste host sai 0. Sem swapchain.

---

## Phase 2 — Triangle (1–2 semanas)

`src/RHI/Vulkan/VulkanDevice.cpp` (e irmãos: command list, swapchain) implementam **só** o caminho do HelloTriangle. O resto das APIs da RHI permanece stub (como o Null), até a fase 3.

1. Instance Vulkan 1.3, device, fila graphics, **dynamic rendering** (`VK_KHR_dynamic_rendering` core 1.3). Sem render passes.
2. `glfwCreateWindowSurface` + swapchain (`VK_KHR_swapchain`).
3. `beginFrame` / `endFrame` / present. Resize (`FramebufferSizeCallback` já existe).
4. `createGraphicsPipeline` VS+PS a partir de SPIR-V (`ShaderBytecode::loadFromFile` já é bytes crus — só mudar a extensão gerada pelo CMake).
5. `CmdDraw` / `drawIndexed`, viewport, scissor, clear, `setRenderTargets` → `vkCmdBeginRendering`.
6. `VK_EXT_debug_utils` + `VK_LAYER_KHRONOS_validation`. No callback: `memoryInitThreadHeap()` **antes** de qualquer log/alloc (rpmalloc no thread do loader).
7. Sample `HelloTriangle` inalterado na lógica. CMake Linux: `Triangle.hlsl` → `Triangle_VSMain.spv` / `Triangle_PSMain.spv`.

```
cmake --preset=linux-vulkan && cmake --build --preset=linux-vulkan
DISPLAY=:0 ./build/linux-vulkan/Samples/HelloTriangle/HelloTriangle --frames 90
```

**Exit:** janela 1280×720 (ou o size real do GLFW), triângulo, resize sem crash, **0 errors / 0 warnings** de validation no happy path. GPU alvo: AMD Radeon RX 6700 (RADV NAVI22) — a mesma da prova no Esoterica.

---

## Phase 3 — Bindless (2–3 semanas)

Este é o pedaço gráfico difícil. Os shaders **já** indexam `bindlessHeap[idx]` — não há SM 6.6 heap único para inventar.

- Heaps `UPDATE_AFTER_BIND` + `PARTIALLY_BOUND`:
  - set 0: sampled images (Texture2D + view Texture3D no mesmo índice, space2)
  - set 1: samplers
  - set 2: storage buffers (space1)
- Alocador: reusar `BindlessManager` (já é agnóstico — só está na pasta DX12 por acidente). Mover para `src/RHI/BindlessManager.{h,cpp}`.
- Capacidade: DX12 usa 8192 bindless + 8192 transientes. No Vulkan, clamar `maxDescriptorSetUpdateAfterBind*` (no RADV costuma dar folga; se não, clamp e logar).
- `CreateTexture` / `CreateBuffer` / `CreateSampler` escrevem o descritor no slot. `bindlessIndex()` / `srvIndex()` / `uavIndex()` iguais ao DX12.
- CBV root 1/2 → `vkCmdBindDescriptorSets` com dynamic offset, ou push descriptor. Não bindless.
- Push constants = root param 0 (32 dwords).
- `vkCmdPipelineBarrier2` em `transition` / `uavBarrier` / `flushBarriers`.
- Allocator: VMA (`GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator`, MIT, header-only).
- Smoke: HelloTriangle texturizado **ou** `Tools/RHITest` se ele já exercita bindless. Handles inteiros no root/CB, não descriptor set por draw.

**Exit:** criar texture/buffer/sampler; um triângulo (ou fullscreen) amostra `bindlessHeap[0]`; 0/0 validation. Tabelas transientes (`writeTextureTable`) funcionam para um compute UAV simples (o path que Vegetation/World usam).

Não usar `VK_EXT_mutable_descriptor_type` neste engine — os shaders são tipados. (No Esoterica isso era tentação por causa do `ResourceDescriptorHeap`; aqui não.)

---

## Phase 4 — Pipeline de shaders (1–2 semanas, pode paralelo à 3)

CMake `tucano_compile_shader`:

| Windows DX12 | Linux Vulkan |
|---|---|
| `dxc -T vs_6_0 -Fo *.cso` | `dxc -spirv -fspv-target-env=vulkan1.3 -T vs_6_0 -Fo *.spv` |
| mesh `as_6_5`/`ms_6_5` | **não compilar** até fase 8 |
| RT `cs_6_5` (RTShadows etc.) | **não compilar** até fase 8 |

Flags SPIR-V medidas (2026-08-17). O default do DXC (`set = space`, `binding = register`) colide `t0`/`s0`/`b0` no set 0 binding 0. O mapa explícito:

```
-fvk-use-dx-layout -fspv-preserve-interface -DTUCANO_SPIRV=1
-fvk-bind-register tN 0 N 0     # bindless Texture2D[]  → set 0
-fvk-bind-register sN 0 N 1     # samplers             → set 1
-fvk-bind-register tN 1 N 2     # structured space1    → set 2
-fvk-bind-register b0/b1/b2     # CBV                  → set 3 bindings 0/1/2
-fvk-bind-register uN 0 N 4     # RWTexture            → set 4
-fvk-bind-register t0 2 0 5     # Texture3D[]          → set 5
```

`register(b0)` **não** vira push constant (o DXC recusa `[[vk::push_constant]]` em `cbuffer`). O RHI duplica root constants: `vkCmdPushConstants` (HelloTriangle) **e** UBO set 3 binding 0 (GBuffer). `Triangle.hlsl` / `BindlessFill.hlsl` continuam com `[[vk::binding]]` no path `TUCANO_SPIRV`.

`ShaderBytecode::loadFromFile` troca `.cso` → `TUCANO_SHADER_EXT` no binário Vulkan.

**Exit:** todos os vs/ps/cs **não-RT e não-mesh** geram `.spv` e passam `spirv-val`. `HelloTriangle` carrega `.spv` no Linux e `.cso` no Windows.

---

## Phase 5 — Um frame deferred (2–4 semanas)

Não o Sponza inteiro. **PBRTest** (grid PBR) ou o GBuffer+Lighting+Tonemap do `Renderer.cpp` com o resto das passes desligadas (`m_settings`).

Implementar na RHI Vulkan o que esse recorte chama:

- MRT `setRenderTargets` (já no triângulo, agora 5 RTs + DSV)
- Depth, `clearDepth` / `clearRenderTarget`
- Vertex/index buffers, input layout **ou** bindless VB (GBuffer usa IA clássica)
- `setGraphicsRootCBV`, `setGraphicsRootConstants`, `setDescriptorHeap`, tables 3/4/5
- Compute: histogram/exposure se o tonemap depender; senão desligar Exposure
- Copies/upload de textura (materiais default)
- Barriers entre GBuffer → Lighting → Tonemap → Present

**Exit:** `PBRTest --frames 90` no Linux, GBuffer+Lighting+Tonemap, validation **0 errors**. Weather/meshlet compute PSOs ainda não entram no path (desligados no Vulkan até a fase 6). Windows DX12 PBRTest intacto.

---

## Phase 6 — Resto do renderer raster (3–6 semanas)

Tudo que `Renderer::render` e os sistemas ligados chamam, **exceto** mesh shaders e DXR:

Shadows (CSM/octa/VSM), AO, bloom, IBL, Bruneton, SSGI/SSR, rain, clouds, water, fog, terrain clipmap, vegetation (compute cull), world GPU cull, RenderGraph barriers/aliasing, async compute **se** o device Vulkan tiver fila compute dedicada — senão executar na graphics (o DX12 já faz isso em vários hints).

Indirect: `drawIndexedIndirect` / `drawIndexedIndirectCount` → `vkCmdDrawIndexedIndirectCount`.

`submitAndWaitHeadless` para HeightmapQuery / readback.

**Exit:** `SponzaViewer --frames 90` com mesh shaders **off** e RT **off**. Visual próximo do DX12 (não pixel-identical). Validation limpa no path default. Features que faltarem: desligar por `supports*()` / settings, não crashar.

**Status 2026-08-17 noite — raster default (SSR/contact/exposure) verde, sem GPUVM no gate.**

```
DISPLAY=:0 ./build/linux-vulkan/Samples/SponzaViewer/SponzaViewer --frames 90
# SponzaViewer OK (90 frames)
# [Vulkan] validation errors=0 warnings=0
# journalctl -k: sem page fault / GPU reset neste run
```

**Não rodar sem `--frames N`.** Um run interativo na tarde de 2026-08-17 GPUVM'ou (`0x80009c249000`, SQC, PERMISSION_FAULTS=3), MODE1-resetou a RX 6700 e derrubou o GNOME. Causas já fechadas no RHI:
- `uploadTexture` ignorava o mip; IBL prefiltered (maxMip=6) ficava UNDEFINED → `SampleLevel` ruidoso + GPUVM
- Barreiras só no mip 0; agora `VK_REMAINING_MIP_LEVELS` / `ARRAY_LAYERS`
- UAV+SRV (Hi-Z, probe atlas) nasciam em GENERAL com descritor READ_ONLY
- Phase3 ligava Hi-Z/probes mesmo com o pass off
- UBO: ring 2048 B + descriptor set novo (dummy `texIds=0` = tela verde)
- glTF Khronos: ECS gravava scale 1 em vez de 0.008; câmera `(-6, 1.8, 0)`

Cena: Khronos Sponza glTF em `Assets/Sponza/Sponza.gltf` (não está no git).

Verde no Vulkan:
- RHI: indirect, copies (mip + row pitch 256), `clearRenderTargetRect`, `submitAndWaitHeadless`, sampler table (não clobber no CBV)
- Raster: GBuffer, CSM, lighting+IBL+Bruneton, GTAO, bloom, **SSR, contact, auto-exposure**, tonemap. Rain fullscreen só gate curto; run de 30 s GPUVM'ou.
- ImGui Vulkan (`imgui_impl_vulkan`, pool próprio) — `TestEditor --frames 8` 0/0, HUD/painéis visíveis

Ainda off / próximo incremento da 6:
- Dummy 1×1 sampled: barrier VS+PS+CS (não só fragment). Release de Texture3D volta ao heap 3D, não ao 2D.
- Clouds/water/fog: HLSL bindless ok; clouds bloqueados até upload 3D ser exercitado com `--frames` **depois de reboot**
- Probe cube bake + convert CS (SSR usa IBL; atlas só entra depois de `m_probeMipsSeeded`)
- Meshlet/Hi-Z compute, octa/VSM, terrain clipmap, vegetation cull, world GPU cull
- Fila async compute dedicada

DX12 Windows intacto (`#if TUCANO_RHI_VULKAN` só no skip de clima/probe-bake/meshlet-CS e no log do viewer).

---

## Phase 7 — Editor no Linux (2–4 semanas)

- `imgui_impl_vulkan` no mesmo swapchain (ou no RT do viewport, como o DebugUI DX12 faz com heap próprio — no Vulkan, um descriptor pool ImGui separado, **não** misturar com o bindless 8192).
- `sceneTextureId`: SRV ImGui da textura do viewport (hoje cria view DX12 no heap de 64). Espelhar.
- `TestEditor` abre, docking, Hierarchy/Inspector, câmera, Play no-op ou o que já existir.
- File dialog POSIX mínimo.
- `WindowChrome` permanece Windows-only.

**Status 2026-08-18:** `imgui_impl_vulkan` verde (`TestEditor --frames 8` 0/0, journal limpo). File dialog POSIX: zenity/kdialog. Crash do swapchain (`owner` nulo) corrigido. Próximo: resize interactivo fica para sessão humana; file dialogs só com pessoa no File menu.

**Exit:** `TestEditor` janela, viewport 3D, resize, sem crash. DX12 Windows editor intacto.

---

## Phase 8 — Mesh shaders e DXR (opcional, 4–8 semanas, **depois** do editor)

Só aqui.

- Mesh: `VK_EXT_mesh_shader`, `dispatchMesh`, compile `MeshletMesh.hlsl` `as_6_5`/`ms_6_5` com `-fspv-target-env=vulkan1.3`. Se a extensão não existir, o renderer **já** tem o fallback VS — não bloquear o mundo.
- DXR: `VK_KHR_ray_query` (compute, mais perto dos `cs_6_5` atuais) **ou** `VK_KHR_ray_tracing_pipeline`. RX 6700 no RADV tem RT. Até lá, `supportsRaytracing() == false` e SSR/IBL continuam.

**Exit:** um meshlet via mesh shaders numa GPU com a extensão; Sponza sem mesh shaders continua igual à fase 6. RT é bônus, não gate.

---

## Calendário (foco, uma pessoa)

| Fase | Tempo | Soma |
|---|---|---|
| 0 contrato + vazar DX12 | 1–2 sem | 2 sem |
| 1 host Linux | ~1 sem | 3 sem |
| 2 triângulo | 1–2 sem | 5 sem |
| 3 bindless + VMA | 2–3 sem | 8 sem |
| 4 DXC SPIR-V | 1–2 sem (// 3) | 8–9 sem |
| 5 PBR deferred | 2–4 sem | 3 meses |
| 6 Sponza raster | 3–6 sem | 4–5 meses |
| 7 editor | 2–4 sem | **~4–6 meses** |
| 8 mesh + RT | depois | extra |

Mais curto que o Esoterica no host/editor (GLFW e ImGui já existem; mesh shaders não são obrigatórios). Mais longo na fase 6 porque o renderer do Tucano é maior (clima, GI, terreno).

---

## Riscos

| Risco | Por que escorrega | Mitigação |
|---|---|---|
| Tabelas transientes ≠ bindless | Rain/Vegetation/World ainda montam SRV tables no DX12 | Fase 0 sobe a API; fase 3 implementa; preferir migrar passes para `bindlessIndex()` quando for barato |
| DXC SPIR-V vs root layout | `b0` não vira push constant, spaces errados | Fase 4 trava em `Triangle.hlsl` + `GBuffer.hlsl` antes de 46 arquivos |
| `setRenderTargets` vs dynamic rendering | 5 MRTs + depth no GBuffer | Phase 2 já usa dynamic rendering; phase 5 só aumenta o count |
| Validation no callback + rpmalloc | Loader chama o callback noutro thread | `memoryInitThreadHeap()` no callback (já aprendido no Esoterica) |
| DXC Linux ausente | CMake `REQUIRED` quebra configure | Fase 1: DXC encontrado ou mensagem clara; não FetchContent de binário sem OK |
| Jolt / curl / lua no GCC | Flags MSVC escondidas, FP exceptions | Fase 1 smoke dos Tools sem GPU |
| ImGui heap vs bindless heap | DX12 já usa heap **separado** de 64 | Vulkan: pool ImGui próprio. Nunca `bindlessHeap[imguiSlot]` |
| Wayland vs X11 | `DISPLAY=:0` é X11; GLFW+Vulkan funciona nos dois via `glfwCreateWindowSurface` | Não usar Xlib direto. Testar X11 primeiro |
| Pixel-diff Sponza | Convenções Y-down, depth 0–1 (já `GLM_FORCE_DEPTH_ZERO_TO_ONE`) | Aceitar “reconhecível”; não golden image no gate |
| Aftermath / TDR | Só Windows | `GpuCrashRecovery` fica no backend DX12; Vulkan aborta em `VK_ERROR_DEVICE_LOST` |
| RADV GPUVM (SQC / PERMISSION_FAULTS) | Layout/descriptor errado não aparece na validation; o kernel page-faulta, o anel trava, MODE1 reset mata o GNOME | Só `--frames N`. `MESA_VK_ABORT_ON_DEVICE_LOSS=1`. Não relançar interativo depois de GPUVM. Conferir `journalctl -k` por page fault / GPU reset |

---

## Regras

1. Engine (`Renderer/`, `World/`, `Editor/` salvo o bloco ImGui) não inclui `d3d12.h` / `vulkan.h`.
2. Fase N só começa depois do exit da N−1.
3. Windows DX12 permanece o path de produção até o exit da fase 7.
4. Um backend por executável. Sem `if (dx12) else vulkan` no meio do GBuffer.
5. Não começar por mesh shaders, DXR, VSM ou DDGI. Triângulo → bindless → PBR → Sponza raster → editor.
6. Não misturar este trabalho com Dagor/Harpia/Esoterica no mesmo PR. Código novo em `src/RHI/Vulkan/` e CMake; inglês nos comentários, como o resto do Tucano.
7. **Não** lançar SponzaViewer Vulkan sem `--frames N` no desktop. GPUVM no RADV dá MODE1 reset e mata a sessão (parece reboot; o kernel continua no ar).

---

## Primeiro commit (quando começar)

1. `src/RHI/RHIBackend.h` com os três macros.
2. Mover `Device::create` para fora de `DX12Device.cpp` (factory no backend).
3. CMake: `TUCANO_RHI` cache string, pasta DX12 só se DX12.
4. `writeTextureTable` / `writeBufferSrvTable` na RHI, um arquivo de Renderer migrado como prova (`GBufferPass.cpp` é o candidato: quase só usa bindless + root constants).
5. Preset `linux-null`.

Sem `vulkan.h`, sem comportamento novo no Windows.
