//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DMetalGpuDevice.h"
#include "B3DMetalGpuQueue.h"
#include "B3DMetalGpuCommandBuffer.h"
#include "B3DMetalGpuCommandBufferPool.h"
#include "B3DMetalGpuBuffer.h"
#include "B3DMetalTexture.h"
#include "B3DMetalHeapAllocator.h"
#include "B3DMetalGpuProgram.h"
#include "B3DMetalGpuPipelineState.h"
#include "Math/B3DMath.h"
#include "B3DMetalGpuParameterSet.h"
#include "B3DMetalGpuParameterSetPool.h"
#include "B3DMetalGpuPipelineParameterLayout.h"
#include "B3DMetalSamplerState.h"
#include "GpuBackend/B3DGpuPipelineParameterLayout.h"
#include "GpuBackend/B3DGpuParameterSet.h"
#include "GpuBackend/B3DGpuProgramParameterDescription.h"
#include "GpuBackend/B3DGpuBackendUtility.h"
#include "B3DMetalEventQuery.h"
#include "B3DMetalGpuQueryPool.h"
#include "GpuBackend/B3DVideoModeInfo.h"
#include "GpuBackend/B3DGpuTimelineFence.h"
#include "B3DMetalGpuTimelineFence.h"
#include "Math/B3DMatrix4.h"
#include "Utility/B3DCommonTypes.h"
#include "FileSystem/B3DFileSystem.h"
#include "Debug/B3DLog.h"
#include "Utility/B3DScopeGuard.h"
#include <atomic>
#include "Utility/Threading/B3DThreading.h"

#include <mach/mach_time.h>

namespace b3d
{
	namespace render
	{
		/** Holds all Metal/Objective-C handles owned by the device. */
		struct MetalGpuDevice::Impl
		{
			// One entry per deferred release request. @c Resource is the Obj-C Metal object being held
			// alive until @c OriginQueue has committed at least @c EventValue on its shared event; at
			// that point @c BeginFrame drops the strong reference and the driver reclaims the storage.
			struct DeferredReleaseEntry
			{
				id Resource = nil;
				MetalGpuQueue* OriginQueue = nullptr;
				u64 EventValue = 0;
			};

			id<MTLDevice> Device = nil;
			id<MTLCommandQueue> CommandQueues[GQT_COUNT] = { nil };
			id<MTLSharedEvent> QueueEvents[GQT_COUNT] = { nil };

			// Deferred-release list drained on every @c BeginFrame. Guarded by @c DeferredReleaseMutex
			// because recreate can fire from worker fibers while the render thread is ticking frames.
			Vector<DeferredReleaseEntry> DeferredReleases;
			Mutex DeferredReleaseMutex;

			// Resolved timestamp counter set, or nil if the device does not expose one at stage-boundary
			// sampling. Used by MetalGpuQueryPool to decide whether to create an MTLCounterSampleBuffer
			// for Timestamp-type pools.
			id<MTLCounterSet> TimestampCounterSet = nil;

			// Counter-sampling support per encoder kind. Metal's @c sampleCountersInBuffer:atSampleIndex:
			// family of calls on a render / compute / blit encoder additionally requires the device to
			// advertise the matching sampling point (Draw / Dispatch / Blit boundary respectively).
			// Apple Silicon advertises @c MTLCounterSamplingPointAtStageBoundary but typically does
			// *not* advertise these per-encoder sampling points, so invoking the encoder-level API on
			// such devices fails validation. These flags gate each branch in WriteTimestamp.
			bool SupportsRenderEncoderTimestamps = false;
			bool SupportsComputeEncoderTimestamps = false;
			bool SupportsBlitEncoderTimestamps = false;

			// On-disk pipeline cache — split into two archives because URL-loaded MTLBinaryArchives are
			// immutable. LoadedBinaryArchive is the (optional) read-only archive loaded from disk and
			// used by pipeline state descriptors for fast-path lookup of pipelines compiled in a prior
			// run. MutableBinaryArchive is a fresh empty archive that receives this session's new
			// compiles via addRenderPipelineFunctionsWithDescriptor: / addComputePipelineFunctionsWithDescriptor:,
			// and is serialized to disk in the destructor. Either / both may be nil when the platform
			// (iOS simulator) or runtime (pre-macOS 11 / pre-iOS 14) doesn't support binary archives.
			//
			// Archive construction is deferred to first use (see MetalGpuDevice::EnsureBinaryArchives)
			// so device init does zero filesystem I/O — the NSSearchPathForDirectoriesInDomains +
			// FileSystem::Exists + FileSystem::CreateFolder cost is only paid when the first PSO
			// actually compiles. BinaryArchivesInitialized guards a double-checked locking init under
			// BinaryArchivesMutex so concurrent pipeline compiles race-safely converge; the flag is
			// std::atomic<bool> to make the unsynchronized fast-path load legal under the C++ memory
			// model (a plain-bool DCL is a classic data race). The release store at the end of the
			// init body synchronizes-with the acquire load on the fast path so the archive handles
			// and path fields above are observable once the flag is seen set.
			id<MTLBinaryArchive> LoadedBinaryArchive = nil;
			id<MTLBinaryArchive> MutableBinaryArchive = nil;
			Path BinaryArchivePath;
			Mutex BinaryArchivesMutex;
			std::atomic<bool> BinaryArchivesInitialized{ false };

