//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DMetalGpuQueryPool.h"
#include "B3DMetalGpuDevice.h"
#include "B3DMetalGpuQueue.h"
#include "Debug/B3DLog.h"

namespace b3d
{
	namespace render
	{
		struct MetalGpuQueryPool::Impl
		{
			id<MTLBuffer> VisibilityBuffer = nil;
			id<MTLCounterSampleBuffer> CounterBuffer = nil;
		};

		MetalGpuQueryPool::MetalGpuQueryPool(MetalGpuDevice& gpuDevice, const GpuQueryPoolCreateInformation& createInformation)
			: GpuQueryPool(createInformation)
			, mGpuDevice(gpuDevice)
			, mImpl(B3DMakeUnique<Impl>())
		{
			id<MTLDevice> device = gpuDevice.GetMetalDevice();

			if (createInformation.Type == GpuQueryType::Occlusion)
			{
				if (device != nil)
				{
					const NSUInteger byteSize = (NSUInteger)createInformation.PoolSize * sizeof(u64);
					mImpl->VisibilityBuffer = [device newBufferWithLength:byteSize
						options:MTLResourceStorageModeShared];
				}
			}
			else if (createInformation.Type == GpuQueryType::Timestamp)
			{
				// Gated on RSC_TIMER_QUERIES: the device only advertises this capability when the platform
				// supports stage-boundary counter sampling and a timestamp counter set is available.
				id<MTLCounterSet> timestampSet = gpuDevice.GetTimestampCounterSet();
				if (device != nil && timestampSet != nil)
				{
					MTLCounterSampleBufferDescriptor* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
					desc.counterSet = timestampSet;
					desc.storageMode = MTLStorageModeShared;
					desc.sampleCount = (NSUInteger)createInformation.PoolSize;

					NSError* err = nil;
					mImpl->CounterBuffer = [device newCounterSampleBufferWithDescriptor:desc error:&err];
					if (mImpl->CounterBuffer == nil)
					{
						const char* reason = err ? [[err localizedDescription] UTF8String] : "unknown error";
						B3D_LOG(Error, LogRenderBackend, "Failed to create MTLCounterSampleBuffer for timestamp pool: {0}", reason);
					}
#if !__has_feature(objc_arc)
					[desc release];
#endif
				}
				else
				{
					B3D_LOG(Warning, LogRenderBackend,
						"Timestamp query pool created on a device without RSC_TIMER_QUERIES; GetQueryResult will return 0.");
				}
			}
			// Pipeline-statistics pools remain unimplemented; Metal does not expose the same counters as
			// Vulkan at this granularity. Their TryResolve/GetQueryResult return defaults and callers
			// should feature-detect via capability flags on the device.
		}

		MetalGpuQueryPool::~MetalGpuQueryPool()
		{
			if (mImpl)
			{
				mImpl->VisibilityBuffer = nil;
				mImpl->CounterBuffer = nil;
			}
		}

		id<MTLBuffer> MetalGpuQueryPool::GetVisibilityBuffer() const
		{
			return mImpl->VisibilityBuffer;
		}

		id<MTLCounterSampleBuffer> MetalGpuQueryPool::GetCounterBuffer() const
		{
			return mImpl->CounterBuffer;
		}

		GpuQueryId MetalGpuQueryPool::AllocateQuery()
		{
			if (mNextQueryId >= mPoolSize)
				return GpuQueryId();

			return GpuQueryId(mNextQueryId++);
		}

		void MetalGpuQueryPool::MarkSubmitted(MetalGpuQueue& queue, u64 eventValue)
		{
			// Event values are monotonic within a queue, so if the same queue is already tracked we only
			// need to bump its recorded value to the newer (larger) one. A different queue needs its own
			// entry because event values in a different queue live in a different namespace and can't be
			// compared.
			for (auto& entry : mInFlightSubmissions)
			{
				if (entry.first == &queue)
				{
					if (eventValue > entry.second)
						entry.second = eventValue;
					return;
				}
			}

			mInFlightSubmissions.push_back(std::make_pair(&queue, eventValue));
		}

