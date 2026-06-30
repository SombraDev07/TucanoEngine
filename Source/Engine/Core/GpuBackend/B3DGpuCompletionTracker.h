//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "B3DGpuTimelineFence.h"

namespace b3d
{
	/** @addtogroup GpuBackend
	 *  @{
	 */

	/**
	 * Abstraction over a monotonic GPU-completion marker, used by GPU allocators (and other
	 * deferred-cleanup consumers) to schedule reclamation against GPU progress. A "marker" is an
	 * opaque, monotonically increasing value handed out at record/submit time; the GPU is said to
	 * have "completed" a marker once every submission tagged with a value <= that marker has drained.
	 */
	class B3D_EXPORT IGpuCompletionTracker
	{
	public:
		virtual ~IGpuCompletionTracker() = default;

		/** Value the next piece of work recorded/submitted will be tagged with. Monotonic; starts at 0. */
		virtual u64 GetCurrentMarker() const = 0;

		/** Returns true once the GPU has drained every submission tagged with a value <= @p marker. */
		virtual bool IsMarkerComplete(u64 marker) const = 0;
	};

	/**
	 * Frame-count based completion tracker for the render thread's primary context. The marker is the
	 * frame index currently being recorded; a marker is "complete" only after the conservative
	 * RenderThread::kMaximumFramesInFlight lag, since at end-of-frame the device blocks until the
	 * previous frame's resources are safe to reuse.
	 */
	class B3D_EXPORT GpuFrameCompletionTracker : public IGpuCompletionTracker
	{
	public:
		/** Index of the frame currently being recorded. Monotonic; starts at 0. */
		u64 GetCurrentMarker() const override { return mFrameIndex.load(std::memory_order_acquire); }

		/**
		 * Returns true once the GPU has caught up such that frame @p marker is no longer in flight on
		 * any queue — i.e. the current frame index has advanced by at least
		 * RenderThread::kMaximumFramesInFlight beyond it.
		 */
		bool IsMarkerComplete(u64 marker) const override;

		/** Advances to the next frame. */
		void AdvanceFrame() { mFrameIndex.fetch_add(1, std::memory_order_acq_rel); }

	private:
		std::atomic<u64> mFrameIndex{0};
	};

	/**
	 * Timeline-fence based completion tracker. Composes a single GpuTimelineFence; the marker is the
	 * exact fence value the GPU signals (no conservative lag like GpuFrameCompletionTracker).
	 *
	 * Marker contract: GetCurrentMarker() returns the value the NEXT submit will signal. Submission
	 * records NotifyWillSubmit() (which yields that value and advances the marker), so a page retired
	 * right before the submit is stamped with exactly the value its submit signals.
	 */
	class B3D_EXPORT GpuFenceCompletionTracker : public IGpuCompletionTracker
	{
	public:
		explicit GpuFenceCompletionTracker(TShared<GpuTimelineFence> fence)
			: mFence(std::move(fence))
		{ }

		/** Value the next submit will signal. Monotonic; starts at 1 (0 is the timeline's unsignaled state). */
		u64 GetCurrentMarker() const override { return mNextValue; }

		/** Exact: true once the GPU has signaled @p marker on the fence. */
		bool IsMarkerComplete(u64 marker) const override { return mFence->IsSignaled(marker); }

		/** Records an impending submission: returns the fence + value to signal, and advances the marker. */
		GpuTimelineFenceAndValue NotifyWillSubmit()
		{
			const u64 value = mNextValue++;
			mLastSignaled = value;
			return { mFence, value };
		}

		/** Blocks until the last submitted marker completes. No-op if nothing was submitted. */
		void WaitUntilComplete()
		{
			if (mLastSignaled > 0)
				mFence->Wait(mLastSignaled);
		}

		/** Marker value of the most recent submission, or 0 if nothing has been submitted yet. */
		u64 GetLastSubmittedMarker() const { return mLastSignaled; }

		const TShared<GpuTimelineFence>& GetFence() const { return mFence; }

	private:
		TShared<GpuTimelineFence> mFence;
		u64 mNextValue = 1;
		u64 mLastSignaled = 0;
	};

	/** @} */

} // namespace b3d
