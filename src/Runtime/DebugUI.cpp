#include "Runtime/DebugUI.h"
#include "Editor/UI/Fonts.h"
#include "Editor/UI/Style.h"
#include "Editor/ViewportInteraction.h"
#include "RHI/RHIBackend.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

#if TUCANO_RHI_DX12
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Resource.h"
#include <imgui_impl_dx12.h>
#include <d3d12.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;
#endif

#if TUCANO_RHI_VULKAN
#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanResources.h"
#include <imgui_impl_vulkan.h>
#endif

#include <cstdio>
#include <cstdlib>

namespace tucano {
#if TUCANO_RHI_DX12
namespace {

void imguiAllocSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                   D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
  *outCpu = info->LegacySingleSrvCpuDescriptor;
  *outGpu = info->LegacySingleSrvGpuDescriptor;
}

void imguiFreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

} // namespace
#endif

#if TUCANO_RHI_VULKAN
namespace {

VkFormat s_imguiColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

void imguiCheckVk(VkResult err) {
  if (err == VK_ERROR_DEVICE_LOST) {
    std::fprintf(stderr, "[Vulkan] ImGui VK_ERROR_DEVICE_LOST — aborting\n");
    std::fflush(stderr);
    std::abort();
  }
  if (err != VK_SUCCESS) {
    std::fprintf(stderr, "[Vulkan] ImGui VkResult %d\n", static_cast<int>(err));
  }
}

} // namespace
#endif

void DebugUI::init(Window& window, rhi::Device& device) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  editor::Style::apply();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  editor::buildFonts();
  ImGui_ImplGlfw_InitForOther(window.handle(), true);

#if TUCANO_RHI_DX12
  auto& dx = static_cast<rhi::DX12Device&>(device);

  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 64;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ID3D12DescriptorHeap* heap = nullptr;
  if (FAILED(dx.device()->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) {
    return;
  }
  m_srvHeap = heap;

  ImGui_ImplDX12_InitInfo init{};
  init.Device = dx.device();
  init.CommandQueue = dx.queue();
  init.NumFramesInFlight = static_cast<int>(rhi::kBackBufferCount);
  init.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  init.SrvDescriptorHeap = heap;
  init.LegacySingleSrvCpuDescriptor = heap->GetCPUDescriptorHandleForHeapStart();
  init.LegacySingleSrvGpuDescriptor = heap->GetGPUDescriptorHandleForHeapStart();
  init.SrvDescriptorAllocFn = imguiAllocSrv;
  init.SrvDescriptorFreeFn = imguiFreeSrv;
  if (!ImGui_ImplDX12_Init(&init)) {
    return;
  }
#elif TUCANO_RHI_VULKAN
  auto& vk = static_cast<rhi::VulkanDevice&>(device);
  ImGui_ImplVulkan_InitInfo init{};
  init.Instance = vk.instance();
  init.PhysicalDevice = vk.physicalDevice();
  init.Device = vk.device();
  init.QueueFamily = vk.graphicsQueueFamily();
  init.Queue = vk.graphicsQueue();
  init.MinImageCount = 2;
  init.ImageCount = rhi::kMaxFramesInFlight;
  init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init.DescriptorPoolSize = 64; // own pool — never the bindless 8192
  init.UseDynamicRendering = true;
  init.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  init.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  init.PipelineRenderingCreateInfo.pColorAttachmentFormats = &s_imguiColorFormat;
  init.MinAllocationSize = 1024 * 1024;
  init.CheckVkResultFn = imguiCheckVk;
  if (!ImGui_ImplVulkan_Init(&init)) {
    std::fprintf(stderr, "[DebugUI] ImGui_ImplVulkan_Init failed\n");
    return;
  }
  VkSamplerCreateInfo samp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samp.magFilter = VK_FILTER_LINEAR;
  samp.minFilter = VK_FILTER_LINEAR;
  samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samp.addressModeU = samp.addressModeV = samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samp.minLod = -1000.0f;
  samp.maxLod = 1000.0f;
  samp.maxAnisotropy = 1.0f;
  VkSampler sampler = VK_NULL_HANDLE;
  if (vkCreateSampler(vk.device(), &samp, nullptr, &sampler) == VK_SUCCESS) {
    m_sceneSampler = sampler;
    m_srvHeap = vk.device(); // VkDevice, for sampler destroy on shutdown
  }
#else
  (void)device;
  // Null / no GPU: build the atlas on the CPU so NewFrame does not crash.
  unsigned char* pixels = nullptr;
  int fontW = 0, fontH = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &fontW, &fontH);
  io.Fonts->SetTexID(static_cast<ImTextureID>(1));
