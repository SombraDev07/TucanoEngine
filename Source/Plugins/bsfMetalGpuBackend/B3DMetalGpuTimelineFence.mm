//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DMetalGpuTimelineFence.h"
#include "B3DMetalGpuDevice.h"

namespace b3d
{
	namespace render
	{
		struct MetalGpuTimelineFence::Impl
		{
			id<MTLSharedEvent> Event = nil;
		};

		MetalGpuTimelineFence::MetalGpuTimelineFence(MetalGpuDevice& device)
			: mImpl(B3DMakeUnique<Impl>())
		{
			id<MTLDevice> mtlDevice = device.GetMetalDevice();
			if (mtlDevice != nil)
				mImpl->Event = [mtlDevice newSharedEvent];
		}

		MetalGpuTimelineFence::~MetalGpuTimelineFence()
		{
			if (mImpl)
				mImpl->Event = nil;
		}

		u64 MetalGpuTimelineFence::GetCompletedValue() const
		{
			if (mImpl->Event == nil)
				return 0;

			return (u64)[mImpl->Event signaledValue];
		}

		id<MTLSharedEvent> MetalGpuTimelineFence::GetSharedEvent() const
		{
			return mImpl->Event;
		}
	} // namespace render
} // namespace b3d
