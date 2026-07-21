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
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw d3d12 dxgi)
target_compile_definitions(imgui_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# Tracy optional stub — keep header-only macros disabled by default
add_library(tracy_stub INTERFACE)
target_compile_definitions(tracy_stub INTERFACE TRACY_ENABLE=0)
