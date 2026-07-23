cmake_minimum_required(VERSION 3.28)
project(TucanoEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /MP /wd4324)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(cmake/Dependencies.cmake)

find_program(DXC_EXECUTABLE dxc
  HINTS
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.28000.0/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
  REQUIRED)

set(TUCANO_SHADER_DIR "${CMAKE_SOURCE_DIR}/Shaders")
set(TUCANO_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${TUCANO_SHADER_OUT}")

function(tucano_compile_shader SHADER_FILE ENTRY PROFILE)
  get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME_WE)
  set(OUT_FILE "${TUCANO_SHADER_OUT}/${SHADER_NAME}_${ENTRY}.cso")
  set(EXTRA_DEPS "")
  if(SHADER_FILE STREQUAL "DeferredLighting.hlsl" OR SHADER_FILE STREQUAL "Clouds.hlsl")
    list(APPEND EXTRA_DEPS "${TUCANO_SHADER_DIR}/Atmosphere.hlsl")
  endif()
  if(SHADER_FILE STREQUAL "DeferredLighting.hlsl")
    list(APPEND EXTRA_DEPS "${TUCANO_SHADER_DIR}/BrunetonAtmosphere.hlsl")
  endif()
  add_custom_command(
    OUTPUT "${OUT_FILE}"
    COMMAND "${DXC_EXECUTABLE}"
      -T ${PROFILE}
      -E ${ENTRY}
      -Fo "${OUT_FILE}"
      -I "${TUCANO_SHADER_DIR}"
      "${TUCANO_SHADER_DIR}/${SHADER_FILE}"
    DEPENDS "${TUCANO_SHADER_DIR}/${SHADER_FILE}" "${TUCANO_SHADER_DIR}/Common.hlsl" ${EXTRA_DEPS}
    COMMENT "DXC ${SHADER_FILE}:${ENTRY}"
    VERBATIM)
  list(APPEND TUCANO_COMPILED_SHADERS "${OUT_FILE}")
  set(TUCANO_COMPILED_SHADERS "${TUCANO_COMPILED_SHADERS}" PARENT_SCOPE)
endfunction()

