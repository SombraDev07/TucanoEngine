include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(stb)
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE ${stb_SOURCE_DIR})

FetchContent_Declare(cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG v1.14
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(cgltf)
add_library(cgltf_headers INTERFACE)
target_include_directories(cgltf_headers INTERFACE ${cgltf_SOURCE_DIR})

FetchContent_Declare(meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG v0.22
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(meshoptimizer)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.8-docking
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
  # ImGuizmo (MIT) — vendored, needs imgui_internal.h so it builds alongside imgui itself.
  ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo)
target_link_libraries(imgui_lib PUBLIC glfw d3d12 dxgi)
target_compile_definitions(imgui_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# Tracy optional stub — keep header-only macros disabled by default
add_library(tracy_stub INTERFACE)
target_compile_definitions(tracy_stub INTERFACE TRACY_ENABLE=0)

FetchContent_Declare(joltphysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.6.0
  GIT_SHALLOW TRUE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
# Jolt traps hardware FP exceptions in Debug/Release by default; some SIMD solver paths touch
# uninitialized lanes and trip an invalid-op fault (crash) once contact islands form. Off for
# stable runtime (matches Jolt's Distribution config).
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(joltphysics)
if(NOT joltphysics_POPULATED)
  FetchContent_Populate(joltphysics)
  add_subdirectory(${joltphysics_SOURCE_DIR}/Build ${joltphysics_BINARY_DIR})
endif()
if(TARGET Jolt)
  set_target_properties(Jolt PROPERTIES FOLDER "third_party")
  set(TUCANO_HAS_JOLT ON CACHE BOOL "Jolt Physics available" FORCE)
else()
  message(WARNING "Tucano: Jolt target not found — physics deferred")
  set(TUCANO_HAS_JOLT OFF CACHE BOOL "Jolt Physics available" FORCE)
endif()

# --- 3thirdy: Nsight Aftermath (optional, NVIDIA) ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(NsightAftermath QUIET)
if(NsightAftermath_FOUND)
  message(STATUS "Tucano: Nsight Aftermath ENABLED (${NsightAftermath_INCLUDE_DIR})")
  set(TUCANO_HAS_AFTERMATH ON CACHE BOOL "Nsight Aftermath linked" FORCE)
  function(tucano_copy_aftermath_dll TARGET_NAME)
    if(NsightAftermath_DLL)
      add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${NsightAftermath_DLL}"
          "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMENT "Copy Aftermath DLL → ${TARGET_NAME}"
        VERBATIM)
    endif()
  endfunction()
else()
  message(STATUS "Tucano: Nsight Aftermath not found — drop SDK in 3thirdy/nsight-aftermath (see README)")
  set(TUCANO_HAS_AFTERMATH OFF CACHE BOOL "Nsight Aftermath linked" FORCE)
  function(tucano_copy_aftermath_dll TARGET_NAME)
  endfunction()
endif()
