//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DMetalPrerequisites.h"
#include "Utility/Threading/B3DThreading.h"

namespace b3d
{
	namespace render
	{
		class MetalGpuDevice;

		/** @addtogroup MetalGpuBackend
		 *  @{
		 */

		/**
		 * Sub-allocates @c MTLTexture and @c MTLBuffer instances out of a pool of @c MTLHeap
		 * objects, bucketed by storage mode. Replaces the per-resource @c newTextureWithDescriptor:
		 * / @c newBufferWithLength:options: calls on the device which are the single largest
		 * per-resource allocation cost on Apple Silicon for render-target-heavy scenes.
		 *
		 * Design:
		 *  - One bucket per @c MTLStorageMode (currently @c Private and @c Shared). @c Memoryless
		 *    is handled separately once transient-flag plumbing lands.
		 *  - Each bucket holds a growable list of @c MTLHeap instances, each sized to
		 *    @c kDefaultHeapSize at creation and grown on-demand when the existing heaps report
		 *    insufficient free space for a request.
		 *  - Requests whose size exceeds @c kLargeResourceThreshold bypass the heap pool entirely
		 *    and allocate directly on the device. This keeps oversized render targets / streaming
		 *    buffers from fragmenting the pool.
		 *
		 * Lifetime:
		 *  - Heap-backed resources release back to their heap automatically via normal ARC /
		 *    @c release refcount (Metal's heap contract — no explicit deallocation call is
		 *    required). Callers continue to route prior resources through
		 *    @c MetalGpuDevice::QueueMetalResourceForDeferredRelease as before.
		 *
		 * Thread safety:
		 *  - All public methods are safe to call from worker fibers. Internal mutex-per-bucket
		 *    keeps contention to the bucket actually being hit (private vs shared).
		 */
		class MetalHeapAllocator
		{
		public:
			/** Default size of each freshly-grown @c MTLHeap (64 MB). */
			static constexpr u64 kDefaultHeapSize = 64ull * 1024ull * 1024ull;

			/**
			 * Resources above this size bypass the heap pool and allocate directly on the device
			 * to avoid fragmenting the pool with oversized allocations (32 MB — half the default
			 * heap size).
			 */
			static constexpr u64 kLargeResourceThreshold = 32ull * 1024ull * 1024ull;

			MetalHeapAllocator(MetalGpuDevice& device);
			~MetalHeapAllocator();

#ifdef __OBJC__
			/**
			 * Allocates an @c MTLTexture from a heap whose storage mode matches
			 * @p descriptor.storageMode, or directly on the device if the texture exceeds
			 * @c kLargeResourceThreshold or no heap in the matching bucket has room. Returns nil
			 * on failure.
			 */
			id<MTLTexture> AllocateTexture(MTLTextureDescriptor* descriptor);

			/**
			 * Allocates an @c MTLBuffer of @p length bytes with the storage mode encoded in
			 * @p options. As with textures, requests exceeding @c kLargeResourceThreshold fall
			 * back to a direct device allocation. Returns nil on failure.
			 */
			id<MTLBuffer> AllocateBuffer(u64 length, MTLResourceOptions options);
#endif

			/**
			 * Drops every pooled @c MTLHeap. Callers must have drained the device's
			 * deferred-release queue first, otherwise resources still being tracked for delayed
			 * release may hold dangling heap refs after this returns. Invoked from the device's
			 * destructor after @c WaitUntilIdle and deferred-release drain.
			 */
			void Shutdown();

		private:
#ifdef __OBJC__
			/**
			 * Allocates a texture from the heap bucket matching @p storageMode. Grows the bucket
			 * with a new heap when existing heaps report insufficient space. The caller has
			 * already pre-checked the large-resource threshold.
			 */
			id<MTLTexture> AllocateTextureFromHeap(MTLTextureDescriptor* descriptor, MTLStorageMode storageMode);

			/**
			 * Counterpart of @c AllocateTextureFromHeap for buffers. @p hazardTrackingMode and
			 * @p cpuCacheMode are extracted from the caller's @c MTLResourceOptions so newly-grown
			 * heaps preserve them.
			 */
			id<MTLBuffer> AllocateBufferFromHeap(u64 length, MTLStorageMode storageMode, MTLResourceOptions options);

			/**
			 * Creates a fresh @c MTLHeap of at least @p minimumSize bytes with the given storage
			 * mode. Returns nil on failure. Caller appends to the bucket.
			 */
			id<MTLHeap> CreateHeap(MTLStorageMode storageMode, u64 minimumSize);
#endif

			/** Index of the bucket for each supported @c MTLStorageMode. */
			enum Bucket
			{
				BucketPrivate = 0,
				BucketShared = 1,
				BucketCount = 2
			};

#ifdef __OBJC__
			static Bucket StorageModeToBucket(MTLStorageMode storageMode);
#endif

			struct BucketData
			{
				// One mutex per bucket rather than one global. Workers allocating shared-storage
				// staging buffers do not contend with workers allocating private-storage textures.
				Mutex BucketMutex;

#ifdef __OBJC__
				// Heaps are stored as raw id. Under ARC this list retains each heap; under MRC
				// CreateHeap returns a +1 retained reference and we release in Shutdown.
				Vector<id<MTLHeap>> Heaps;
#endif
			};

			MetalGpuDevice& mDevice;
			BucketData mBuckets[BucketCount];
			bool mSharedBucketSupported = false;
		};

		/** @} */
	} // namespace render
} // namespace b3d
