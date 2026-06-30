//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include <iterator>
#include <utility>

namespace b3d
{
	/** @addtogroup Containers
	 *  @{
	 */

	/**
	 * A dynamically-growing array that stores elements in fixed-size pages (chunks) rather than a single contiguous
	 * buffer. Growth only allocates a new page — existing data is never copied or moved. This makes it ideal for
	 * large arrays that grow incrementally.
	 *
	 * @note PageSize must be a power of two so that index calculations can use bitwise operations.
	 *
	 * @tparam Type			Element type.
	 * @tparam PageSize		Number of elements per page. Must be a power of two.
	 */
	template<typename Type, u64 PageSize = 1024>
	class TChunkedArray final
	{
		static_assert((PageSize & (PageSize - 1)) == 0, "PageSize must be a power of two.");
		static_assert(PageSize > 0, "PageSize must be greater than zero.");

		static constexpr u64 kPageBits = []() constexpr
		{
			u64 bits = 0;
			u64 value = PageSize;
			while(value >>= 1)
				++bits;
			return bits;
		}();

		static constexpr u64 kPageMask = PageSize - 1;

	public:
		using value_type = Type;

		/** @name Internal
		 *  @{
		 */

		/** Random-access iterator for TChunkedArray. */
		template<bool IsConst>
		class TIterator
		{
		public:
			using iterator_category = std::random_access_iterator_tag;
			using value_type = Type;
			using difference_type = i64;
			using pointer = std::conditional_t<IsConst, const Type*, Type*>;
			using reference = std::conditional_t<IsConst, const Type&, Type&>;

			using ContainerPtr = std::conditional_t<IsConst, const TChunkedArray*, TChunkedArray*>;

			TIterator() = default;
			TIterator(ContainerPtr container, u64 index)
				: mContainer(container), mIndex(index) {}

			// Allow conversion from non-const to const iterator
			template<bool OtherConst, typename = std::enable_if_t<IsConst && !OtherConst>>
			TIterator(const TIterator<OtherConst>& other)
				: mContainer(other.mContainer), mIndex(other.mIndex) {}

			reference operator*() const { return (*mContainer)[mIndex]; }
			pointer operator->() const { return &(*mContainer)[mIndex]; }
			reference operator[](difference_type offset) const { return (*mContainer)[mIndex + offset]; }

			TIterator& operator++() { ++mIndex; return *this; }
			TIterator operator++(int) { TIterator temp = *this; ++mIndex; return temp; }
			TIterator& operator--() { --mIndex; return *this; }
			TIterator operator--(int) { TIterator temp = *this; --mIndex; return temp; }

			TIterator& operator+=(difference_type offset) { mIndex += offset; return *this; }
			TIterator& operator-=(difference_type offset) { mIndex -= offset; return *this; }

			TIterator operator+(difference_type offset) const { return TIterator(mContainer, mIndex + offset); }
			TIterator operator-(difference_type offset) const { return TIterator(mContainer, mIndex - offset); }

			difference_type operator-(const TIterator& other) const { return (i64)mIndex - (i64)other.mIndex; }

			bool operator==(const TIterator& other) const { return mIndex == other.mIndex; }
			bool operator!=(const TIterator& other) const { return mIndex != other.mIndex; }
			bool operator<(const TIterator& other) const { return mIndex < other.mIndex; }
			bool operator>(const TIterator& other) const { return mIndex > other.mIndex; }
			bool operator<=(const TIterator& other) const { return mIndex <= other.mIndex; }
			bool operator>=(const TIterator& other) const { return mIndex >= other.mIndex; }

		private:
			template<bool> friend class TIterator;

			ContainerPtr mContainer = nullptr;
			u64 mIndex = 0;
		};

		/** @} */

		using Iterator = TIterator<false>;
		using ConstIterator = TIterator<true>;
		using ReverseIterator = std::reverse_iterator<Iterator>;
		using ConstReverseIterator = std::reverse_iterator<ConstIterator>;

		// std-compatible aliases
		using iterator = Iterator;
		using const_iterator = ConstIterator;
		using reverse_iterator = ReverseIterator;
		using const_reverse_iterator = ConstReverseIterator;

		TChunkedArray() = default;

		TChunkedArray(const TChunkedArray& other)
		{
			if(!other.Empty())
				*this = other;
		}

		TChunkedArray(TChunkedArray&& other) noexcept
		{
			if(!other.Empty())
				*this = std::move(other);
		}

		TChunkedArray(u64 size, const Type& value = Type())
		{
			EnsureCapacity(size);
			FillConstructRange(0, size, value);
			mSize = size;
		}

		TChunkedArray(std::initializer_list<Type> list)
		{
			const u64 count = (u64)list.size();
			EnsureCapacity(count);

			auto it = list.begin();
			ForEachPageRange(0, count, [&it](Type* ptr, u64 segmentCount)
			{
				std::uninitialized_copy(it, it + segmentCount, ptr);
				it += segmentCount;
			});

			mSize = count;
		}

