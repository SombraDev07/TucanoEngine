#include "Runtime/DebugUI.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Resource.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx12.h>

#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace tucano {
namespace {

void imguiAllocSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                   D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
  *outCpu = info->LegacySingleSrvCpuDescriptor;
  *outGpu = info->LegacySingleSrvGpuDescriptor;
}

void imguiFreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

} // namespace

void DebugUI::init(Window& window, rhi::Device& device) {
  auto& dx = static_cast<rhi::DX12Device&>(device);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplGlfw_InitForOther(window.handle(), true);

  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 64;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  static ComPtr<ID3D12DescriptorHeap> s_heap;
  if (FAILED(dx.device()->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&s_heap)))) {
    return;
  }
  m_srvHeap = s_heap.Get();

  ImGui_ImplDX12_InitInfo init{};
  init.Device = dx.device();
  init.CommandQueue = dx.queue();
  init.NumFramesInFlight = static_cast<int>(rhi::kMaxFramesInFlight);
  init.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  init.SrvDescriptorHeap = s_heap.Get();
  init.LegacySingleSrvCpuDescriptor = s_heap->GetCPUDescriptorHandleForHeapStart();
  init.LegacySingleSrvGpuDescriptor = s_heap->GetGPUDescriptorHandleForHeapStart();
  init.SrvDescriptorAllocFn = imguiAllocSrv;
  init.SrvDescriptorFreeFn = imguiFreeSrv;
  if (!ImGui_ImplDX12_Init(&init)) {
    return;
  }
  m_ready = true;
}

void DebugUI::shutdown() {
  if (!m_ready) {
    return;
  }
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  m_ready = false;
}

void DebugUI::beginFrame() {
  if (!m_ready) {
    return;
  }
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void DebugUI::endFrame(rhi::CommandList& cmd, rhi::Texture& renderTarget) {
  if (!m_ready) {
    return;
  }
  ImGui::Render();
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
}

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

void DebugUI::drawWeatherAndLights(RainParams& rain, Scene& scene, RendererSettings& settings) {
  if (!m_ready) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Tucano Tools")) {
    if (ImGui::CollapsingHeader("Rain (Cry-parity)", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enable rain", &rain.enabled);
      ImGui::Checkbox("SceneRain cones", &rain.enableSceneRain);
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
    }

    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Shadows", &settings.enableShadows);
      ImGui::Checkbox("IBL", &settings.enableIBL);
      ImGui::Checkbox("Bloom", &settings.enableBloom);
      ImGui::Checkbox("AO (GTAO)", &settings.enableAO);
      ImGui::Checkbox("Auto Exposure", &settings.enableAutoExposure);
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
      if (settings.enableAO) {
        ImGui::SliderFloat("AO Intensity", &settings.aoIntensity, 0.0f, 2.0f);
        ImGui::SliderFloat("AO Radius", &settings.aoRadius, 0.2f, 2.5f);
      }
      if (settings.enableBloom) {
        ImGui::SliderFloat("Bloom Strength", &settings.bloomStrength, 0.0f, 1.0f);
      }
      int gi = static_cast<int>(settings.giTier);
      if (ImGui::SliderInt("GI tier", &gi, 0, 3)) {
        settings.giTier = static_cast<GITier>(gi);
      }
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
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
      if (ImGui::Button("Add point light")) {
        scene.addPoint(scene.camera.position() + scene.camera.forward() * 2.0f, {1, 0.9f, 0.7f}, 15.0f, 10.0f);
      }
    }
  }
  ImGui::End();
}

} // namespace tucano
