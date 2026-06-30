//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

// Build-system configuration
#include "B3DFrameworkConfig.h"

// Initial platform/compiler-related stuff to set.
#define B3D_PLATFORM_ID_WIN32 1
#define B3D_PLATFORM_ID_LINUX 2
#define B3D_PLATFORM_ID_MACOS 3
#define B3D_PLATFORM_ID_IOS 4
#define B3D_PLATFORM_ID_ANDROID 5
#define B3D_PLATFORM_ID_PS5 6

// Base compiler IDs. Proprietary platforms may introduce additional IDs
#define B3D_COMPILER_ID_MSVC 1
#define B3D_COMPILER_ID_GCC 2
#define B3D_COMPILER_ID_INTEL 3
#define B3D_COMPILER_ID_CLANG 4

#define B3D_ARCHITECTURE_ID_X86_32 1
#define B3D_ARCHITECTURE_ID_X86_64 2
#define B3D_ARCHITECTURE_ID_ARM64 3

#define B3D_ENDIAN_ID_LITTLE 1
#define B3D_ENDIAN_BIG 2
#define B3D_ENDIAN B3D_ENDIAN_ID_LITTLE

#define B3D_INLINE inline

// Boolean convenience macros for platforms
#define B3D_PLATFORM_WIN32 (B3D_PLATFORM == B3D_PLATFORM_ID_WIN32)
#define B3D_PLATFORM_LINUX (B3D_PLATFORM == B3D_PLATFORM_ID_LINUX)
#define B3D_PLATFORM_MACOS (B3D_PLATFORM == B3D_PLATFORM_ID_MACOS)
#define B3D_PLATFORM_IOS (B3D_PLATFORM == B3D_PLATFORM_ID_IOS)
#define B3D_PLATFORM_ANDROID (B3D_PLATFORM == B3D_PLATFORM_ID_ANDROID)
#define B3D_PLATFORM_PS5 (B3D_PLATFORM == B3D_PLATFORM_ID_PS5)

// Boolean convenience macros for compilers
#define B3D_COMPILER_MSVC (B3D_COMPILER == B3D_COMPILER_ID_MSVC)
#define B3D_COMPILER_GCC (B3D_COMPILER == B3D_COMPILER_ID_GCC)
#define B3D_COMPILER_INTEL (B3D_COMPILER == B3D_COMPILER_ID_INTEL)
#define B3D_COMPILER_CLANG (B3D_COMPILER == B3D_COMPILER_ID_CLANG)

// Find the architecture type
#if defined(__x86_64__) || defined(_M_X64)
#	define B3D_ARCHITECTURE B3D_ARCHITECTURE_ID_X86_64
#elif defined(__aarch64__)
#	define B3D_ARCHITECTURE B3D_ARCHITECTURE_ID_ARM64
#else
#	define B3D_ARCHITECTURE B3D_ARCHITECTURE_ID_X86_32
#endif

// Pull in the active platform's compiler + platform-specific defines
#define B3D_INTERNAL_STR(x) #x
#define B3D_INTERNAL_XSTR(x) B3D_INTERNAL_STR(x)
#include B3D_INTERNAL_XSTR(Prerequisites/B3D_PLATFORM_NAME/B3DPlatformDefines.h)
#undef B3D_INTERNAL_STR
#undef B3D_INTERNAL_XSTR

// Optimization control. Relies on the compiler-specific pragmas pulled in above.
#if !B3D_BUILD_TYPE_SHIPPING
#	define B3D_DISABLE_OPTIMIZATION _B3D_DISABLE_OPTIMIZATION
#	define B3D_ENABLE_OPTIMIZATION _B3D_ENABLE_OPTIMIZATION
#else
#	define B3D_DISABLE_OPTIMIZATION
#	define B3D_ENABLE_OPTIMIZATION
#endif

// iOS
#if B3D_PLATFORM_IOS
#	define B3D_BREAK() __builtin_trap()
#	define B3D_CODE_SECTION(name) __attribute__((section("__TEXT,__" name ",regular,pure_instructions"))) __attribute__((aligned(4)))
#endif

// Android
#if B3D_PLATFORM_ANDROID
#	define B3D_BREAK() raise(SIGTRAP)
#	define B3D_CODE_SECTION(name) __attribute__((section(name)))
#endif
