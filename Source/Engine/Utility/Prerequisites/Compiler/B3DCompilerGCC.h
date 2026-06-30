//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

// GCC compiler defines
#define B3D_COMPILER B3D_COMPILER_ID_GCC
#define B3D_COMPILER_VERSION (((__GNUC__)*100) + (__GNUC_MINOR__ * 10) + __GNUC_PATCHLEVEL__)
#define B3D_THREADLOCAL __thread
#define B3D_STDCALL __attribute__((stdcall))
#define B3D_CDECL __attribute__((cdecl))
#define B3D_FALLTHROUGH __attribute__((fallthrough));
#define B3D_FORCENOINLINE __attribute__((noinline))
#define B3D_FORCEINLINE inline __attribute__((always_inline))
#define B3D_LIKELY(x) __builtin_expect(!!(x), 1)
#define B3D_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define _B3D_DISABLE_OPTIMIZATION _Pragma("GCC optimize \"O0\"")
#define _B3D_ENABLE_OPTIMIZATION _Pragma("GCC reset_options")
#define B3D_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define B3D_PRETTY_FUNCTION_PREFIX '='
#define B3D_PRETTY_FUNCTION_SUFFIX ']'