#endif
  m_ready = true;
}

uint64_t DebugUI::sceneTextureId(rhi::Device& device, rhi::Texture& texture) {
#if TUCANO_RHI_DX12
  if (!m_ready || m_srvHeap == nullptr) return 0;

  auto& dx = static_cast<rhi::DX12Device&>(device);
  auto& dxTexture = static_cast<rhi::DX12Texture&>(texture);
  ID3D12Resource* resource = dxTexture.get();
  if (resource == nullptr) return 0;

  auto* heap = static_cast<ID3D12DescriptorHeap*>(m_srvHeap);
  const UINT stride =
      dx.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  const uint32_t slot = 1u + (m_sceneSlotCursor % rhi::kBackBufferCount);
  ++m_sceneSlotCursor;

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(slot) * stride;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
  gpu.ptr += static_cast<UINT64>(slot) * stride;

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = rhi::toDxgi(texture.format());
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  dx.device()->CreateShaderResourceView(resource, &srv, cpu);

  return static_cast<uint64_t>(gpu.ptr);
#elif TUCANO_RHI_VULKAN
  (void)device;
  if (!m_ready || m_sceneSampler == nullptr) {
    return 0;
  }
  auto& vkTex = static_cast<rhi::VulkanTexture&>(texture);
  const uint32_t slot = m_sceneSlotCursor % rhi::kMaxFramesInFlight;
  ++m_sceneSlotCursor;
  if (m_sceneSets[slot] != 0) {
    ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_sceneSets[slot]));
    m_sceneSets[slot] = 0;
  }
  const VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(m_sceneSampler), vkTex.view,
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_sceneSets[slot] = reinterpret_cast<uint64_t>(set);
  return m_sceneSets[slot];
#else
  (void)device;
  (void)texture;
  return 0;
#endif
}

void DebugUI::shutdown() {
  if (m_ready) {
#if TUCANO_RHI_DX12
    ImGui_ImplDX12_Shutdown();
#elif TUCANO_RHI_VULKAN
    for (uint64_t& set : m_sceneSets) {
      if (set != 0) {
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(set));
        set = 0;
      }
    }
    ImGui_ImplVulkan_Shutdown();
#endif
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_ready = false;
  }
#if TUCANO_RHI_DX12
  if (m_srvHeap) {
    static_cast<ID3D12DescriptorHeap*>(m_srvHeap)->Release();
    m_srvHeap = nullptr;
  }
#elif TUCANO_RHI_VULKAN
  if (m_sceneSampler && m_srvHeap) {
    vkDestroySampler(static_cast<VkDevice>(m_srvHeap), static_cast<VkSampler>(m_sceneSampler), nullptr);
  }
  m_sceneSampler = nullptr;
  m_srvHeap = nullptr;
#endif
}

void DebugUI::beginFrame() {
  if (!m_ready) {
    return;
  }
#if TUCANO_RHI_DX12
  ImGui_ImplDX12_NewFrame();
#elif TUCANO_RHI_VULKAN
  ImGui_ImplVulkan_NewFrame();
#endif
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();
}

