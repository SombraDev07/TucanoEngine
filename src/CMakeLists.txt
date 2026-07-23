add_library(TucanoRuntime STATIC
  Platform/Window.cpp
  Platform/Input.cpp
  Platform/FileSystem.cpp
  RHI/DX12/DX12Common.cpp
  RHI/DX12/DX12Device.cpp
  RHI/DX12/DX12CommandList.cpp
  RHI/DX12/DX12SwapChain.cpp
  RHI/DX12/DX12DescriptorHeap.cpp
  RHI/DX12/DX12Pipeline.cpp
  RHI/DX12/DX12Upload.cpp
  RHI/DX12/BarrierBatcher.cpp
  RHI/DX12/BindlessManager.cpp
  RHI/DX12/PipelineCache.cpp
  RHI/DX12/GpuCrashRecovery.cpp
  RHI/DX12/SplitTransitionTracker.cpp
  RHI/DX12/DX12ReadBack.cpp
  Renderer/Camera.cpp
  Renderer/Mesh.cpp
  Renderer/DevTexture.cpp
  Renderer/Material.cpp
  Renderer/Texture.cpp
  Renderer/Scene.cpp
  Renderer/Deferred/GBufferPass.cpp
  Renderer/Deferred/LightingPass.cpp
  Renderer/Deferred/ShadowPass.cpp
  Renderer/PostFX/TonemapPass.cpp
  Renderer/PostFX/BloomPass.cpp
  Renderer/PostFX/AOPass.cpp
  Renderer/PostFX/ExposurePass.cpp
  Renderer/GI/IBL.cpp
  Renderer/GI/BrunetonAtmosphere.cpp
  Renderer/GI/VoxelGI.cpp
  Renderer/GI/WorldSDF.cpp
  Renderer/GI/ReflectionProbes.cpp
  Renderer/RayTracing/RayTracingScene.cpp
  Renderer/Shadows/ToroidalShadows.cpp
  Renderer/Shadows/OctahedralShadows.cpp
  Renderer/Shadows/VirtualShadowMaps.cpp
  Renderer/Weather/RainSystem.cpp
  Renderer/Weather/CloudSystem.cpp
  Renderer/Weather/CloudNoise.cpp
  Renderer/RenderGraph/RenderGraph.cpp
  Renderer/Renderer.cpp
  AssetPipeline/GLTFLoader.cpp
  AssetPipeline/ImageLoader.cpp
  AssetPipeline/DdsLoader.cpp
  AssetPipeline/AssetCooker.cpp
  AssetPipeline/ResourceFactory.cpp
  AssetPipeline/AssetPack.cpp
  Runtime/Screenshot.cpp
  Runtime/DebugUI.cpp
  Runtime/StbImpl.cpp
  Physics/PhysicsWorld.cpp
  Core/JobSystem.cpp
  Core/Json.cpp
  ECS/ComponentTypes.cpp
  ECS/Components.cpp
  ECS/EntityManager.cpp
  ECS/QueryManager.cpp
  ECS/EventManager.cpp
  ECS/TemplateManager.cpp
  ECS/World.cpp
  ECS/PhysicsSync.cpp
)

target_include_directories(TucanoRuntime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(TucanoRuntime
  PUBLIC
    glfw
    glm::glm
    stb_headers
    cgltf_headers
    meshoptimizer
    imgui_lib
    tracy_stub
    Jolt
    d3d12
    dxgi
    dxguid
    d3dcompiler
)

if(TUCANO_HAS_AFTERMATH)
  target_compile_definitions(TucanoRuntime PUBLIC TUCANO_AFTERMATH=1)
  target_link_libraries(TucanoRuntime PUBLIC NsightAftermath::NsightAftermath)
endif()

target_compile_definitions(TucanoRuntime
  PUBLIC
    TUCANO_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    TUCANO_ENGINE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/EngineAssets"
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_LEFT_HANDED
)

# ── Editor API DLL (C ABI for .NET P/Invoke) ──────────

add_library(TucanoEditorAPI SHARED
  EditorAPI/TucanoAPI.cpp
)

target_include_directories(TucanoEditorAPI
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/Samples"
)

target_link_libraries(TucanoEditorAPI
  PUBLIC
    TucanoRuntime
)

target_compile_definitions(TucanoEditorAPI
  PRIVATE
    TUCANO_API_EXPORTS
)

# Copy DLL to TestPlay for .NET editor
add_custom_command(TARGET TucanoEditorAPI POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:TucanoEditorAPI>"
    "${TUCANO_TESTPLAY_DIR}/"
  COMMENT "Deploy TucanoEditorAPI.dll → TestPlay/"
  VERBATIM)