			// First CPU/GPU timestamp pair captured at device init. The second pair is sampled lazily
			// on the first call to ConvertTimestampToMilliseconds so the calibration uses whatever
			// elapsed time accrued organically between init and first timer-query resolution — both
			// more accurate than a 1 ms sleep and free of startup cost. TimestampCalibrationDone flips
			// true after the second pair is captured and the GPU tick rate is computed. Guarded by
			// TimestampCalibrationMutex since ConvertTimestampToMilliseconds is not restricted to the
			// render thread.
			MTLTimestamp FirstCpuTimestamp = 0;
			MTLTimestamp FirstGpuTimestamp = 0;
			bool FirstTimestampPairCaptured = false;
			bool TimestampCalibrationDone = false;
			Mutex TimestampCalibrationMutex;
		};

		namespace
		{
			/** Parses the Metal device's reported name to infer the GPU vendor. */
			GPUVendor ParseVendorFromDeviceName(NSString* deviceName)
			{
				if (deviceName == nil)
					return GPU_UNKNOWN;

				const String name([deviceName UTF8String]);

				// The Metal device name typically starts with the vendor name followed by the model
				// (e.g. "Apple M1 Pro", "AMD Radeon Pro 5500M", "Intel(R) Iris(R) Plus Graphics",
				// "NVIDIA GeForce GT 750M"). Match on the common prefixes.
				if (name.find("Apple") != String::npos)
					return GPU_APPLE;
				if (name.find("AMD") != String::npos || name.find("Radeon") != String::npos)
					return GPU_AMD;
				if (name.find("Intel") != String::npos)
					return GPU_INTEL;
				if (name.find("NVIDIA") != String::npos || name.find("GeForce") != String::npos)
					return GPU_NVIDIA;

				return GPU_UNKNOWN;
			}

			/**
			 * Resolves the on-disk path for the Metal pipeline binary archive. Uses the user's caches
			 * directory (@c NSCachesDirectory) so the archive survives reboots but is allowed to be
			 * purged by the OS under disk pressure — standard location for regenerable caches. Falls
			 * back to @c NSTemporaryDirectory if the caches directory can't be resolved (rare).
			 */
			Path GetPipelineBinaryArchivePath()
			{
				NSArray* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
				String cacheDir;
				if ([paths count] > 0)
					cacheDir = String([[paths objectAtIndex:0] UTF8String]);
				else
					cacheDir = String([NSTemporaryDirectory() UTF8String]);

				Path archiveDir = Path(cacheDir) + "Banshee";
				if (!FileSystem::Exists(archiveDir))
					FileSystem::CreateFolder(archiveDir);

				return archiveDir + "MetalPipelineCache.bin";
			}

		} // namespace


		MetalGpuDevice::MetalGpuDevice()
			: mImpl(B3DMakeUnique<Impl>())
		{
			mVideoModeInfo = B3DMakeShared<VideoModeInfo>();
		}

		MetalGpuDevice::~MetalGpuDevice()
		{
			// Flip the shutdown flag before any teardown work runs. Late resource destructors reached
			// during device teardown observe this via @c IsShuttingDown and take the synchronous
			// release path instead of appending to @c DeferredReleases — queuing after the drain
			// below would leave entries in the list when @c mHeapAllocator.reset() tears down the
			// heaps those resources are backed by.
			mIsShuttingDown = true;

			// Persist the mutable pipeline binary archive to disk so the next launch can reuse compiled
			// functions. Only MutableBinaryArchive is serialized — LoadedBinaryArchive is immutable and
			// the file it came from already exists on disk. This means the persisted cache reflects only
			// this session's new compiles, overwriting the prior file; pipelines that weren't used this
			// session will not survive in the cache. That's the intended behavior for a regenerable
			// cache. Serialization failures here are non-fatal (permission changes on the caches folder
			// etc.) — we log and continue teardown.
			//
			// serializeToURL: blocks the calling thread for the full archive write (archives can be MB-
			// sized — a cold-launched game with hundreds of BSL permutations serialized a 10+ MB file in
			// profiles). We kick it off on the default-priority concurrent dispatch queue so the rest of
			// teardown (queue release, device nil, deferred-release drain) runs in parallel; a group
			// wait at the end of the destructor joins the two paths before returning. Obj-C strong
			// references to the archive and the NSURL are held by the block's capture list so they
			// survive until the serialize call returns.
			dispatch_group_t serializeGroup = nullptr;
			if (mImpl->MutableBinaryArchive != nil && !mImpl->BinaryArchivePath.IsEmpty())
			{
				if (@available(macOS 11.0, iOS 14.0, *))
				{
					serializeGroup = dispatch_group_create();

					NSString* nsPath = @(mImpl->BinaryArchivePath.ToString().c_str());
					NSURL* url = [NSURL fileURLWithPath:nsPath];
					id<MTLBinaryArchive> archive = mImpl->MutableBinaryArchive;
					const String archivePathStr = mImpl->BinaryArchivePath.ToString();

					dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
					dispatch_group_async(serializeGroup, queue, ^{
						NSError* serializeError = nil;
						if (![archive serializeToURL:url error:&serializeError])
						{
							NSString* desc = serializeError ? [serializeError localizedDescription] : @"unknown error";
							B3D_LOG(Warning, LogRenderBackend,
								"Failed to serialize Metal pipeline binary archive to '{0}': {1}",
								archivePathStr,
								String([desc UTF8String]));
						}
					});
				}
			}

			// Drain any still-pending deferred releases unconditionally — the device is going away so no
			// Obj-C references owned by the list may outlive it. Callers are expected to have driven the
			// GpuDevice through @c WaitUntilIdle before destroying it, so dropping the strong refs here
			// cannot race with any in-flight command buffer.
			{
				Lock lock(mImpl->DeferredReleaseMutex);
				for (Impl::DeferredReleaseEntry& entry : mImpl->DeferredReleases)
					entry.Resource = nil;
				mImpl->DeferredReleases.clear();
			}

			// Tear down the heap allocator only after the deferred-release queue has fully drained.
			// Heap-backed resources hold implicit refs back to their parent heap; dropping the
			// heaps first while the deferred-release list still has heap-backed textures / buffers
			// queued would leave those with dangling parent refs when they release later in this
			// destructor. The explicit drain above guarantees the order. Resetting the TUnique nils
			// all pooled MTLHeaps; the driver reclaims their storage on the next autorelease drain.
			mHeapAllocator.reset();

			// Join the async serializeToURL: block before we release the device — MTLBinaryArchive's
			// serialization touches the MTLDevice internally (it needs the device to crack open the
			// archive's internal function representations), so nil'ing the device before the group
			// completes would crash. The block's capture list retains the archive and the NSURL, so
			// those two survive regardless; the wait only gates on the device-scoped work actually
			// finishing.
			if (serializeGroup != nullptr)
			{
				dispatch_group_wait(serializeGroup, DISPATCH_TIME_FOREVER);
#if !__has_feature(objc_arc)
				dispatch_release(serializeGroup);
#endif
			}
			mImpl->MutableBinaryArchive = nil;
			mImpl->LoadedBinaryArchive = nil;

			// Explicitly release Metal handles. Under ARC assigning nil decrements the refcount.
			for (u32 i = 0; i < GQT_COUNT; i++)
			{
				mImpl->QueueEvents[i] = nil;
				mImpl->CommandQueues[i] = nil;
			}
			mImpl->Device = nil;
		}

