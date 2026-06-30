//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DMetalPrerequisites.h"
#include "GpuBackend/B3DGpuQueries.h"

namespace b3d
{
	namespace render
	{
		class MetalGpuDevice;
		class MetalGpuQueue;

		/** @addtogroup MetalGpuBackend
		 *  @{
		 */

		/**
		 * Metal implementation of a query pool.
		 *
		 * Occlusion pools allocate a shared @c MTLBuffer used as the visibility result buffer; each
		 * query gets an 8-byte slot. Timestamp pools (macOS 10.15+) use an @c MTLCounterSampleBuffer.
		 * Pipeline-statistics pools are not implemented (Metal does not expose the same granularity as
		 * Vulkan).
		 */
		class MetalGpuQueryPool final : public GpuQueryPool
		{
		public:
			MetalGpuQueryPool(MetalGpuDevice& gpuDevice, const GpuQueryPoolCreateInformation& createInformation);
			~MetalGpuQueryPool() override;

			GpuQueryId AllocateQuery() override;
			bool TryResolve(bool wait = false) override;
			u64 GetQueryResult(GpuQueryId queryId, u32 elementIndex = 0) override;

#ifdef __OBJC__
			/** Returns the underlying visibility-result buffer for occlusion queries. */
			id<MTLBuffer> GetVisibilityBuffer() const;

			/** Returns the underlying counter sample buffer for timestamp queries, or nil if unsupported. */
			id<MTLCounterSampleBuffer> GetCounterBuffer() const;
#endif

			/** Returns the byte offset of a query's result slot inside the visibility buffer. */
			u32 GetQueryOffset(GpuQueryId queryId) const { return queryId.Id * sizeof(u64); }

			/**
			 * Records that the owning command buffer has been submitted on @p queue, with the shared-event
			 * value @p eventValue scheduled as the completion marker. @c TryResolve uses this to poll each
			 * tracked queue's signaled value instead of committing an empty flush.
			 *
			 * A single pool may be written to from multiple queues over its lifetime (e.g. recorded on the
			 * graphics queue in one frame, then on the compute queue in the next). Event values live in a
			 * per-queue namespace and cannot be compared across queues, so each (queue, value) pair is
			 * tracked independently until resolved.
			 */
			void MarkSubmitted(MetalGpuQueue& queue, u64 eventValue);

			/** Resets the allocator so @c AllocateQuery returns slot 0 again. Matches @c ResetQueries. */
			void ResetNextQueryId() { mNextQueryId = 0; }

		private:
			struct Impl;

			MetalGpuDevice& mGpuDevice;
			TUnique<Impl> mImpl;
			u32 mNextQueryId = 0;

			// Tracks every (queue, eventValue) pair this pool was written from that has not yet been
			// resolved. Appended to by MarkSubmitted (or the existing entry's value is replaced with the
			// max, since event values within a queue are monotonic). Entries are removed by TryResolve
			// once the queue's signaled value catches up.
			Vector<std::pair<MetalGpuQueue*, u64>> mInFlightSubmissions;
		};

		/** @} */
	} // namespace render
} // namespace b3d