void DebugUI::endFrame(rhi::CommandList& cmd, rhi::Texture& renderTarget) {
  if (!m_ready) {
    return;
  }
  ImGui::Render();
#if TUCANO_RHI_DX12
  auto& dxCmd = static_cast<rhi::DX12CommandList&>(cmd);
  auto& dxTex = static_cast<rhi::DX12Texture&>(renderTarget);
  cmd.transition(renderTarget, rhi::ResourceState::RenderTarget);
  cmd.flushBarriers();

  D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxTex.rtv;
  dxCmd.get()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  ID3D12DescriptorHeap* heap = static_cast<ID3D12DescriptorHeap*>(m_srvHeap);
  dxCmd.get()->SetDescriptorHeaps(1, &heap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCmd.get());
  cmd.setDescriptorHeap();
#elif TUCANO_RHI_VULKAN
  cmd.endRendering();
  cmd.transition(renderTarget, rhi::ResourceState::RenderTarget);
  auto& vkTex = static_cast<rhi::VulkanTexture&>(renderTarget);
  VkCommandBuffer vkcmd = vkTex.owner ? vkTex.owner->currentCommandBuffer() : VK_NULL_HANDLE;
  if (!vkcmd || !vkTex.view) {
    return;
  }
  VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = vkTex.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea.extent = {vkTex.w, vkTex.h};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &color;
  vkCmdBeginRendering(vkcmd, &ri);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkcmd);
  vkCmdEndRendering(vkcmd);
#else
  (void)cmd;
  (void)renderTarget;
#endif
}

bool DebugUI::drawTransformGizmo(const glm::mat4& view, const glm::mat4& proj, glm::mat4& model,
                                 GizmoOp op, bool worldSpace, float snap,
                                 uint32_t viewportWidth, uint32_t viewportHeight) {
  if (!m_ready || viewportWidth == 0 || viewportHeight == 0) return false;

  // Forwards to the editor's implementation rather than calling ImGuizmo again: two copies of the
  // same call is two places for the mode and snap rules to drift apart. This entry point is the
  // pre-viewport-panel one — the whole window is the viewport — so it draws to the background list
  // and its rect starts at the window origin.
  editor::GizmoOperation operation = editor::GizmoOperation::Translate;
  switch (op) {
    case GizmoOp::Rotate: operation = editor::GizmoOperation::Rotate; break;
    case GizmoOp::Scale: operation = editor::GizmoOperation::Scale; break;
    case GizmoOp::Translate: break;
  }
  return editor::manipulateTransform(
      view, proj, model, operation,
      worldSpace ? editor::GizmoSpace::World : editor::GizmoSpace::Local, snap, 0.0f, 0.0f,
      float(viewportWidth), float(viewportHeight), editor::GizmoLayer::BackgroundDrawList);
}

bool DebugUI::gizmoHovered() const { return m_ready && editor::gizmoIsOver(); }

bool DebugUI::wantCaptureMouse() const { return m_ready && ImGui::GetIO().WantCaptureMouse; }
bool DebugUI::wantCaptureKeyboard() const { return m_ready && ImGui::GetIO().WantCaptureKeyboard; }

void DebugUI::drawPerfHud(float frameMs, uint32_t drawCalls, uint32_t width, uint32_t height) {
  if (!m_ready) {
    return;
  }
  const float fps = 1000.0f / std::max(0.01f, frameMs);
  ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##PerfHud", nullptr, flags)) {
    ImGui::Text("FPS  %.1f", fps);
    ImGui::Text("ms   %.2f", frameMs);
    ImGui::Text("draws %u", drawCalls);
    ImGui::Text("%ux%u", width, height);
  }
  ImGui::End();
}

