//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DMonoPrerequisites.h"
#include "B3DMonoLoader.h"

namespace b3d
{
	/** @addtogroup Mono
	 *  @{
	 */

	/**
	 * RAII scope that places the calling thread in the cooperative GC "unsafe" (managed-running) state for its lifetime,
	 * restoring the previous state on destruction.
	 *
	 * Required whenever native code touches managed state - reads/writes managed objects, dereferences a raw pointer into
	 * the managed heap (e.g. MonoUtil::Unbox, ScriptArray::GetRaw, mono_string_chars), or otherwise assumes the GC will
	 * not run - from a context that is NOT already GC-unsafe. Code reached from an internal call (icall) is already
	 * GC-unsafe and does not need this; code reached from the native main loop / P-Invoke is GC-safe and does.
	 *
	 * IMPORTANT: this only guarantees the GC will not run *asynchronously* (on another thread) during the scope. It does
	 * NOT make it safe to hold a raw managed-heap pointer across an allocation: an allocation is itself a GC point that
	 * can move objects. Copy interior pointers out before allocating; never box/new while holding one.
	 *
	 * Entering when already GC-unsafe (or when cooperative suspend is disabled) is a no-op, so the scope is always safe to
	 * apply and may be nested.
	 */
	class ScopedGCUnsafeRegion
	{
	public:
		ScopedGCUnsafeRegion()
		{
			mCookie = mono_threads_enter_gc_unsafe_region(&mStackData);
		}

		~ScopedGCUnsafeRegion()
		{
			mono_threads_exit_gc_unsafe_region(mCookie, &mStackData);
		}

		ScopedGCUnsafeRegion(const ScopedGCUnsafeRegion&) = delete;
		ScopedGCUnsafeRegion& operator=(const ScopedGCUnsafeRegion&) = delete;

	private:
		void* mStackData = nullptr;
		void* mCookie = nullptr;
	};

	/**
	 * RAII scope that places the calling thread in the cooperative GC "safe" (blocking) state for its lifetime, restoring
	 * the previous state on destruction.
	 *
	 * Use this around long native blocking operations invoked from a GC-unsafe context (i.e. from an icall) - a modal
	 * dialog, synchronous file/network I/O, a wait on a kernel object, a sleep - so the thread does not stall garbage
	 * collection for other managed threads while it is blocked. The thread must NOT touch any managed object for the
	 * duration of the scope.
	 *
	 * Entering when already GC-safe (or when cooperative suspend is disabled) is a no-op.
	 */
	class ScopedGCSafeRegion
	{
	public:
		ScopedGCSafeRegion()
		{
			mCookie = mono_threads_enter_gc_safe_region(&mStackData);
		}

		~ScopedGCSafeRegion()
		{
			mono_threads_exit_gc_safe_region(mCookie, &mStackData);
		}

		ScopedGCSafeRegion(const ScopedGCSafeRegion&) = delete;
		ScopedGCSafeRegion& operator=(const ScopedGCSafeRegion&) = delete;

	private:
		void* mStackData = nullptr;
		void* mCookie = nullptr;
	};

	/** @} */
} // namespace b3d
