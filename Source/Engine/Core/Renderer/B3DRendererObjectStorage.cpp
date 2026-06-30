//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Renderer/B3DRendererObjectStorage.h"
#include "Allocators/B3DFrameAllocator.h"

namespace b3d
{
	RendererId RendererObjectStorage::AllocateRendererId()
	{
		RendererId objectId = mObjectIdAllocator.Allocate();
		mPendingAllocations.insert(objectId);
		return objectId;
	}

	void RendererObjectStorage::DeallocateRendererId(RendererId objectId)
	{
		auto found = mPendingAllocations.find(objectId);
		if(found != mPendingAllocations.end())
			mPendingAllocations.erase(found);
		else
			mDeallocations.Add(objectId);

		mObjectIdAllocator.Deallocate(objectId);
	}

	RendererObjectStorage::CommandBatch RendererObjectStorage::FlushCommands(FrameAllocator& allocator)
	{
		const u32 deallocationCount = (u32)mDeallocations.size();
		const u32 allocationCount = (u32)mPendingAllocations.size();

		if(deallocationCount == 0 && allocationCount == 0)
			return {};

		CommandBatch result;
		if(deallocationCount > 0)
		{
			RendererId* ids = reinterpret_cast<RendererId*>(allocator.AllocateAligned(sizeof(RendererId) * deallocationCount, alignof(RendererId)));
			for(u32 index = 0; index < deallocationCount; ++index)
				ids[index] = mDeallocations[index];

			result.DeallocatedIds = TArrayView<const RendererId>(ids, deallocationCount);
		}

		if(allocationCount > 0)
		{
			RendererId* ids = reinterpret_cast<RendererId*>(allocator.AllocateAligned(sizeof(RendererId) * allocationCount, alignof(RendererId)));
			u32 index = 0;
			for(const RendererId& pendingId : mPendingAllocations)
				ids[index++] = pendingId;

			result.AllocatedIds = TArrayView<const RendererId>(ids, allocationCount);
		}

		mDeallocations.clear();
		mPendingAllocations.clear();

		return result;
	}
} // namespace b3d
