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
  add_custom_command(
    OUTPUT "${OUT_FILE}"
    COMMAND "${DXC_EXECUTABLE}"
      -T ${PROFILE}
      -E ${ENTRY}
      -Fo "${OUT_FILE}"
      -I "${TUCANO_SHADER_DIR}"
      "${TUCANO_SHADER_DIR}/${SHADER_FILE}"
    DEPENDS "${TUCANO_SHADER_DIR}/${SHADER_FILE}" "${TUCANO_SHADER_DIR}/Common.hlsl"
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
tucano_compile_shader(DeferredLighting.hlsl VSMain vs_6_0)
tucano_compile_shader(DeferredLighting.hlsl PSMain ps_6_0)
tucano_compile_shader(PostFX.hlsl VSMain vs_6_0)
tucano_compile_shader(PostFX.hlsl PSTonemap ps_6_0)
tucano_compile_shader(PostFX.hlsl PSBloomDown ps_6_0)
tucano_compile_shader(PostFX.hlsl PSBloomUp ps_6_0)
tucano_compile_shader(PostFX.hlsl PSAO ps_6_0)
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
tucano_compile_shader(ContactShadows.hlsl VSMain vs_6_0)
tucano_compile_shader(ContactShadows.hlsl PSMain ps_6_0)
tucano_compile_shader(Rain.hlsl VSMain vs_6_0)
tucano_compile_shader(Rain.hlsl VSSceneRain vs_6_0)
tucano_compile_shader(Rain.hlsl PSCopy ps_6_0)
tucano_compile_shader(Rain.hlsl PSOcclusion ps_6_0)
tucano_compile_shader(Rain.hlsl PSDeferredGBuffer ps_6_0)
tucano_compile_shader(Rain.hlsl PSStreaks ps_6_0)
tucano_compile_shader(Rain.hlsl PSPuddleSpec ps_6_0)
tucano_compile_shader(Rain.hlsl PSSceneRain ps_6_0)
tucano_compile_shader(Rain.hlsl PSMist ps_6_0)
tucano_compile_shader(Rain.hlsl PSDrops ps_6_0)

add_custom_target(TucanoShaders ALL DEPENDS ${TUCANO_COMPILED_SHADERS})

add_subdirectory(src)
add_subdirectory(Samples/HelloTriangle)
add_subdirectory(Samples/PBRTest)
add_subdirectory(Samples/SponzaViewer)
add_subdirectory(Tools/Benchmark)

add_dependencies(TucanoRuntime TucanoShaders)
add_dependencies(HelloTriangle TucanoShaders)
add_dependencies(PBRTest TucanoShaders)
add_dependencies(SponzaViewer TucanoShaders)
add_dependencies(TucanoBenchmark TucanoShaders)