		id<MTLDevice> MetalGpuDevice::GetMetalDevice() const
		{
			return mImpl->Device;
		}

		id<MTLCommandQueue> MetalGpuDevice::GetMetalQueue(GpuQueueType type) const
		{
			B3D_ASSERT((u32)type < GQT_COUNT);
			return mImpl->CommandQueues[(u32)type];
		}

		void MetalGpuDevice::QueueMetalResourceForDeferredRelease(id resource, MetalGpuQueue* originQueue, u64 eventValue)
		{
			if (resource == nil)
				return;

			Impl::DeferredReleaseEntry entry;
			entry.Resource = resource;
			entry.OriginQueue = originQueue;
			entry.EventValue = eventValue;

			Lock lock(mImpl->DeferredReleaseMutex);
			mImpl->DeferredReleases.push_back(std::move(entry));
		}

		void MetalGpuDevice::BeginFrame()
		{
			// Drain the deferred-release list: any entry whose origin queue has committed past the
			// recorded event value is no longer referenced by in-flight work and can drop its strong ref.
			// Swap-and-pop erase keeps the walk O(n) without shifting the tail on every drop. Entries
			// with a null @c OriginQueue are released unconditionally (e.g. device-teardown path).
			Lock lock(mImpl->DeferredReleaseMutex);

			size_t writeIndex = 0;
			for (size_t readIndex = 0; readIndex < mImpl->DeferredReleases.size(); readIndex++)
			{
				Impl::DeferredReleaseEntry& entry = mImpl->DeferredReleases[readIndex];
				const bool ready = entry.OriginQueue == nullptr
					|| entry.OriginQueue->GetLastSignaledEventValue() >= entry.EventValue;

				if (ready)
				{
					// Dropping @c Resource nils the strong ref; the Metal runtime releases the backing.
					entry.Resource = nil;
					continue;
				}

				if (writeIndex != readIndex)
					mImpl->DeferredReleases[writeIndex] = std::move(entry);
				writeIndex++;
			}
			mImpl->DeferredReleases.resize(writeIndex);
		}

		id<MTLCounterSet> MetalGpuDevice::GetTimestampCounterSet() const
		{
			return mImpl->TimestampCounterSet;
		}

		bool MetalGpuDevice::SupportsRenderEncoderTimestamps() const
		{
			return mImpl->SupportsRenderEncoderTimestamps;
		}

		bool MetalGpuDevice::SupportsComputeEncoderTimestamps() const
		{
			return mImpl->SupportsComputeEncoderTimestamps;
		}

		bool MetalGpuDevice::SupportsBlitEncoderTimestamps() const
		{
			return mImpl->SupportsBlitEncoderTimestamps;
		}

		id<MTLBinaryArchive> MetalGpuDevice::GetLoadedBinaryArchive()
		{
			EnsureBinaryArchives();
			return mImpl->LoadedBinaryArchive;
		}

		id<MTLBinaryArchive> MetalGpuDevice::GetMutableBinaryArchive()
		{
			EnsureBinaryArchives();
			return mImpl->MutableBinaryArchive;
		}