		bool MetalGpuQueryPool::TryResolve(bool wait)
		{
			if (mInFlightSubmissions.empty())
				return true;

			// B11: snapshot (queue -> signaledValue) once. @c [event signaledValue] is a non-inlined
			// KVC-style read and we'd otherwise hit it once per in-flight entry. Pools in practice
			// straddle only one or two queues, so the snapshot stays tiny and the linear lookup below
			// is effectively free.
			TInlineArray<std::pair<MetalGpuQueue*, u64>, 4> signaledSnapshot;
			for (auto& entry : mInFlightSubmissions)
			{
				bool alreadyCached = false;
				for (const auto& cached : signaledSnapshot)
				{
					if (cached.first == entry.first)
					{
						alreadyCached = true;
						break;
					}
				}
				if (alreadyCached)
					continue;

				id<MTLSharedEvent> event = entry.first->GetSharedEvent();
				const u64 signaled = (event != nil) ? (u64)[event signaledValue] : ~(u64)0;
				signaledSnapshot.Add(std::make_pair(entry.first, signaled));
			}

			auto fnSnapshotValue = [&](MetalGpuQueue* queue) -> u64
			{
				for (const auto& cached : signaledSnapshot)
				{
					if (cached.first == queue)
						return cached.second;
				}
				// Unreachable given the snapshot construction above, but defend against future
				// refactors by falling back to a direct query.
				id<MTLSharedEvent> event = queue->GetSharedEvent();
				return (event != nil) ? (u64)[event signaledValue] : ~(u64)0;
			};

			// First pass: drop any entries whose queue has already signaled past its tracked value. This
			// works for the non-wait path and is also the cheapest way to resolve the common case where
			// the GPU finished some time ago.
			for (auto it = mInFlightSubmissions.begin(); it != mInFlightSubmissions.end();)
			{
				if (fnSnapshotValue(it->first) >= it->second)
					it = mInFlightSubmissions.erase(it);
				else
					++it;
			}

			if (mInFlightSubmissions.empty())
				return true;

			if (!wait)
				return false;

			// Block on each remaining submission. We register one listener per entry — each entry lives
			// on a distinct queue, so the notifyListener calls are independent and the semaphore counts
			// them in aggregate. Reuses the per-queue MTLSharedEventListener (Apple recommends one
			// listener per queue).
			dispatch_semaphore_t sem = dispatch_semaphore_create(0);
			const u32 pending = (u32)mInFlightSubmissions.size();
			for (auto& entry : mInFlightSubmissions)
			{
				id<MTLSharedEvent> event = entry.first->GetSharedEvent();
				MTLSharedEventListener* listener = entry.first->GetSharedEventListener();
				[event notifyListener:listener atValue:entry.second block:^(id<MTLSharedEvent>, uint64_t)
				{
					dispatch_semaphore_signal(sem);
				}];
			}

			for (u32 i = 0; i < pending; ++i)
				dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

			// See the matching note in @c MetalGpuQueue::WaitUntilIdle: @c dispatch_semaphore_t is not
			// ARC-managed under MRC, so release it explicitly when ARC is off.
#if !__has_feature(objc_arc)
			dispatch_release(sem);
#endif

			mInFlightSubmissions.clear();
			return true;
		}

		u64 MetalGpuQueryPool::GetQueryResult(GpuQueryId queryId, u32 elementIndex)
		{
			(void)elementIndex;

			if (!queryId.IsValid())
				return 0;

			if (mQueryType == GpuQueryType::Occlusion)
			{
				if (mImpl->VisibilityBuffer == nil)
					return 0;

				const u64* contents = (const u64*)[mImpl->VisibilityBuffer contents];
				if (contents == nullptr)
					return 0;

				return contents[queryId.Id];
			}

			if (mQueryType == GpuQueryType::Timestamp)
			{
				if (mImpl->CounterBuffer == nil)
					return 0;

				NSData* resolved = [mImpl->CounterBuffer resolveCounterRange:NSMakeRange(queryId.Id, 1)];
				if (resolved == nil || [resolved length] < sizeof(MTLCounterResultTimestamp))
					return 0;

				const MTLCounterResultTimestamp* ts = (const MTLCounterResultTimestamp*)[resolved bytes];
				// Return raw GPU ticks. Callers that need wall-clock time use
				// MetalGpuDevice::ConvertTimestampToMilliseconds, which applies the CPU-GPU calibration
				// captured at device init.
				return ts->timestamp;
			}

			return 0;
		}
	} // namespace render
} // namespace b3d