		~TChunkedArray()
		{
			DestructRange(0, mSize);
			FreeAllPages();
		}

		TChunkedArray& operator=(const TChunkedArray& other)
		{
			if(this == &other)
				return *this;

			u64 mySize = mSize;
			const u64 otherSize = other.mSize;

			if(mySize > otherSize)
			{
				if(otherSize > 0)
					CopyAssignRange(other, 0, otherSize);

				DestructRange(otherSize, mySize);
			}
			else
			{
				if(mySize > 0)
					CopyAssignRange(other, 0, mySize);

				EnsureCapacity(otherSize);
				CopyConstructRange(other, mySize, otherSize);
			}

			mSize = otherSize;
			return *this;
		}

		TChunkedArray& operator=(TChunkedArray&& other) noexcept
		{
			if(this == &other)
				return *this;

			DestructRange(0, mSize);
			FreeAllPages();

			mPages = std::exchange(other.mPages, {});
			mSize = std::exchange(other.mSize, 0);

			return *this;
		}

		/** Returns a reference to the element at the given index. No bounds checking. */
		Type& operator[](u64 index)
		{
			return mPages[index >> kPageBits][index & kPageMask];
		}

		/** Returns a const reference to the element at the given index. No bounds checking. */
		const Type& operator[](u64 index) const
		{
			return mPages[index >> kPageBits][index & kPageMask];
		}

		Iterator Begin() { return Iterator(this, 0); }
		Iterator End() { return Iterator(this, mSize); }
		ConstIterator Begin() const { return ConstIterator(this, 0); }
		ConstIterator End() const { return ConstIterator(this, mSize); }

		ReverseIterator RBegin() { return ReverseIterator(End()); }
		ReverseIterator REnd() { return ReverseIterator(Begin()); }
		ConstReverseIterator RBegin() const { return ConstReverseIterator(End()); }
		ConstReverseIterator REnd() const { return ConstReverseIterator(Begin()); }

		/** Appends an element by copy. */
		void Add(const Type& value)
		{
			EnsureCapacity(mSize + 1);
			new(&GetElement(mSize)) Type(value);
			++mSize;
		}

		/** Appends an element by move. */
		void Add(Type&& value)
		{
			EnsureCapacity(mSize + 1);
			new(&GetElement(mSize)) Type(std::move(value));
			++mSize;
		}

		/** Constructs an element in-place at the end. */
		template<typename... Args>
		Type& EmplaceBack(Args&&... args)
		{
			EnsureCapacity(mSize + 1);
			Type* element = new(&GetElement(mSize)) Type(std::forward<Args>(args)...);
			++mSize;
			return *element;
		}

		/** Removes and destructs the last element. */
		void Pop()
		{
			B3D_ASSERT(mSize > 0);
			--mSize;
			GetElement(mSize).~Type();
		}

		/** Returns the number of elements. */
		u64 Size() const { return mSize; }

		/** Returns true if the array is empty. */
		bool Empty() const { return mSize == 0; }

		/** Returns the current capacity (number of allocated element slots). */
		u64 Capacity() const { return (u64)mPages.Size() * PageSize; }

		/**
		 * Resizes the array. New elements are value-initialized. If the new size is smaller, trailing elements
		 * are destructed. Pages are not freed (use Shrink() for that).
		 */
		void Resize(u64 newSize)
		{
			if(newSize > mSize)
			{
				EnsureCapacity(newSize);
				DefaultConstructRange(mSize, newSize);
			}
			else
				DestructRange(newSize, mSize);

			mSize = newSize;
		}

		/** Resizes the array, initializing new elements with @p value. */
		void Resize(u64 newSize, const Type& value)
		{
			if(newSize > mSize)
			{
				EnsureCapacity(newSize);
				FillConstructRange(mSize, newSize, value);
			}
			else
				DestructRange(newSize, mSize);

			mSize = newSize;
		}

		/** Pre-allocates pages to hold at least @p capacity elements. Does not change size. */
		void Reserve(u64 capacity)
		{
			EnsureCapacity(capacity);
		}

		/** Frees any unused trailing pages beyond what is needed for the current size. */
		void Shrink()
		{
			const u64 requiredPages = (mSize + PageSize - 1) >> kPageBits;
			for(u64 pageIndex = requiredPages; pageIndex < (u64)mPages.Size(); ++pageIndex)
				B3DFree(mPages[pageIndex]);

			mPages.Resize(requiredPages);
			mPages.Shrink();
		}

		/** Destructs all elements and frees all pages. */
		void Clear()
		{
			DestructRange(0, mSize);
			FreeAllPages();
			mSize = 0;
		}

		// std-compatible interface
		void push_back(const Type& value) { Add(value); }
		void push_back(Type&& value) { Add(std::move(value)); }
		void pop_back() { Pop(); }
		u64 size() const { return mSize; }
		bool empty() const { return mSize == 0; }
		void resize(u64 newSize) { Resize(newSize); }
		void resize(u64 newSize, const Type& value) { Resize(newSize, value); }
		void reserve(u64 capacity) { Reserve(capacity); }