set(TUCANO_COMPILED_SHADERS "")
tucano_compile_shader(Triangle.hlsl VSMain vs_6_0)
tucano_compile_shader(Triangle.hlsl PSMain ps_6_0)
tucano_compile_shader(GBuffer.hlsl VSMain vs_6_0)
tucano_compile_shader(GBuffer.hlsl PSMain ps_6_0)
tucano_compile_shader(Shadow.hlsl VSMain vs_6_0)
tucano_compile_shader(Shadow.hlsl PSMain ps_6_0)
tucano_compile_shader(ShadowOcta.hlsl VSMain vs_6_0)
tucano_compile_shader(ShadowOcta.hlsl PSMain ps_6_0)
tucano_compile_shader(DeferredLighting.hlsl VSMain vs_6_0)
tucano_compile_shader(DeferredLighting.hlsl PSMain ps_6_0)
tucano_compile_shader(PostFX.hlsl VSMain vs_6_0)
tucano_compile_shader(PostFX.hlsl PSTonemap ps_6_0)
tucano_compile_shader(PostFX.hlsl PSBloomDown ps_6_0)
tucano_compile_shader(PostFX.hlsl PSBloomUp ps_6_0)
tucano_compile_shader(PostFX.hlsl PSAO ps_6_0)
tucano_compile_shader(PostFX.hlsl PSGTAO ps_6_0)
tucano_compile_shader(PostFX.hlsl PSGTAOBlur ps_6_0)
tucano_compile_shader(Exposure.hlsl CSClearHistogram cs_6_0)
tucano_compile_shader(Exposure.hlsl CSBuildHistogram cs_6_0)
tucano_compile_shader(Exposure.hlsl CSReduceExposure cs_6_0)
tucano_compile_shader(Phase3.hlsl VSMain vs_6_0)
tucano_compile_shader(Phase3.hlsl PSSSGI ps_6_0)
tucano_compile_shader(Phase3.hlsl PSSSR ps_6_0)
tucano_compile_shader(Phase3.hlsl PSCompose ps_6_0)
tucano_compile_shader(Phase3.hlsl PSDDGI ps_6_0)
tucano_compile_shader(Phase3.hlsl PSVisAlbedo ps_6_0)
tucano_compile_shader(Phase3.hlsl PSVisNormal ps_6_0)
tucano_compile_shader(Phase3.hlsl PSVisORM ps_6_0)
tucano_compile_shader(Phase3.hlsl PSVisEmissive ps_6_0)
tucano_compile_shader(VisBuffer.hlsl VSMain vs_6_0)
tucano_compile_shader(VisBuffer.hlsl PSMain ps_6_0)
tucano_compile_shader(DDGIUpdate.hlsl CSMain cs_6_0)
tucano_compile_shader(ProbeCapture.hlsl VSMain vs_6_0)
tucano_compile_shader(ProbeCapture.hlsl PSMain ps_6_0)
tucano_compile_shader(ProbeCubeToLatLong.hlsl CSMain cs_6_0)
tucano_compile_shader(MeshletCull.hlsl CSMain cs_6_0)
tucano_compile_shader(MeshletCompact.hlsl CSMain cs_6_0)
tucano_compile_shader(HiZPyramid.hlsl CSCopy cs_6_0)
tucano_compile_shader(HiZPyramid.hlsl CSReduce cs_6_0)
tucano_compile_shader(SdfJfa.hlsl CSMain cs_6_0)
tucano_compile_shader(SdfFinalize.hlsl CSMain cs_6_0)
tucano_compile_shader(GBufferMeshlet.hlsl VSMain vs_6_0)
tucano_compile_shader(GBufferMeshlet.hlsl PSMain ps_6_0)
tucano_compile_shader(VisResolve.hlsl VSMain vs_6_0)
tucano_compile_shader(VisResolve.hlsl PSResolve ps_6_0)
tucano_compile_shader(MeshletMesh.hlsl ASMain as_6_5)
tucano_compile_shader(MeshletMesh.hlsl MSMain ms_6_5)
tucano_compile_shader(MeshletMesh.hlsl PSMain ps_6_5)
tucano_compile_shader(ContactShadows.hlsl VSMain vs_6_0)
tucano_compile_shader(ContactShadows.hlsl PSMain ps_6_0)
tucano_compile_shader(RTShadows.hlsl CSMain cs_6_5)
tucano_compile_shader(RTContact.hlsl CSMain cs_6_5)
tucano_compile_shader(RTReflections.hlsl CSMain cs_6_5)
tucano_compile_shader(Rain.hlsl VSMain vs_6_0)
tucano_compile_shader(Rain.hlsl VSSceneRain vs_6_0)
tucano_compile_shader(Rain.hlsl PSCopy ps_6_0)
tucano_compile_shader(Rain.hlsl PSOcclusion ps_6_0)
tucano_compile_shader(Rain.hlsl PSDeferredGBuffer ps_6_0)
tucano_compile_shader(Rain.hlsl PSRainComposite ps_6_0)
tucano_compile_shader(Rain.hlsl PSSceneRain ps_6_0)
tucano_compile_shader(Rain.hlsl PSDrops ps_6_0)
tucano_compile_shader(Rain.hlsl VSSplash vs_6_0)
tucano_compile_shader(Rain.hlsl PSSplash ps_6_0)
tucano_compile_shader(Rain.hlsl VSRainDrops vs_6_0)
tucano_compile_shader(Rain.hlsl PSRainDrops ps_6_0)
tucano_compile_shader(Rain.hlsl PSWetnessUpdate ps_6_0)
tucano_compile_shader(Clouds.hlsl VSMain vs_6_0)
tucano_compile_shader(Clouds.hlsl PSCloudMarch ps_6_0)
tucano_compile_shader(Clouds.hlsl PSCloudTemporal ps_6_0)
tucano_compile_shader(Clouds.hlsl PSWeatherMap ps_6_0)
tucano_compile_shader(Clouds.hlsl PSCloudComposite ps_6_0)

add_custom_target(TucanoShaders ALL DEPENDS ${TUCANO_COMPILED_SHADERS})

# Deploy selected test exes into repo TestPlay/ (clean sky lab + editor only).
set(TUCANO_TESTPLAY_DIR "${CMAKE_SOURCE_DIR}/TestPlay")
function(tucano_deploy_testplay TARGET_NAME)
  add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TUCANO_TESTPLAY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TUCANO_TESTPLAY_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${TARGET_NAME}>" "${TUCANO_TESTPLAY_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "${TUCANO_TESTPLAY_DIR}/shaders"
    COMMENT "Deploy ${TARGET_NAME} → TestPlay/"
    VERBATIM)
endfunction()

add_subdirectory(src)
add_subdirectory(Samples/HelloTriangle)
add_subdirectory(Samples/PBRTest)
add_subdirectory(Samples/SponzaViewer)
add_subdirectory(Samples/PhysicsDemo)
add_subdirectory(Samples/SponzaPhysics)
add_subdirectory(Samples/SkyLab)
add_subdirectory(Samples/TestEditor)
add_subdirectory(Tools/Benchmark)
add_subdirectory(Tools/ECSTest)
add_subdirectory(Tools/InputTest)
add_subdirectory(Tools/AnimationTest)

add_dependencies(TucanoRuntime TucanoShaders)
add_dependencies(HelloTriangle TucanoShaders)
add_dependencies(PBRTest TucanoShaders)
add_dependencies(SponzaViewer TucanoShaders)
add_dependencies(PhysicsDemo TucanoShaders)
add_dependencies(SponzaPhysics TucanoShaders)
add_dependencies(SkyLab TucanoShaders)
add_dependencies(TestEditor TucanoShaders)
add_dependencies(TucanoBenchmark TucanoShaders)