void DebugUI::drawWeatherAndLights(RainParams& rain, CloudParams& clouds, Scene& scene,
                                   RendererSettings& settings,
                                   bool lightsOwnedByEcs) {
  if (!m_ready) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Tucano Tools")) {
    if (ImGui::CollapsingHeader("Rain (Cry-parity)")) {
#if TUCANO_RHI_VULKAN
      rain.enabled = false;
      rain.enableSceneRain = false;
      ImGui::BeginDisabled();
#endif
      ImGui::Checkbox("Enable rain", &rain.enabled);
#if TUCANO_RHI_VULKAN
      ImGui::EndDisabled();
      ImGui::TextDisabled("Vulkan: rain gated (GPUVM on --seconds 30).");
      ImGui::BeginDisabled();
#endif
      if (rain.enabled && rain.amount < 0.05f) {
        rain.amount = 0.65f; // recover if a lab zeroed amount
      }
      ImGui::Checkbox("SceneRain cones", &rain.enableSceneRain);
      if (rain.enableSceneRain) {
        ImGui::TextDisabled("Note: cones are for enclosed scenes (Sponza); outdoors they look huge.");
      }
      ImGui::Checkbox("World splashes", &rain.enableWorldSplashes);
      ImGui::SliderFloat("Amount", &rain.amount, 0.0f, 1.5f);
      if (rain.enableSceneRain) {
        ImGui::SliderFloat("SceneRain intensity", &rain.sceneRainIntensity, 0.0f, 2.0f);
      }
      ImGui::SliderFloat("Streak intensity", &rain.streakIntensity, 0.0f, 2.0f);
      ImGui::SliderFloat("Streak speed", &rain.streakSpeed, 0.1f, 4.0f);
      ImGui::SliderFloat("Volume layers", &rain.streakLayers, 1.0f, 3.0f);
      ImGui::SliderFloat("Lens drops", &rain.rainDropsAmount, 0.0f, 1.5f);
      ImGui::SliderFloat("Drops speed", &rain.rainDropsSpeed, 0.1f, 3.0f);
      ImGui::SliderFloat("Drops lighting", &rain.rainDropsLighting, 0.0f, 2.0f);
      ImGui::SliderFloat("Wet darkening", &rain.diffuseDarkening, 0.0f, 1.0f);
      ImGui::SliderFloat("Puddles", &rain.puddlesAmount, 0.0f, 2.0f);
      ImGui::SliderFloat("Puddle SSR mirror", &rain.puddlesSSR, 0.0f, 2.0f);
      ImGui::SliderFloat("Splashes", &rain.splashesAmount, 0.0f, 2.0f);
      ImGui::SliderFloat("Gloss boost", &rain.glossBoost, 0.0f, 2.0f);
      ImGui::SliderFloat("Mist", &rain.mistAmount, 0.0f, 1.5f);
      ImGui::SliderFloat("Max view dist", &rain.maxViewDist, 10.0f, 120.0f);
      ImGui::ColorEdit3("Rain color", &rain.color.x);
      ImGui::SliderFloat3("Wind", &rain.wind.x, -1.0f, 1.0f);
      if (ImGui::Button("Preset: Light drizzle")) {
        rain.enabled = true;
        rain.amount = 0.35f;
        rain.streakIntensity = 0.25f;
        rain.rainDropsAmount = 0.15f;
        rain.puddlesAmount = 0.5f;
        rain.mistAmount = 0.15f;
      }
      ImGui::SameLine();
      if (ImGui::Button("Preset: Storm")) {
        rain.enabled = true;
        rain.amount = 1.2f;
        rain.streakIntensity = 1.1f;
        rain.rainDropsAmount = 0.9f;
        rain.puddlesAmount = 1.6f;
        rain.mistAmount = 0.7f;
        rain.streakSpeed = 2.0f;
      }
#if TUCANO_RHI_VULKAN
      ImGui::EndDisabled();
#endif
    }

    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Shadows", &settings.enableShadows);
      ImGui::Checkbox("IBL", &settings.enableIBL);
      ImGui::Checkbox("Bloom", &settings.postFx.enableBloom);
      ImGui::Checkbox("AO (GTAO)", &settings.postFx.enableAO);
      ImGui::Checkbox("Auto Exposure", &settings.postFx.enableAutoExposure);
      ImGui::Checkbox("Contact shadows", &settings.enableContactShadows);
      ImGui::Checkbox("SSR", &settings.enableSSR);
      ImGui::Checkbox("RT shadows (Ray Query)", &settings.enableRTShadows);
      ImGui::Checkbox("RT reflections (Ray Query)", &settings.enableRTReflections);
      ImGui::Checkbox("Toroidal CSM", &settings.enableToroidalShadows);
      ImGui::Checkbox("Octahedral point shadows", &settings.enableOctahedralPointShadows);
      ImGui::Checkbox("VSM (near cascade)", &settings.enableVSM);
      ImGui::Checkbox("ESM soft shadows", &settings.enableESM);
      ImGui::Checkbox("PCSS (CSM)", &settings.enablePCSS);
      if (settings.enablePCSS) {
        ImGui::SliderFloat("PCSS light size", &settings.pcssLightSize, 0.005f, 0.15f);
      }
      if (settings.enableESM) {
        ImGui::SliderFloat("ESM exponent", &settings.esmExponent, 20.0f, 200.0f);
      }
      if (settings.postFx.enableAO) {
        ImGui::SliderFloat("AO Intensity", &settings.postFx.aoIntensity, 0.0f, 2.0f);
        ImGui::SliderFloat("AO Radius", &settings.postFx.aoRadius, 0.2f, 2.5f);
      }
      if (settings.postFx.enableBloom) {
        ImGui::SliderFloat("Bloom Strength", &settings.postFx.bloomStrength, 0.0f, 1.0f);
      }
      int gi = static_cast<int>(settings.giTier);
      if (ImGui::SliderInt("GI tier", &gi, 0, 3)) {
        settings.giTier = static_cast<GITier>(gi);
      }
    }

    if (ImGui::CollapsingHeader("Atmosphere (FASE 5.2)", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enable atmosphere", &settings.sky.enableAtmosphere);
      ImGui::Checkbox("Bruneton LUTs (EGSR)", &settings.sky.useBrunetonAtmosphere);
      ImGui::Checkbox("Time of day drives sun", &settings.sky.atmosphereDrivesSun);
      ImGui::SliderFloat("Time of day", &settings.sky.timeOfDay, 0.0f, 1.0f);
      ImGui::TextDisabled("0=mid  0.25=rise  0.5=noon  0.75=set");
      ImGui::SliderFloat("Turbidity", &settings.sky.turbidity, 1.0f, 8.0f);
      ImGui::SliderFloat("Fog density", &settings.sky.fogDensity, 0.0f, 0.08f, "%.4f");
      ImGui::SliderFloat("Fog height", &settings.sky.fogHeight, 5.0f, 200.0f);
      ImGui::SliderFloat3("Wind", &settings.sky.wind.x, -2.0f, 2.0f);
      ImGui::Separator();
#if TUCANO_RHI_VULKAN
      clouds.enabled = false;
      ImGui::BeginDisabled();
#endif
      ImGui::Checkbox("Volumetric clouds", &clouds.enabled);
#if TUCANO_RHI_VULKAN
      ImGui::EndDisabled();
      ImGui::TextDisabled("Vulkan: clouds gated (3D upload untested).");
      ImGui::BeginDisabled();
#endif
      if (clouds.enabled) {
        ImGui::SliderFloat("Coverage", &clouds.coverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Density", &clouds.density, 0.1f, 3.0f);
        ImGui::SliderFloat("Altitude", &clouds.altitude, 200.0f, 4000.0f);
        ImGui::SliderFloat("Thickness", &clouds.thickness, 200.0f, 6000.0f);
        ImGui::SliderFloat("Storminess", &clouds.storminess, 0.0f, 1.0f);
        ImGui::Checkbox("Cloud shadows", &clouds.enableShadows);
        if (clouds.enableShadows) {
          ImGui::SliderFloat("Shadow strength", &clouds.shadowStrength, 0.0f, 1.0f);
        }
        ImGui::Checkbox("God rays", &clouds.enableGodRays);
        if (clouds.enableGodRays) {
          ImGui::SliderFloat("God ray strength", &clouds.godRayStrength, 0.0f, 1.5f);
        }
        ImGui::Checkbox("Clouds drive rain amount", &clouds.driveRain);
      }
      if (ImGui::Button("Preset: Clear noon")) {
        settings.sky.enableAtmosphere = true;
        settings.sky.atmosphereDrivesSun = true;
        settings.sky.timeOfDay = 0.5f;
        settings.sky.turbidity = 2.0f;
        settings.sky.fogDensity = 0.004f;
        settings.sky.fogHeight = 60.0f;
        clouds.enabled = true;
        clouds.coverage = 0.28f;
        clouds.density = 0.9f;
        clouds.altitude = 1800.0f;
        clouds.thickness = 1600.0f;
        clouds.storminess = 0.1f;
      }
      ImGui::SameLine();
      if (ImGui::Button("Preset: Golden hour")) {
        settings.sky.enableAtmosphere = true;
        settings.sky.atmosphereDrivesSun = true;
        settings.sky.timeOfDay = 0.78f;
        settings.sky.turbidity = 3.5f;
        settings.sky.fogDensity = 0.018f;
        settings.sky.fogHeight = 35.0f;
        clouds.enabled = true;
        clouds.coverage = 0.55f;
        clouds.density = 1.2f;
        clouds.altitude = 1300.0f;
        clouds.thickness = 2600.0f;
        clouds.storminess = 0.35f;
        clouds.enableGodRays = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Preset: Overcast")) {
        settings.sky.enableAtmosphere = true;
        settings.sky.atmosphereDrivesSun = true;
        settings.sky.timeOfDay = 0.42f;
        settings.sky.turbidity = 6.5f;
        settings.sky.fogDensity = 0.035f;
        settings.sky.fogHeight = 25.0f;
        clouds.enabled = true;
        clouds.coverage = 0.88f;
        clouds.density = 1.6f;
        clouds.altitude = 700.0f;
        clouds.thickness = 3200.0f;
        clouds.storminess = 0.85f;
        clouds.enableShadows = true;
      }
#if TUCANO_RHI_VULKAN
      ImGui::EndDisabled();
#endif
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (lightsOwnedByEcs) {
        ImGui::TextDisabled("Owned by the scene's entities — edit in the Outliner/Inspector.");
        ImGui::BeginDisabled();
      }
      for (size_t i = 0; i < scene.lights.size(); ++i) {
        auto& L = scene.lights[i];
        ImGui::PushID(static_cast<int>(i));
        const char* typeName = L.type == LightType::Directional ? "Directional"
                               : L.type == LightType::Spot      ? "Spot"
                                                                : "Point";
        if (ImGui::TreeNode(typeName)) {
          ImGui::ColorEdit3("Color", &L.color.x);
          ImGui::SliderFloat("Intensity", &L.intensity, 0.0f, 80.0f);
          if (L.type != LightType::Directional) {
            ImGui::DragFloat3("Position", &L.position.x, 0.05f);
            ImGui::SliderFloat("Range", &L.range, 0.5f, 80.0f);
          }
          if (L.type != LightType::Point) {
            ImGui::DragFloat3("Direction", &L.direction.x, 0.01f);
          }
          if (L.type == LightType::Spot) {
            ImGui::SliderFloat("Inner cone", &L.innerCone, 1.0f, 60.0f);
            ImGui::SliderFloat("Outer cone", &L.outerCone, 5.0f, 90.0f);
          }
          ImGui::Checkbox("Cast shadows", &L.castShadows);
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      if (lightsOwnedByEcs) {
        // No "Add" either: it would append to a list that is rebuilt from entities next frame, so
        // the light would appear for one frame and vanish. Adding a light entity is the Outliner's
        // job now.
        ImGui::EndDisabled();
      } else if (ImGui::Button("Add point light")) {
        scene.addPoint(scene.camera.position() + scene.camera.forward() * 2.0f, {1, 0.9f, 0.7f}, 15.0f, 10.0f);
      }
    }
  }
  ImGui::End();
}

} // namespace tucano