		// Iterators
		iterator begin() { return Begin(); }
		iterator end() { return End(); }
		const_iterator begin() const { return Begin(); }
		const_iterator end() const { return End(); }

		reverse_iterator rbegin() { return RBegin(); }
		reverse_iterator rend() { return REnd(); }
		const_reverse_iterator rbegin() const { return RBegin(); }
		const_reverse_iterator rend() const { return REnd(); }

	private:
		/** Returns a reference to the element at index (raw, no construction/destruction). */
		Type& GetElement(u64 index)
		{
			return mPages[index >> kPageBits][index & kPageMask];
		}

		const Type& GetElement(u64 index) const
		{
			return mPages[index >> kPageBits][index & kPageMask];
		}

		/** Ensures enough pages are allocated to hold at least @p requiredSize elements. */
		void EnsureCapacity(u64 requiredSize)
		{
			const u64 requiredPages = (requiredSize + PageSize - 1) >> kPageBits;
			while((u64)mPages.Size() < requiredPages)
				mPages.Add(B3DAllocateMultiple<Type>(PageSize));
		}

		/** Frees all page allocations and clears the page array. Does not destruct elements. */
		void FreeAllPages()
		{
			for(u64 pageIndex = 0; pageIndex < (u64)mPages.Size(); ++pageIndex)
				B3DFree(mPages[pageIndex]);

			mPages.Clear();
		}

		/**
		 * Iterates over the page sub-ranges that cover [startIndex, endIndex) and calls @p fn(Type* pagePtr, u64 count)
		 * for each contiguous segment within a page.
		 */
		template<typename Fn>
		void ForEachPageRange(u64 startIndex, u64 endIndex, Fn&& fn)
		{
			while(startIndex < endIndex)
			{
				const u64 page = startIndex >> kPageBits;
				const u64 offset = startIndex & kPageMask;
				const u64 count = std::min(PageSize - offset, endIndex - startIndex);

				fn(mPages[page] + offset, count);
				startIndex += count;
			}
		}

		/** Const version of ForEachPageRange. */
		template<typename Fn>
		void ForEachPageRange(u64 startIndex, u64 endIndex, Fn&& fn) const
		{
			while(startIndex < endIndex)
			{
				const u64 page = startIndex >> kPageBits;
				const u64 offset = startIndex & kPageMask;
				const u64 count = std::min(PageSize - offset, endIndex - startIndex);

				fn(mPages[page] + offset, count);
				startIndex += count;
			}
		}

		/** Destructs elements in the range [startIndex, endIndex) using per-page iteration. */
		void DestructRange(u64 startIndex, u64 endIndex)
		{
			ForEachPageRange(startIndex, endIndex, [](Type* ptr, u64 count)
			{
				for(u64 elementIndex = 0; elementIndex < count; ++elementIndex)
					ptr[elementIndex].~Type();
			});
		}

		/** Copy-constructs elements from @p other in the range [startIndex, endIndex) into uninitialized memory. */
		void CopyConstructRange(const TChunkedArray& other, u64 startIndex, u64 endIndex)
		{
			u64 current = startIndex;
			while(current < endIndex)
			{
				const u64 page = current >> kPageBits;
				const u64 offset = current & kPageMask;
				const u64 count = std::min(PageSize - offset, endIndex - current);

				std::uninitialized_copy(other.mPages[page] + offset, other.mPages[page] + offset + count, mPages[page] + offset);
				current += count;
			}
		}

		/** Copy-assigns elements from @p other in the range [startIndex, endIndex) over already-constructed elements. */
		void CopyAssignRange(const TChunkedArray& other, u64 startIndex, u64 endIndex)
		{
			u64 current = startIndex;
			while(current < endIndex)
			{
				const u64 page = current >> kPageBits;
				const u64 offset = current & kPageMask;
				const u64 count = std::min(PageSize - offset, endIndex - current);

				std::copy(other.mPages[page] + offset, other.mPages[page] + offset + count, mPages[page] + offset);
				current += count;
			}
		}

		/** Value-initializes (default-constructs) elements in the range [startIndex, endIndex) in uninitialized memory. */
		void DefaultConstructRange(u64 startIndex, u64 endIndex)
		{
			ForEachPageRange(startIndex, endIndex, [](Type* ptr, u64 count)
			{
				std::uninitialized_value_construct_n(ptr, count);
			});
		}

		/** Copy-constructs elements in the range [startIndex, endIndex) from @p value in uninitialized memory. */
		void FillConstructRange(u64 startIndex, u64 endIndex, const Type& value)
		{
			ForEachPageRange(startIndex, endIndex, [&value](Type* ptr, u64 count)
			{
				std::uninitialized_fill_n(ptr, count, value);
			});
		}

		TArray<Type*> mPages;
		u64 mSize = 0;
	};

	/** @} */
} // namespace b3d
