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

# tinyexr (BSD-3-Clause) — vendored. OpenEXR is what HDRI sites hand out alongside .hdr, and stb
# cannot decode it. Its ZIP backend is pointed at stb's zlib so this pulls in no new dependency:
# both stb implementations already live in src/Runtime/StbImpl.cpp.
add_library(tinyexr_headers INTERFACE)
target_include_directories(tinyexr_headers INTERFACE ${CMAKE_SOURCE_DIR}/third_party/tinyexr)
target_compile_definitions(tinyexr_headers INTERFACE TINYEXR_USE_MINIZ=0 TINYEXR_USE_STB_ZLIB=1)

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
set(TUCANO_IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo/ImGuizmo.cpp
)
if(TUCANO_RHI STREQUAL "dx12")
  list(APPEND TUCANO_IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
elseif(TUCANO_RHI STREQUAL "vulkan")
  list(APPEND TUCANO_IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
endif()
add_library(imgui_lib STATIC ${TUCANO_IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo)
if(TUCANO_RHI STREQUAL "dx12")
  target_link_libraries(imgui_lib PUBLIC glfw d3d12 dxgi)
elseif(TUCANO_RHI STREQUAL "vulkan")
  target_link_libraries(imgui_lib PUBLIC glfw Vulkan::Vulkan)
else()
  target_link_libraries(imgui_lib PUBLIC glfw)
endif()
# IMGUI_USE_WCHAR32 makes ImWchar 32-bit. The Material Design icon codepoints run past U+FFFF, and
# with the default 16-bit index they are unreachable — icons render as nothing, silently. This must
# stay PUBLIC: a mismatch between imgui_lib and its callers changes struct layouts.
target_compile_definitions(imgui_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_USE_WCHAR32)

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
# Jolt's optional Vulkan compute shaders need glslc; we do not use that path.
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
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

# ── miniaudio (header-only, MIT, single .h) ──────────

FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.21
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniaudio)
add_library(miniaudio_headers INTERFACE)
target_include_directories(miniaudio_headers INTERFACE ${miniaudio_SOURCE_DIR})

# ── imgui-node-editor (header-only) ──────────────────

FetchContent_Declare(imgui_node_editor
  GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
  GIT_TAG develop
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui_node_editor)
if(NOT imgui_node_editor_POPULATED)
  FetchContent_Populate(imgui_node_editor)
endif()
add_library(imgui_node_editor INTERFACE)
target_include_directories(imgui_node_editor INTERFACE ${imgui_node_editor_SOURCE_DIR})
target_link_libraries(imgui_node_editor INTERFACE imgui_lib)
# imgui_node_editor needs IMGUI_DEFINE_MATH_OPERATORS
target_compile_definitions(imgui_node_editor INTERFACE IMGUI_DEFINE_MATH_OPERATORS)

# ── Draco (Google mesh compression) ───────────────────

FetchContent_Declare(draco
  GIT_REPOSITORY https://github.com/google/draco.git
  GIT_TAG 1.5.7
  GIT_SHALLOW TRUE)
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
set(DRACO_POINT_CLOUD_COMPRESSION OFF CACHE BOOL "" FORCE)
set(DRACO_MESH_COMPRESSION_SUPPORTED ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(draco)

# ── Vulkan Memory Allocator (MIT, header-only) ────────
if(TUCANO_RHI STREQUAL "vulkan")
  FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.2.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(vma)
  add_library(vma_headers INTERFACE)
  target_include_directories(vma_headers INTERFACE ${vma_SOURCE_DIR}/include)
endif()

# ── OpenFBX (FBX importer) ────────────────────────────

FetchContent_Declare(openfbx
  GIT_REPOSITORY https://github.com/nem0/OpenFBX.git
  GIT_TAG master
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(openfbx)
if(NOT openfbx_POPULATED)
  FetchContent_Populate(openfbx)
endif()
add_library(openfbx STATIC ${openfbx_SOURCE_DIR}/src/ofbx.cpp ${openfbx_SOURCE_DIR}/src/libdeflate.c)
target_include_directories(openfbx PUBLIC ${openfbx_SOURCE_DIR}/src)
target_compile_definitions(openfbx PRIVATE OFBX_MESH_LOADING=1 OFBX_SKELETON_LOADING=1)

# ── ZSTD (fast real-time compression) ─────────────────

FetchContent_Declare(zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG v1.5.6
  GIT_SHALLOW TRUE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)
if(TARGET libzstd_static)
  target_include_directories(libzstd_static INTERFACE ${zstd_SOURCE_DIR}/lib)
endif()

# ── Lua 5.4 (MIT, scripting runtime) ──────────────────

FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
  FetchContent_Populate(lua)
endif()
add_library(lua STATIC
  ${lua_SOURCE_DIR}/lapi.c
  ${lua_SOURCE_DIR}/lauxlib.c
  ${lua_SOURCE_DIR}/lbaselib.c
  ${lua_SOURCE_DIR}/lcode.c
  ${lua_SOURCE_DIR}/lcorolib.c
  ${lua_SOURCE_DIR}/lctype.c
  ${lua_SOURCE_DIR}/ldblib.c
  ${lua_SOURCE_DIR}/ldebug.c
  ${lua_SOURCE_DIR}/ldo.c
  ${lua_SOURCE_DIR}/ldump.c
  ${lua_SOURCE_DIR}/lfunc.c
  ${lua_SOURCE_DIR}/lgc.c
  ${lua_SOURCE_DIR}/linit.c
  ${lua_SOURCE_DIR}/liolib.c
  ${lua_SOURCE_DIR}/llex.c
  ${lua_SOURCE_DIR}/lmathlib.c
  ${lua_SOURCE_DIR}/lmem.c
  ${lua_SOURCE_DIR}/loadlib.c
  ${lua_SOURCE_DIR}/lobject.c
  ${lua_SOURCE_DIR}/lopcodes.c
  ${lua_SOURCE_DIR}/loslib.c
  ${lua_SOURCE_DIR}/lparser.c
  ${lua_SOURCE_DIR}/lstate.c
  ${lua_SOURCE_DIR}/lstring.c
  ${lua_SOURCE_DIR}/lstrlib.c
  ${lua_SOURCE_DIR}/ltable.c
  ${lua_SOURCE_DIR}/ltablib.c
  ${lua_SOURCE_DIR}/ltm.c
  ${lua_SOURCE_DIR}/lundump.c
  ${lua_SOURCE_DIR}/lutf8lib.c
  ${lua_SOURCE_DIR}/lvm.c
  ${lua_SOURCE_DIR}/lzio.c
)
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
target_compile_definitions(lua PRIVATE LUA_COMPAT_5_3)
if(MSVC)
  target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ── libcurl (MIT, HTTP for AI agent) ──────────────────

FetchContent_Declare(rpmalloc
  GIT_REPOSITORY https://github.com/mjansson/rpmalloc.git
  GIT_TAG 1.4.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(rpmalloc)
add_library(rpmalloc STATIC ${rpmalloc_SOURCE_DIR}/rpmalloc/rpmalloc.c)
target_include_directories(rpmalloc PUBLIC ${rpmalloc_SOURCE_DIR}/rpmalloc)
target_compile_definitions(rpmalloc PRIVATE ENABLE_ASSERTS=0)

FetchContent_Declare(curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG curl-8_9_1
  GIT_SHALLOW TRUE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
else()
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_SSL ON CACHE BOOL "" FORCE)
endif()
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(curl)
if(NOT curl_POPULATED)
  FetchContent_Populate(curl)
  add_subdirectory(${curl_SOURCE_DIR} ${curl_BINARY_DIR})
endif()
