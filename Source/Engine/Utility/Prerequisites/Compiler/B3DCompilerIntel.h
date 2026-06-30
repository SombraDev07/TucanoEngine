//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

// Intel compiler defines
// Note: B3D_THREADLOCAL is intentionally left to the per-platform header, since the Intel compiler spells it differently depending on the target platform.
#define B3D_COMPILER B3D_COMPILER_ID_INTEL
#define B3D_COMPILER_VERSION __INTEL_COMPILER
#define B3D_STDCALL __stdcall
#define B3D_CDECL __cdecl
#define B3D_FALLTHROUGH
#define _B3D_DISABLE_OPTIMIZATION __pragma(optimize("", off))
#define _B3D_ENABLE_OPTIMIZATION __pragma(optimize("", on))
#define B3D_LIKELY(x) (x)
#define B3D_UNLIKELY(x) (x)
#define B3D_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define B3D_PRETTY_FUNCTION_PREFIX '='
#define B3D_PRETTY_FUNCTION_SUFFIX ']'
