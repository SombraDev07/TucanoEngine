# Reflector wiring (P3-03).
#
# Two targets, on purpose:
#
#   TucanoReflect       regenerates src/Generated/Reflection.g.{h,cpp}. Run by hand after
#                       annotating a type. Not part of ALL — a build must never rewrite files that
#                       are under version control behind the author's back.
#   TucanoReflectCheck  fails the build when the generated files no longer match the annotations.
#                       This is what makes the generated file trustworthy: it is committed, so it
#                       is what actually compiles, and this says so out loud when it goes stale.
#
# When the libclang Python package is missing, the reflector prints a note and exits 0, so the
# committed output is used and the build still works. That is the whole reason it is committed.

find_package(Python3 COMPONENTS Interpreter QUIET)

set(TUCANO_REFLECTOR_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/reflector.py)
set(TUCANO_REFLECTOR_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/manifest.txt)
set(TUCANO_REFLECTOR_OUT ${CMAKE_SOURCE_DIR}/src/Generated/Reflection.g.h)

# The compile arguments have to describe *this* machine's toolchain — clang needs the MSVC STL and
# Windows SDK include paths, and they carry version numbers. Generated at configure time from what
# CMake already resolved rather than hard-coded.
set(TUCANO_REFLECTOR_ARGS ${CMAKE_CURRENT_BINARY_DIR}/compile_args.txt)

# MSVC's own include dir sits next to the compiler CMake picked; the Windows SDK version is the one
# CMake resolved. Deriving both beats hard-coding a version number that is wrong on the next machine.
get_filename_component(TUCANO_MSVC_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(TUCANO_MSVC_ROOT "${TUCANO_MSVC_BIN}/../../.." ABSOLUTE)
set(TUCANO_MSVC_INCLUDE "${TUCANO_MSVC_ROOT}/include")
set(TUCANO_SDK_INCLUDE "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}")
string(REPLACE "\\" "/" TUCANO_SDK_INCLUDE "${TUCANO_SDK_INCLUDE}")
string(REPLACE "//" "/" TUCANO_SDK_INCLUDE "${TUCANO_SDK_INCLUDE}")
string(REGEX REPLACE "/$" "" TUCANO_SDK_INCLUDE "${TUCANO_SDK_INCLUDE}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/compile_args.txt.in ${TUCANO_REFLECTOR_ARGS} @ONLY)

if(Python3_Interpreter_FOUND)
  add_custom_target(TucanoReflect
    COMMAND ${Python3_EXECUTABLE} ${TUCANO_REFLECTOR_SCRIPT}
            --out ${TUCANO_REFLECTOR_OUT}
            --manifest ${TUCANO_REFLECTOR_MANIFEST}
            --src-root ${CMAKE_SOURCE_DIR}/src
            --compile-args ${TUCANO_REFLECTOR_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Reflector: regenerating src/Generated/Reflection.g.h"
    VERBATIM)

  add_custom_target(TucanoReflectCheck
    COMMAND ${Python3_EXECUTABLE} ${TUCANO_REFLECTOR_SCRIPT}
            --out ${TUCANO_REFLECTOR_OUT}
            --manifest ${TUCANO_REFLECTOR_MANIFEST}
            --src-root ${CMAKE_SOURCE_DIR}/src
            --compile-args ${TUCANO_REFLECTOR_ARGS}
            --check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Reflector: checking the generated reflection is current"
    VERBATIM)
else()
  message(STATUS "Reflector: no Python interpreter — using the committed src/Generated output.")
endif()