		void MetalGpuDevice::EnsureBinaryArchives()
		{
			// Double-checked locking: once initialized, the two archive handles are read-only and
			// safe to return, so the common case takes no lock. An acquire load on the atomic flag
			// pairs with the release store at the end of the initialization body below, giving us
			// the happens-before edge that makes the archive / path writes visible to any thread
			// that observes the flag set. Avoids a mutex acquisition on every PSO compile after the
			// first, which matters because worker fibers hammer this path.
			if (mImpl->BinaryArchivesInitialized.load(std::memory_order_acquire))
				return;

#if !TARGET_IPHONE_SIMULATOR
			if (@available(macOS 11.0, iOS 14.0, *))
			{
				Lock lock(mImpl->BinaryArchivesMutex);
				// Relaxed load is fine here: we hold BinaryArchivesMutex, which provides the
				// synchronization edge from the publishing store to this read. No need for an
				// acquire fence under the lock.
				if (mImpl->BinaryArchivesInitialized.load(std::memory_order_relaxed))
					return;

				// Wrap the transient descriptors / NSString / NSURL / NSError objects created below in an
				// autorelease pool so they are reclaimed when this method returns rather than persisting
				// on whatever pool exists on the calling thread (under the fiber scheduler the first PSO
				// compile is not guaranteed to be on a thread with a runloop). Strong refs stashed into
				// mImpl survive the inner drain under both ARC (retained on assignment) and MRC (the
				// newBinaryArchiveWithDescriptor: return is assigned to a strong field).
				@autoreleasepool
				{
					mImpl->BinaryArchivePath = GetPipelineBinaryArchivePath();

					// Load the existing on-disk archive (if any) into LoadedBinaryArchive. Failures here
					// are non-fatal — the mutable archive below still lets us cache this session's compiles.
					if (FileSystem::Exists(mImpl->BinaryArchivePath))
					{
						MTLBinaryArchiveDescriptor* loadDesc = [[MTLBinaryArchiveDescriptor alloc] init];
						NSString* nsPath = @(mImpl->BinaryArchivePath.ToString().c_str());
						loadDesc.url = [NSURL fileURLWithPath:nsPath];

						NSError* loadError = nil;
						mImpl->LoadedBinaryArchive = [mImpl->Device newBinaryArchiveWithDescriptor:loadDesc error:&loadError];
						if (mImpl->LoadedBinaryArchive == nil)
						{
							NSString* desc = loadError ? [loadError localizedDescription] : @"unknown error";
							B3D_LOG(Warning, LogRenderBackend,
								"Could not load Metal pipeline binary archive from '{0}': {1}. Starting with an empty cache.",
								mImpl->BinaryArchivePath.ToString(),
								String([desc UTF8String]));
						}

#if !__has_feature(objc_arc)
						[loadDesc release];
#endif
					}

					// Always create a fresh empty mutable archive. addRenderPipelineFunctionsWithDescriptor:
					// requires an archive created *without* a URL; attempting to add to a URL-loaded archive
					// fails because the loaded form is immutable.
					MTLBinaryArchiveDescriptor* mutableDesc = [[MTLBinaryArchiveDescriptor alloc] init];
					NSError* mutableError = nil;
					mImpl->MutableBinaryArchive = [mImpl->Device newBinaryArchiveWithDescriptor:mutableDesc error:&mutableError];
					if (mImpl->MutableBinaryArchive == nil)
					{
						NSString* desc = mutableError ? [mutableError localizedDescription] : @"unknown error";
						B3D_LOG(Warning, LogRenderBackend,
							"Could not create writable Metal pipeline binary archive: {0}. Pipeline caching disabled for this session.",
							String([desc UTF8String]));
						mImpl->BinaryArchivePath = Path();
					}

#if !__has_feature(objc_arc)
					[mutableDesc release];
#endif
				}

				// Release store publishes the archive / path writes above to any fast-path acquire
				// load. Must be the final mutation of Impl before we return.
				mImpl->BinaryArchivesInitialized.store(true, std::memory_order_release);
				return;
			}
#endif
			// No binary-archive support on this platform / runtime. Flip the flag so future callers
			// short-circuit without re-entering the availability check. Release store pairs with the
			// fast-path acquire load.
			{
				Lock lock(mImpl->BinaryArchivesMutex);
				mImpl->BinaryArchivesInitialized.store(true, std::memory_order_release);
			}
		}

