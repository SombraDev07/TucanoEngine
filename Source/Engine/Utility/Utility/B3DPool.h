//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "Utility/B3DTChunkedArray.h"

namespace b3d
{
	/** @addtogroup Containers
	 *  @{
	 */

	/**
	 * A pool for allocating same-sized objects with stable addresses. Backed by a TChunkedArray so growth never moves
	 * existing elements, which means pointers handed out by Allocate() remain valid for the element's entire lifetime.
	 *
	 * Free slots are tracked by an intrusive singly-linked list threaded through the storage itself — the unused bytes
	 * of a released slot hold the pointer to the next free slot, so no separate free-list allocation is needed. Because
	 * the link is overlaid on the element storage, the element type must be at least pointer-sized.
	 *
	 * Allocate() constructs the element and Release() destructs it. All outstanding elements must be released before the
	 * pool is destroyed. The pool is not thread-safe; callers that share it across threads must synchronise externally.
	 *
	 * @tparam Type			Element type. Must be at least the size of a pointer.
	 * @tparam PageSize		Number of elements per storage page. Must be a power of two.
	 */
	template<typename Type, u64 PageSize = 64>
	class TPool final
	{
		static_assert(sizeof(Type) >= sizeof(void*),
			"TPool element size must be at least pointer-sized to hold the intrusive free-list link.");

		/** Storage for a single element, reinterpreted as a free-list link while the slot is unused. */
		union Slot
		{
			alignas(Type) u8 Storage[sizeof(Type)];
			Slot* NextFree;
		};

	public:
		TPool() = default;

		~TPool()
		{
			B3D_ASSERT(mAllocatedCount == 0 && "Not all elements were released back to the pool.");
		}

		TPool(const TPool&) = delete;
		TPool& operator=(const TPool&) = delete;

		/** Allocates an element from the pool and constructs it in-place with the provided arguments. */
		template<typename... Args>
		Type* Allocate(Args&&... args)
		{
			Slot* slot;
			if(mFreeHead != nullptr)
			{
				slot = mFreeHead;
				mFreeHead = slot->NextFree;
			}
			else
				slot = &mStorage.EmplaceBack();

			++mAllocatedCount;
			return new(slot->Storage) Type(std::forward<Args>(args)...);
		}

		/** Destructs the element and returns its slot to the pool for reuse. */
		void Release(Type* ptr)
		{
			B3D_ASSERT(ptr != nullptr);
			B3D_ASSERT(mAllocatedCount > 0);

			ptr->~Type();

			Slot* slot = reinterpret_cast<Slot*>(ptr);
			slot->NextFree = mFreeHead;
			mFreeHead = slot;
			--mAllocatedCount;
		}

		/** Returns the number of elements currently allocated (not returned to the pool). */
		u32 GetAllocatedCount() const { return mAllocatedCount; }

	private:
		TChunkedArray<Slot, PageSize> mStorage;
		Slot* mFreeHead = nullptr;
		u32 mAllocatedCount = 0;
	};

	/** @} */
} // namespace b3d
