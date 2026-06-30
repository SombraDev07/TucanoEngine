//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

// MSVC compiler defines
#define B3D_COMPILER B3D_COMPILER_ID_MSVC
#define B3D_COMPILER_VERSION _MSC_VER
#define B3D_THREADLOCAL __declspec(thread)
#define B3D_STDCALL __stdcall
#define B3D_CDECL __cdecl
#define B3D_FALLTHROUGH
#define B3D_FORCENOINLINE __declspec(noinline)
#define B3D_FORCEINLINE __forceinline
#define B3D_LIKELY(x) (x)
#define B3D_UNLIKELY(x) (x)
#define _B3D_DISABLE_OPTIMIZATION __pragma(optimize("", off))
#define _B3D_ENABLE_OPTIMIZATION __pragma(optimize("", on))
#undef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ __FUNCSIG__
#define B3D_PRETTY_FUNCTION __FUNCSIG__
#define B3D_PRETTY_FUNCTION_PREFIX '<'
#define B3D_PRETTY_FUNCTION_SUFFIX '<'
