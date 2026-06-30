//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include <mono/jit/details/jit-functions.h>
#include <mono/metadata/details/assembly-functions.h>
#include <mono/metadata/details/mono-config-functions.h>
#include <mono/metadata/details/mono-gc-functions.h>
#include <mono/metadata/details/mono-debug-functions.h>
#include <mono/utils/details/mono-publib-functions.h>
#include <mono/utils/details/mono-logger-functions.h>
#include <mono/metadata/details/threads-functions.h>
#include <mono/metadata/details/appdomain-functions.h>
#include <mono/metadata/details/object-functions.h>
#include <mono/metadata/details/metadata-functions.h>
#include <mono/metadata/details/image-functions.h>
#include <mono/metadata/details/loader-functions.h>
#include <mono/utils/details/mono-error-functions.h>
#include <mono/metadata/details/class-functions.h>
#include <mono/metadata/details/debug-helpers-functions.h>
#include <mono/metadata/details/reflection-functions.h>

// The cooperative-GC thread-state transition API (mono/utils/mono-threads-api.h) is exported by coreclr.dll but its
// declaration header isn't shipped with the dependency, so declare the entry points we need here.
MONO_API_FUNCTION(void*, mono_threads_enter_gc_unsafe_region, (void** stackData))
MONO_API_FUNCTION(void, mono_threads_exit_gc_unsafe_region, (void* cookie, void** stackData))
MONO_API_FUNCTION(void*, mono_threads_enter_gc_safe_region, (void** stackData))
MONO_API_FUNCTION(void, mono_threads_exit_gc_safe_region, (void* cookie, void** stackData))