		bool MetalGpuDevice::Initialize()
		{
			if (mIsInitialized)
				return true;

			// Any early-return failure path below leaves mImpl partially populated — command queues
			// and their shared events may be created for queue slots 0..N while slot N+1 failed.
			// The destructor already tears everything down, but we'd still serialize an empty
			// binary archive to disk and leave mQueueInfos referencing queues whose backing
			// MTLCommandQueue is about to be nil'd. Unwind here instead so a failed Initialize
			// leaves the device identical to a freshly-constructed one.
			bool initSucceeded = false;
			ScopeGuard fnTeardown([this, &initSucceeded]()
			{
				if (initSucceeded)
					return;

				// Unwind in reverse of construction. Heap allocator goes first so any pooled MTLHeap
				// references drop before we nil the MTLDevice they were created from.
				mHeapAllocator.reset();

				for (u32 queueIndex = 0; queueIndex < GQT_COUNT; queueIndex++)
				{
					mQueueInfos[queueIndex].Queues.Clear();
					mImpl->QueueEvents[queueIndex] = nil;
					mImpl->CommandQueues[queueIndex] = nil;
				}
				mImpl->Device = nil;
			});

			// Acquire the default system Metal device. Multi-GPU enumeration via MTLCopyAllDevices
			// can be added later once the engine needs to pick a specific adapter.
			mImpl->Device = MTLCreateSystemDefaultDevice();
			if (mImpl->Device == nil)
			{
				B3D_LOG(Error, LogRenderBackend, "Failed to acquire a default Metal device. The Metal backend requires a Metal-capable GPU.");
				return false;
			}

			// Tier-2 argument buffers are a hard requirement of the backend: the parameter set path
			// encodes all bindings into argument buffers rather than the direct setBuffer/setTexture
			// slots. Tier-2 is available on Apple GPU 4+ and Mac GPU 2+.
			const bool supportsApple4 = [mImpl->Device supportsFamily:MTLGPUFamilyApple4];
			const bool supportsMac2 = [mImpl->Device supportsFamily:MTLGPUFamilyMac2];
			if (!supportsApple4 && !supportsMac2)
			{
				NSString* deviceName = [mImpl->Device name];
				B3D_LOG(Error, LogRenderBackend,
					"Metal backend requires Apple GPU 4+ or Mac GPU 2+ for Tier-2 argument buffers. "
					"Reported device '{0}' does not qualify.",
					deviceName ? String([deviceName UTF8String]) : String("<unknown>"));
				return false;
			}

			// Create one command queue per GpuQueueType. Metal exposes a single unified queue family
			// (every queue accepts graphics, compute, and blit work), so the per-type split mirrors
			// the engine's abstraction without any real affinity. Each queue owns one MTLSharedEvent
			// that MetalGpuQueue / MetalGpuCommandBuffer use for cross-queue synchronization; the
			// engine's GpuQueueMask maps onto waits on the target queue's event.
			for (u32 i = 0; i < GQT_COUNT; i++)
			{
				mImpl->CommandQueues[i] = [mImpl->Device newCommandQueue];
				if (mImpl->CommandQueues[i] == nil)
				{
					B3D_LOG(Error, LogRenderBackend, "Failed to create a Metal command queue for queue type {0}.", i);
					return false;
				}

				id<MTLSharedEvent> queueEvent = [mImpl->Device newSharedEvent];
				if (queueEvent == nil)
				{
					B3D_LOG(Error, LogRenderBackend, "Failed to create a Metal shared event for queue type {0}.", i);
					return false;
				}

				mImpl->QueueEvents[i] = queueEvent;

				mQueueInfos[i].FamilyIndex = i;
				mQueueInfos[i].Queues.Add(B3DMakeShared<MetalGpuQueue>(*this, (GpuQueueType)i, 0, mImpl->CommandQueues[i], queueEvent));
			}

			InitializeCapabilities();

			// The pipeline binary archives (LoadedBinaryArchive / MutableBinaryArchive) used to be built
			// here. They are now constructed lazily on the first pipeline compile via
			// @c EnsureBinaryArchives — see GetLoadedBinaryArchive / GetMutableBinaryArchive. This pulls
			// the NSSearchPathForDirectoriesInDomains + FileSystem::Exists + FileSystem::CreateFolder
			// cost out of device init entirely.

			// Heap allocator owns a pool of MTLHeap instances for sub-allocating textures and
			// non-volatile buffers. Constructed here (post-capability probe, post-queue creation)
			// so that hasUnifiedMemory is queryable and the device is fully usable; torn down in
			// the destructor after the deferred-release queue has been drained so heap-backed
			// resources do not outlive their parent heap.
			mHeapAllocator = B3DMakeUnique<MetalHeapAllocator>(*this);

			mIsInitialized = true;
			initSucceeded = true;
			return true;
		}

