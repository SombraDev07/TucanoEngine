//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DMetalEventQuery.h"
#include "B3DMetalGpuDevice.h"
#include "B3DMetalGpuCommandBuffer.h"

namespace b3d
{
	namespace render
	{
		struct MetalEventQuery::Impl
		{
			id<MTLSharedEvent> Event = nil;
		};

		MetalEventQuery::MetalEventQuery(MetalGpuDevice& gpuDevice)
			: mGpuDevice(gpuDevice)
			, mImpl(B3DMakeUnique<Impl>())
		{
			id<MTLDevice> device = gpuDevice.GetMetalDevice();
			if (device != nil)
				mImpl->Event = [device newSharedEvent];
		}

		MetalEventQuery::~MetalEventQuery()
		{
			if (mImpl)
				mImpl->Event = nil;
		}

		void MetalEventQuery::Begin(GpuCommandBuffer& commandBuffer)
		{
			if (mImpl->Event == nil)
				return;

			auto& metalCB = static_cast<MetalGpuCommandBuffer&>(commandBuffer);
			id<MTLCommandBuffer> mtlCB = metalCB.GetOrAcquireMetalCommandBuffer();
			if (mtlCB == nil)
				return;

			// Signal is encoded at the current stream position, which matches the base-class contract
			// (B3DEventQuery.h: "Once the GPU reaches this point the query will be set in the signaled
			// state"). The Vulkan backend has identical semantics: VulkanEventQuery::Begin issues a
			// @c vkCmdSetEvent at the call site (or queues it until after the current render pass
			// ends). A review suggestion to move this signal to commit time would make @c IsReady
			// return false until the whole buffer retires rather than until the GPU reached the Begin
			// call — that is a meaning change away from the base contract, so we keep the per-call
			// mid-stream signal. Metal @c encodeSignalEvent: is a command-buffer-level API and must
			// not be called while an encoder is open; callers on the Metal path schedule EventQuery
			// begins between encoder boundaries so the call is safe. No stricter assertion exists on
			// the base class, matching Vulkan's looser contract.
			const u64 nextValue = mExpectedValue.fetch_add(1, std::memory_order_relaxed) + 1;
			[mtlCB encodeSignalEvent:mImpl->Event value:nextValue];
		}

		bool MetalEventQuery::IsReady() const
		{
			if (mImpl->Event == nil)
				return true;

			const u64 target = mExpectedValue.load(std::memory_order_relaxed);
			return mImpl->Event.signaledValue >= target;
		}
	} // namespace render
} // namespace b3d
