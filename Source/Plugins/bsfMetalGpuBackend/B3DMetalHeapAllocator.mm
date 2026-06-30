//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DMetalHeapAllocator.h"
#include "B3DMetalGpuDevice.h"
#include "Debug/B3DLog.h"

namespace b3d
{
	namespace render
	{
		MetalHeapAllocator::MetalHeapAllocator(MetalGpuDevice& device)
			: mDevice(device)
		{
			// Shared-storage heaps are only meaningful on devices with unified memory. On discrete
			// GPUs (older Intel Macs, AMD eGPUs) falling back to direct newBufferWithLength: keeps
			// semantics identical — the resource will still be created shared-visible, just without
			// the heap-pool fast path. hasUnifiedMemory is gated to macOS 10.15 / iOS 13.
			if (@available(macOS 10.15, iOS 13.0, *))
				mSharedBucketSupported = [mDevice.GetMetalDevice() hasUnifiedMemory];
			else
				mSharedBucketSupported = false;
		}

		MetalHeapAllocator::~MetalHeapAllocator()
		{
			Shutdown();
		}

		MetalHeapAllocator::Bucket MetalHeapAllocator::StorageModeToBucket(MTLStorageMode storageMode)
		{
			switch (storageMode)
			{
			case MTLStorageModePrivate:
				return BucketPrivate;
			case MTLStorageModeShared:
				return BucketShared;
			default:
				// Managed / Memoryless / any future mode is not pooled here. Callers check
				// storageMode before routing through the allocator, so reaching this branch is a
				// programming error — fall back to Private bucket which at worst produces an
				// allocation failure rather than a silent storage-mode mismatch.
				B3D_ASSERT(false);
				return BucketPrivate;
			}
		}

		id<MTLTexture> MetalHeapAllocator::AllocateTexture(MTLTextureDescriptor* descriptor)
		{
			if (descriptor == nil)
				return nil;

			id<MTLDevice> device = mDevice.GetMetalDevice();
			if (device == nil)
				return nil;

			const MTLStorageMode storageMode = descriptor.storageMode;

			// Query the driver's size/alignment for the would-be texture to decide heap vs direct.
			// heapTextureSizeAndAlignWithDescriptor: is free — it does no allocation, just returns
			// the layout parameters. Available macOS 10.13 / iOS 10.
			MTLSizeAndAlign sizeAndAlign = {};
			if (@available(macOS 10.13, iOS 10.0, *))
				sizeAndAlign = [device heapTextureSizeAndAlignWithDescriptor:descriptor];

			const bool poolEligible = (storageMode == MTLStorageModePrivate)
				|| (storageMode == MTLStorageModeShared && mSharedBucketSupported);
			const bool tooLarge = sizeAndAlign.size >= kLargeResourceThreshold;

			if (!poolEligible || tooLarge)
				return [device newTextureWithDescriptor:descriptor];

			id<MTLTexture> pooled = AllocateTextureFromHeap(descriptor, storageMode);
			if (pooled != nil)
				return pooled;

			// Heap path failed (exceptional: heap pool exhausted due to pressure, or every existing
			// heap reported nil even after we tried to grow). Fall back to direct device allocation
			// so the scene can still render at a slower rate rather than crashing on a nil texture.
			B3D_LOG(Warning, LogRenderBackend,
				"MetalHeapAllocator: heap-pool texture alloc failed for {0} bytes; falling back to device.",
				(u64)sizeAndAlign.size);
			return [device newTextureWithDescriptor:descriptor];
		}

		id<MTLBuffer> MetalHeapAllocator::AllocateBuffer(u64 length, MTLResourceOptions options)
		{
			if (length == 0)
				return nil;

			id<MTLDevice> device = mDevice.GetMetalDevice();
			if (device == nil)
				return nil;

			// Extract storage mode from the flat MTLResourceOptions mask. MTLResourceOptions packs
			// storage/cache/hazard modes into a single u64 at fixed bit positions defined by Metal.
			const MTLStorageMode storageMode = (MTLStorageMode)((options & MTLResourceStorageModeMask) >> MTLResourceStorageModeShift);

			const bool poolEligible = (storageMode == MTLStorageModePrivate)
				|| (storageMode == MTLStorageModeShared && mSharedBucketSupported);
			const bool tooLarge = length >= kLargeResourceThreshold;

			if (!poolEligible || tooLarge)
				return [device newBufferWithLength:length options:options];

			id<MTLBuffer> pooled = AllocateBufferFromHeap(length, storageMode, options);
			if (pooled != nil)
				return pooled;

			B3D_LOG(Warning, LogRenderBackend,
				"MetalHeapAllocator: heap-pool buffer alloc failed for {0} bytes; falling back to device.",
				length);
			return [device newBufferWithLength:length options:options];
		}