		void MetalGpuDevice::InitializeCapabilities()
		{
			// Driver version is populated from the device name/registry-id; Metal does not expose a
			// driver version string in the same way Vulkan does.
			mCapabilities.DriverVersion.Major = 1;
			mCapabilities.DriverVersion.Minor = 0;
			mCapabilities.DriverVersion.Release = 0;
			mCapabilities.DriverVersion.Build = 0;

			NSString* deviceName = [mImpl->Device name];
			mCapabilities.DeviceName = deviceName ? String([deviceName UTF8String]) : String();
			mCapabilities.DeviceVendor = ParseVendorFromDeviceName(deviceName);
			mCapabilities.BackendName = "Metal";

			// Probe Metal feature-set / GPU family. Apple4/Mac2 (Tier-2 argument buffers) is validated
			// in Initialize(); here we use finer-grained tiers to toggle optional capabilities.
			const bool supportsApple4 = [mImpl->Device supportsFamily:MTLGPUFamilyApple4];
			const bool supportsApple6 = [mImpl->Device supportsFamily:MTLGPUFamilyApple6];
			const bool supportsMac2 = [mImpl->Device supportsFamily:MTLGPUFamilyMac2];

			// All supported Metal GPUs can run compute. Geometry shaders are not supported natively on
			// Metal and the SPIRV-Cross emulation path is out of scope for this phase; we do not
			// advertise RSC_GEOMETRY_PROGRAM. Tessellation is deliberately NOT advertised here: the
			// Metal path emulates it via a compute pre-pass feeding a post-tess vertex stage, which
			// requires the SPIRV-Cross MSL rebuild (plan item F3) to emit the correct kernel/vertex
			// split. Until F3 lands, advertising RSC_TESSELLATION_PROGRAM would let BSL tessellation
			// shaders through to pipeline creation where they produce opaque Metal validation errors
			// rather than a clean "unsupported" message from the engine.
			// TODO: re-enable after F3 (MSL rebuild via SPIRV-Cross).
			mCapabilities.SetCapability(RSC_COMPUTE_PROGRAM);
			mCapabilities.SetCapability(RSC_LOAD_STORE);
			mCapabilities.SetCapability(RSC_LOAD_STORE_MSAA);

			if (supportsMac2)
				mCapabilities.SetCapability(RSC_TEXTURE_COMPRESSION_BC);
			if (supportsApple4)
			{
				mCapabilities.SetCapability(RSC_TEXTURE_COMPRESSION_ETC2);
				mCapabilities.SetCapability(RSC_TEXTURE_COMPRESSION_ASTC);
			}
			mCapabilities.SetCapability(RSC_BYTECODE_CACHING);
			mCapabilities.SetCapability(RSC_TEXTURE_VIEWS);
			mCapabilities.SetCapability(RSC_RENDER_TARGET_LAYERS);
			mCapabilities.SetCapability(RSC_MULTI_THREADED_CB);

			// Per-encoder counter sampling is orthogonal to stage-boundary sampling: the encoder-level
			// @c sampleCountersInBuffer:atSampleIndex:withBarrier: API requires the matching sampling
			// point flag (Draw / Dispatch / Blit boundary). Apple Silicon advertises only the stage
			// boundary, so calling the render-encoder variant on such devices trips Metal validation
			// even though RSC_TIMER_QUERIES is set. Cache the per-encoder flags so WriteTimestamp can
			// gate each branch and fall through to a supported encoder, or no-op with a warn-once log.
			mImpl->SupportsRenderEncoderTimestamps =
				[mImpl->Device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];
			mImpl->SupportsComputeEncoderTimestamps =
				[mImpl->Device supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary];
			mImpl->SupportsBlitEncoderTimestamps =
				[mImpl->Device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];

			// Timestamp queries need both stage-boundary counter sampling and a resolvable timestamp
			// counter set. Probe explicitly — Intel integrated GPUs on older macOS do not support this.
			// We cache the resolved counter set on Impl so the query pool can build an MTLCounterSampleBuffer
			// without re-scanning, and capture one CPU/GPU tick pair for wallclock conversion.
			if ([mImpl->Device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
			{
				for (id<MTLCounterSet> counterSet in [mImpl->Device counterSets])
				{
					if ([[counterSet name] isEqualToString:MTLCommonCounterSetTimestamp])
					{
						mImpl->TimestampCounterSet = counterSet;
						break;
					}
				}

				if (mImpl->TimestampCounterSet != nil)
				{
					mCapabilities.SetCapability(RSC_TIMER_QUERIES);

					// Two-phase calibration (no startup-cost sleep): capture the *first* CPU/GPU
					// timestamp pair now, defer the second pair to the first call of
					// ConvertTimestampToMilliseconds. This has two benefits over the previous
					// sleep-based approach — (1) the baseline between the two samples is whatever
					// elapsed time accrued organically between device init and first timer-query
					// resolution, which is typically orders of magnitude wider than 1 ms and therefore
					// yields a much more accurate GPU tick rate; (2) zero blocking in Initialize.
					MTLTimestamp cpuTs = 0, gpuTs = 0;
					[mImpl->Device sampleTimestamps:&cpuTs gpuTimestamp:&gpuTs];
					mCpuBaseTimestamp = cpuTs;
					mGpuBaseTimestamp = gpuTs;
					mImpl->FirstCpuTimestamp = cpuTs;
					mImpl->FirstGpuTimestamp = gpuTs;
					mImpl->FirstTimestampPairCaptured = true;
				}
			}

			// Ray tracing requires Apple GPU 6+ (M-series) or discrete Mac GPUs with MTLGPUFamilyMac2
			// + ray-tracing acceleration structure support. Leave this to a follow-up — we don't build
			// ray-tracing shaders in the engine yet.
			(void)supportsApple6;

			// Metal's clip-space Y axis points up — matching D3D, not Vulkan. A positive gl_Position.y
			// lands at the top of the viewport after the standard Metal viewport transform (origin
			// top-left, height extending downward). This pairs with flip_vert_y = false in the SPIRV-
			// Cross options below so the HLSL-authored BSL shaders feed their y-up clip positions
			// straight through without a compensating flip. Shader matrices cross-compiled from HLSL
			// via SPIRV-Cross remain column-major.
			mCapabilities.Conventions.NdcYAxis = GpuBackendConventions::Axis::Up;
			mCapabilities.Conventions.MatrixOrder = GpuBackendConventions::MatrixOrder::ColumnMajor;

			// Metal guarantees at least 31 vertex buffer slots and 8 color render targets on macOS 2+.
			// Note: slot 30 is reserved for the per-pipeline argument-buffer-member mapping in a future
			// S13b-style dynamic-offsets fast path; engine callers should treat 30 as an internal reserve
			// (the current code does not bind anything there, but leaves room for it).
			mCapabilities.VertexBufferCount = 31;
			mCapabilities.RenderTargetCount = 8;

			constexpr u16 textureUnitsPerStage = 16;
			for (u32 i = 0; i < GPT_COUNT; i++)
			{
				mCapabilities.SampledTexturesPerStage[i] = textureUnitsPerStage;
				mCapabilities.UniformBufferCountPerStage[i] = 16;
			}

			mCapabilities.StorageTexturesPerStage[GPT_FRAGMENT_PROGRAM] = 8;
			mCapabilities.StorageTexturesPerStage[GPT_COMPUTE_PROGRAM] = 8;

			mCapabilities.TotalSampledTexturesCount = textureUnitsPerStage * GPT_COUNT;
			mCapabilities.TotalUniformBuffersCount = 16 * GPT_COUNT;
			mCapabilities.TotalStorageTexturesCount = 16;

			mCapabilities.GeometryProgramNumOutputVertices = 1024;

			// Uniform-buffer offset alignment differs by GPU family: Apple Silicon reports 16, older
			// discrete / Intel hardware reports 256. Metal does not expose a public API to query this
			// directly, so derive from the GPU family. Per Apple's "Metal Feature Set Tables", Apple
			// family GPUs (Apple3+) align to 16, while Mac family GPUs align to 256.
			//
			// This alignment applies to *both* the plain-buffer binding path
			// (@c setVertexBuffer:offset:atIndex: etc.) and to the argument-buffer binding path used by
			// MetalGpuParameters: each buffer handle that @c MTLArgumentEncoder records via
			// @c setBuffer:offset:atIndex: must start at an offset that matches this alignment, because
			// the argument encoder writes the offset into the argument-buffer slot verbatim and the GPU
			// re-applies it at fetch time. So the value below governs the engine's GpuBufferUtility
			// sub-allocation granularity for every uniform buffer the backend binds — through a stage
			// slot *or* through an argument buffer. Structured-buffer / vertex-buffer alignment follow
			// the same family split (16 on Apple-family, 256 on Mac-family); the engine exposes only
			// one cap field, so honor the strictest case here.
			mCapabilities.MinimumUniformBufferOffsetAlignment = supportsApple4 ? 16 : 256;

			// Metal's sampler descriptor caps maxAnisotropy at 16 across every feature set documented in
			// "Metal Feature Set Tables"; the value is probed here so the sampler can clamp without
			// baking a literal into the binding code.
			mMaxSamplerAnisotropy = 16;

			mCapabilities.AddShaderProfile(kGpuProgramLanguageName);
		}

		u32 MetalGpuDevice::GetQueueCount(GpuQueueType type) const
		{
			return (u32)mQueueInfos[(u32)type].Queues.size();
		}

		TShared<GpuQueue> MetalGpuDevice::GetQueue(GpuQueueType type, u32 index) const
		{
			if (index < mQueueInfos[(u32)type].Queues.size())
				return mQueueInfos[(u32)type].Queues[index];

			return nullptr;
		}

		TShared<render::GpuCommandBufferPool> MetalGpuDevice::CreateGpuCommandBufferPool(const render::GpuCommandBufferPoolCreateInformation& createInformation)
		{
			return B3DMakeSharedFromExisting(new(B3DAllocate<MetalGpuCommandBufferPool>()) MetalGpuCommandBufferPool(*this, createInformation));
		}

		TShared<Texture> MetalGpuDevice::CreateTexture(const TextureCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			MetalTexture* rawTexture = new(B3DAllocate<MetalTexture>()) MetalTexture(*this, createInformation);

			TShared<MetalTexture> texture = flags.IsSet(GpuObjectCreateFlag::RenderThreadDestroy)
				? B3DMakeSharedFromExisting(rawTexture)
				: MakeSharedStandalone<MetalTexture>(rawTexture);

			texture->SetShared(texture);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				texture->Initialize();

			return texture;
		}

		TShared<GpuBuffer> MetalGpuDevice::CreateGpuBuffer(const GpuBufferCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			MetalGpuBuffer* rawBuffer = new(B3DAllocate<MetalGpuBuffer>()) MetalGpuBuffer(*this, createInformation);

			TShared<MetalGpuBuffer> buffer = flags.IsSet(GpuObjectCreateFlag::RenderThreadDestroy)
				? B3DMakeSharedFromExisting(rawBuffer)
				: MakeSharedStandalone<MetalGpuBuffer>(rawBuffer);

			buffer->SetShared(buffer);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				buffer->Initialize();

			return buffer;
		}

		TShared<GpuQueryPool> MetalGpuDevice::CreateQueryPool(const GpuQueryPoolCreateInformation& createInformation)
		{
			return B3DMakeShared<MetalGpuQueryPool>(*this, createInformation);
		}

		TShared<EventQuery> MetalGpuDevice::CreateEventQuery()
		{
			return B3DMakeShared<MetalEventQuery>(*this);
		}

		TShared<GpuProgram> MetalGpuDevice::CreateGpuProgram(const GpuProgramCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			TShared<MetalGpuProgram> program = B3DMakeShared<MetalGpuProgram>(*this, createInformation);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				program->Initialize();

			return program;
		}

		TShared<GpuGraphicsPipelineState> MetalGpuDevice::CreateGpuGraphicsPipelineState(const GpuGraphicsPipelineStateCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			TShared<MetalGpuGraphicsPipelineState> pipelineState = B3DMakeShared<MetalGpuGraphicsPipelineState>(*this, createInformation);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				pipelineState->Initialize();

			return pipelineState;
		}

		TShared<GpuComputePipelineState> MetalGpuDevice::CreateGpuComputePipelineState(const GpuComputePipelineStateCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			TShared<MetalGpuComputePipelineState> pipelineState = B3DMakeShared<MetalGpuComputePipelineState>(*this, createInformation);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				pipelineState->Initialize();

			return pipelineState;
		}

		TShared<GpuPipelineParameterLayout> MetalGpuDevice::CreateGpuPipelineParameterLayout(const GpuPipelineParameterLayoutCreateInformation& createInformation)
		{
			return B3DMakeShared<MetalGpuPipelineParameterLayout>(*this, createInformation);
		}

		TShared<GpuPipelineParameterSetLayout> MetalGpuDevice::CreateGpuPipelineParameterSetLayout(const GpuProgramParameterDescription& parameterDescription)
		{
			return B3DMakeShared<MetalGpuPipelineParameterSetLayout>(*this, parameterDescription);
		}

		TUnique<GpuParameterSetPool> MetalGpuDevice::CreateParameterSetPool(const GpuParameterSetPoolCreateInformation& createInformation)
		{
			return B3DMakeUnique<MetalGpuParameterSetPool>(*this, createInformation);
		}

		TShared<GpuTimelineFence> MetalGpuDevice::CreateTimelineFence()
		{
			return B3DMakeShared<MetalGpuTimelineFence>(*this);
		}

		void MetalGpuDevice::ConvertProjectionMatrix(const Matrix4& input, Matrix4& output)
		{
			// Metal clip-space matches D3D (y-up, z-[0,1]). No axis flip needed on the projection matrix itself.
			output = input;
		}

		GpuUniformBufferInformation MetalGpuDevice::GenerateUniformBufferInformation(const String& name, TArray<GpuUniformBufferMemberInformation>& inOutUniforms)
		{
			// SPIRV-Cross emits MSL with the same member layout as Vulkan GLSL's std140, so uniform
			// buffer packing here mirrors VulkanGpuDevice::GenerateUniformBufferInformation.
			GpuUniformBufferInformation bufferInfo;
			bufferInfo.Size = 0;
			bufferInfo.IsShareable = true;
			bufferInfo.Name = name;
			bufferInfo.Slot = 0;
			bufferInfo.Set = 0;

			for (auto& member : inOutUniforms)
			{
				u32 size;
				if (member.Type == GPDT_STRUCT)
				{
					// Nested structs are aligned to a vec4 boundary and rounded up to the next vec4.
					size = Math::DivideAndRoundUp(member.ElementSize, 16u) * 4;
					bufferInfo.Size = Math::DivideAndRoundUp(bufferInfo.Size, 4u) * 4;
				}
				else
					size = GpuBackendUtility::CalcStd140MemberSizeAndOffset(member.Type, member.ArraySize, bufferInfo.Size);

				member.ElementSize = size;
				member.ArrayElementStride = size;
				member.CpuOffset = bufferInfo.Size;
				member.GpuOffset = 0;
				bufferInfo.Size += size * member.ArraySize;
				member.ParentUniformBufferSlot = 0;
				member.ParentUniformBufferSet = 0;
			}

			// Total uniform-buffer size must end on a 4-byte boundary (every u32 counts as 4 bytes).
			if (bufferInfo.Size % 4 != 0)
				bufferInfo.Size += (4 - (bufferInfo.Size % 4));

			return bufferInfo;
		}

		float MetalGpuDevice::ConvertTimestampToMilliseconds(u64 timestamp)
		{
			// Timestamps returned by MetalGpuQueryPool are raw GPU ticks sampled from an MTLCounterSampleBuffer.
			// If no first-pair sample was captured at init time, timer queries are unsupported on this
			// device — report 0 ms.
			if (!mImpl->FirstTimestampPairCaptured)
				return 0.0f;

			// One-shot deferred calibration. The first call to this function samples a second CPU/GPU
			// timestamp pair and derives the GPU tick rate against the wall-clock delta since init.
			// Doing it here (instead of a fixed sleep during Initialize) gives a calibration baseline
			// as wide as whatever elapsed time accrued organically — typically many frames — which is
			// substantially more accurate than a 1 ms sleep. Guarded under a mutex because this path
			// may be reached from worker fibers concurrently.
			if (!mImpl->TimestampCalibrationDone)
			{
				Lock lock(mImpl->TimestampCalibrationMutex);
				if (!mImpl->TimestampCalibrationDone)
				{
					MTLTimestamp cpuTs2 = 0, gpuTs2 = 0;
					[mImpl->Device sampleTimestamps:&cpuTs2 gpuTimestamp:&gpuTs2];

					mach_timebase_info_data_t timebase = {};
					mach_timebase_info(&timebase);
					const double cpuTicksToNs = (double)timebase.numer / (double)timebase.denom;
					const double cpuDeltaNs = (double)(cpuTs2 - mImpl->FirstCpuTimestamp) * cpuTicksToNs;
					const double gpuDelta = (double)(gpuTs2 - mImpl->FirstGpuTimestamp);
					mGpuTicksPerNanosecond = cpuDeltaNs > 0.0 ? gpuDelta / cpuDeltaNs : 1.0;
					mImpl->TimestampCalibrationDone = true;
				}
			}

			if (mGpuTicksPerNanosecond <= 0.0)
				return 0.0f;

			const double ticksPerMillisecond = mGpuTicksPerNanosecond * 1.0e6;
			return (float)((double)timestamp / ticksPerMillisecond);
		}

		void MetalGpuDevice::PresentRenderWindow(const TShared<RenderWindow>& renderWindow, GpuQueueMask syncMask)
		{
			if (!renderWindow)
				return;

			// Presents always go through the graphics queue on Metal. Cross-queue dependencies with
			// compute/transfer work are expressed via the incoming sync mask; the queue encodes the
			// waits on the presenting command buffer.
			TShared<GpuQueue> queue = GetQueue(GQT_GRAPHICS, 0);
			if (!queue)
				return;

			queue->PresentRenderWindow(renderWindow, syncMask);
		}

		void MetalGpuDevice::WaitUntilIdle()
		{
			// Wait on each queue's shared event rather than enumerating in-flight command buffers. Each
			// queue's WaitUntilIdle blocks until that queue's last reserved event value is signaled.
			for (u32 typeIndex = 0; typeIndex < GQT_COUNT; typeIndex++)
			{
				const GpuQueueType queueType = (GpuQueueType)typeIndex;
				const u32 queueCount = GetQueueCount(queueType);
				for (u32 queueIndex = 0; queueIndex < queueCount; queueIndex++)
				{
					TShared<GpuQueue> queue = GetQueue(queueType, queueIndex);
					if (queue)
						queue->WaitUntilIdle();
				}
			}
		}

		TShared<SamplerState> MetalGpuDevice::CreateSamplerState(const SamplerStateCreateInformation& createInformation, GpuObjectCreateFlags flags)
		{
			TShared<MetalSamplerState> samplerState = B3DMakeShared<MetalSamplerState>(*this, createInformation);

			if (!flags.IsSet(GpuObjectCreateFlag::DeferredInitialize))
				samplerState->Initialize();

			return samplerState;
		}
	} // namespace render
} // namespace b3d
