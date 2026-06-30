//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DGpuCompletionTracker.h"
#include "CoreObject/B3DRenderThread.h"

using namespace b3d;

bool GpuFrameCompletionTracker::IsMarkerComplete(u64 marker) const
{
	const u64 currentFrame = mFrameIndex.load(std::memory_order_acquire);
	return marker + RenderThread::kMaximumFramesInFlight <= currentFrame;
}