		id<MTLTexture> MetalHeapAllocator::AllocateTextureFromHeap(MTLTextureDescriptor* descriptor, MTLStorageMode storageMode)
		{
			BucketData& bucket = mBuckets[StorageModeToBucket(storageMode)];
			Lock lock(bucket.BucketMutex);

			// Try existing heaps in LRU-ish order (front to back). Automatic heaps sub-allocate
			// internally — a nil return simply means this heap does not have a free span large
			// enough for the request, so we move on.
			for (id<MTLHeap> heap : bucket.Heaps)
			{
				id<MTLTexture> texture = [heap newTextureWithDescriptor:descriptor];
				if (texture != nil)
					return texture;
			}

			// Grow the bucket with a fresh heap sized to the default (the caller has already
			// pre-checked that the request is below kLargeResourceThreshold, so kDefaultHeapSize
			// will always fit it).
			id<MTLHeap> freshHeap = CreateHeap(storageMode, kDefaultHeapSize);
			if (freshHeap == nil)
				return nil;

			bucket.Heaps.push_back(freshHeap);

			return [freshHeap newTextureWithDescriptor:descriptor];
		}

		id<MTLBuffer> MetalHeapAllocator::AllocateBufferFromHeap(u64 length, MTLStorageMode storageMode, MTLResourceOptions options)
		{
			BucketData& bucket = mBuckets[StorageModeToBucket(storageMode)];
			Lock lock(bucket.BucketMutex);

			for (id<MTLHeap> heap : bucket.Heaps)
			{
				id<MTLBuffer> buffer = [heap newBufferWithLength:length options:options];
				if (buffer != nil)
					return buffer;
			}

			id<MTLHeap> freshHeap = CreateHeap(storageMode, kDefaultHeapSize);
			if (freshHeap == nil)
				return nil;

			bucket.Heaps.push_back(freshHeap);

			return [freshHeap newBufferWithLength:length options:options];
		}

		id<MTLHeap> MetalHeapAllocator::CreateHeap(MTLStorageMode storageMode, u64 minimumSize)
		{
			id<MTLDevice> device = mDevice.GetMetalDevice();
			if (device == nil)
				return nil;

			// MTLHeapDescriptor is available on macOS 10.13 / iOS 10. Backstop for older SDKs is
			// already handled at the call site: without heap support the fallback path returns
			// direct device allocations.
			if (@available(macOS 10.13, iOS 10.0, *))
			{
				MTLHeapDescriptor* heapDesc = [[MTLHeapDescriptor alloc] init];
				heapDesc.size = minimumSize;
				heapDesc.storageMode = storageMode;
				heapDesc.cpuCacheMode = MTLCPUCacheModeDefaultCache;

				// Automatic heap type lets Metal place resources internally with no explicit offset
				// bookkeeping. Placement heaps would require us to track offsets + aliasing, which
				// is out of scope for B13 (phase-3 material once transient-aliasing needs land).
				if (@available(macOS 10.15, iOS 13.0, *))
					heapDesc.type = MTLHeapTypeAutomatic;

				// Mirror the existing per-resource default — tracked. The engine-side resource
				// tracker that would unlock untracked heaps is gated on S14 per the existing TODOs
				// in B3DMetalTexture.mm / B3DMetalGpuBuffer.mm.
				if (@available(macOS 10.15, iOS 13.0, *))
					heapDesc.hazardTrackingMode = MTLHazardTrackingModeTracked;

				id<MTLHeap> heap = [device newHeapWithDescriptor:heapDesc];
#if !__has_feature(objc_arc)
				[heapDesc release];
#endif

				if (heap == nil)
				{
					B3D_LOG(Warning, LogRenderBackend,
						"MetalHeapAllocator: newHeapWithDescriptor failed for {0} bytes, storageMode {1}.",
						minimumSize, (u32)storageMode);
					return nil;
				}

				return heap;
			}

			return nil;
		}

		void MetalHeapAllocator::Shutdown()
		{
			// Under ARC, clearing the Vector drops every heap's strong ref and Metal reclaims the
			// backing storage on the next autorelease pool drain. Under MRC the heaps were returned
			// with +1 retain from newHeapWithDescriptor; release each explicitly.
			for (u32 bucketIndex = 0; bucketIndex < BucketCount; bucketIndex++)
			{
				BucketData& bucket = mBuckets[bucketIndex];
				Lock lock(bucket.BucketMutex);
#if !__has_feature(objc_arc)
				for (id<MTLHeap> heap : bucket.Heaps)
					[heap release];
#endif
				bucket.Heaps.clear();
			}
		}
	} // namespace render
} // namespace b3d
