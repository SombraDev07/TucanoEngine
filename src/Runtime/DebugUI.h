#pragma once

#include "Renderer/Weather/CloudSystem.h"
#include "Renderer/Weather/RainParams.h"
#include "Renderer/Scene.h"
#include "Renderer/Renderer.h"
#include "RHI/RHI.h"
#include "Platform/Window.h"

#include <memory>

struct GLFWwindow;

namespace tucano {

class DebugUI {
public:
  void init(Window& window, rhi::Device& device);
  void shutdown();
  void beginFrame();
  void endFrame(rhi::CommandList& cmd, rhi::Texture& renderTarget);

  // `lightsOwnedByEcs` tells this panel that something rebuilds `scene.lights` from entities every
  // frame (ecs::syncLightsToScene). It then shows them read-only and points at the Inspector,
  // because editing here would be undone before the next frame and the widget would just lie.
  // Samples with no ECS world leave it false and keep the editor they have always had.
  // `clouds` is passed separately because `CloudParams` is the sole owner of the cloud layer since
  // E-05 — it used to be duplicated inside `RendererSettings`, and the copy that this panel edited
  // was the one that won, which is why the editor's Clouds panel appeared to do nothing.
  void drawWeatherAndLights(RainParams& rain, CloudParams& clouds, Scene& scene,
                            RendererSettings& settings, bool lightsOwnedByEcs = false);
  void drawPerfHud(float frameMs, uint32_t drawCalls, uint32_t width, uint32_t height);
  bool wantCaptureMouse() const;
  bool wantCaptureKeyboard() const;

  // ── Transform gizmo (ImGuizmo) ──
  enum class GizmoOp { Translate, Rotate, Scale };

  // Draws the manipulator for `model` (world matrix, mutated in place). `snap` <= 0 disables
  // snapping. Returns true while the user is dragging a handle, so callers can suppress their own
  // picking/drag for that frame.
  bool drawTransformGizmo(const glm::mat4& view, const glm::mat4& proj, glm::mat4& model,
                          GizmoOp op, bool worldSpace, float snap,
                          uint32_t viewportWidth, uint32_t viewportHeight);

  // True while the cursor is over a gizmo handle (even without dragging).
  bool gizmoHovered() const;

  // ── Scene as a texture ──
  //
  // Returns an ImTextureID (cast it) for `texture`, so a panel can draw the rendered scene with
  // ImGui::Image. Needed because the editor cannot show the world *behind* its panels: the tool
  // window is docked into the shell's central node, and an occupied dock node paints over whatever
  // was drawn before ImGui. Every real editor solves this by rendering to a texture and showing it
  // in a viewport panel — which is also what a gizmo and click-picking need, since both work in
  // panel-relative coordinates.
  //
  // The descriptor comes from this class's own shader-visible heap: that is the heap ImGui binds
  // when it draws, and the engine's bindless heap is a different one whose indices mean nothing
  // here. One slot per frame in flight, rotated, because overwriting a descriptor the GPU may still
  // be reading from an in-flight frame is undefined.
  //
  // Returns 0 when the UI never initialised. Call once per frame, before endFrame().
  uint64_t sceneTextureId(rhi::Device& device, rhi::Texture& texture);

private:
  bool m_ready = false;
  void* m_srvHeap = nullptr; // ID3D12DescriptorHeap* (DX12) or unused (Vulkan)
  void* m_sceneSampler = nullptr; // VkSampler for ImGui::Image of the viewport
  uint64_t m_fence = 0;
  // Slot 0 of the heap is the font atlas; the scene rotates through the ones after it.
  uint32_t m_sceneSlotCursor = 0;
  uint64_t m_sceneSets[4]{}; // VkDescriptorSet, one per in-flight frame
};

} // namespace tucano
