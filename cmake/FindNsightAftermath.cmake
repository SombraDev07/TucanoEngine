# FindNsightAftermath.cmake — looks in 3thirdy/ + NSIGHT_AFTERMATH_SDK env
#
# Sets:
#   NsightAftermath_FOUND
#   NsightAftermath_INCLUDE_DIRS
#   NsightAftermath_LIBRARIES
#   NsightAftermath_DLL (Windows, optional)

set(_AFTERMATH_ROOTS "")
if(DEFINED ENV{NSIGHT_AFTERMATH_SDK} AND NOT "$ENV{NSIGHT_AFTERMATH_SDK}" STREQUAL "")
  list(APPEND _AFTERMATH_ROOTS "$ENV{NSIGHT_AFTERMATH_SDK}")
endif()
list(APPEND _AFTERMATH_ROOTS
  "${CMAKE_SOURCE_DIR}/3thirdy/nsight-aftermath"
  "${CMAKE_SOURCE_DIR}/3thirdy/NsightAftermathSDK"
  "${CMAKE_SOURCE_DIR}/3thirdy/nsight-aftermath/SDK"
)

# Also scan one versioned child under 3thirdy/nsight-aftermath/*
file(GLOB _AFTERMATH_VERSION_DIRS "${CMAKE_SOURCE_DIR}/3thirdy/nsight-aftermath/*")
foreach(_d IN LISTS _AFTERMATH_VERSION_DIRS)
  if(IS_DIRECTORY "${_d}" AND EXISTS "${_d}/include/GFSDK_Aftermath.h")
    list(APPEND _AFTERMATH_ROOTS "${_d}")
  endif()
endforeach()

find_path(NsightAftermath_INCLUDE_DIR
  NAMES GFSDK_Aftermath.h
  PATHS ${_AFTERMATH_ROOTS}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_AF_LIB_NAMES GFSDK_Aftermath_Lib.x64 GFSDK_AFTERMATH_Lib.x64)
  set(_AF_DLL_NAMES GFSDK_Aftermath_Lib.x64.dll GFSDK_AFTERMATH_Lib.x64.dll)
  set(_AF_LIB_SUFFIXES lib/x64 lib)
else()
  set(_AF_LIB_NAMES GFSDK_Aftermath_Lib.x86 GFSDK_AFTERMATH_Lib.x86)
  set(_AF_DLL_NAMES GFSDK_Aftermath_Lib.x86.dll GFSDK_AFTERMATH_Lib.x86.dll)
  set(_AF_LIB_SUFFIXES lib/x86 lib)
endif()

find_library(NsightAftermath_LIBRARY
  NAMES ${_AF_LIB_NAMES}
  PATHS ${_AFTERMATH_ROOTS}
  PATH_SUFFIXES ${_AF_LIB_SUFFIXES}
  NO_DEFAULT_PATH)

find_file(NsightAftermath_DLL
  NAMES ${_AF_DLL_NAMES}
  PATHS ${_AFTERMATH_ROOTS}
  PATH_SUFFIXES ${_AF_LIB_SUFFIXES}
  NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NsightAftermath
  REQUIRED_VARS NsightAftermath_INCLUDE_DIR NsightAftermath_LIBRARY)

if(NsightAftermath_FOUND)
  set(NsightAftermath_INCLUDE_DIRS "${NsightAftermath_INCLUDE_DIR}")
  set(NsightAftermath_LIBRARIES "${NsightAftermath_LIBRARY}")
  if(NOT TARGET NsightAftermath::NsightAftermath)
    add_library(NsightAftermath::NsightAftermath UNKNOWN IMPORTED)
    set_target_properties(NsightAftermath::NsightAftermath PROPERTIES
      IMPORTED_LOCATION "${NsightAftermath_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${NsightAftermath_INCLUDE_DIR}")
  endif()
endif()

mark_as_advanced(NsightAftermath_INCLUDE_DIR NsightAftermath_LIBRARY NsightAftermath_DLL)
