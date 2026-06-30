//************************************ B3D Framework - Copyright 2018 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "D3D12MemAlloc.h"

#ifndef B3D_USE_D3D12MA
// Stub implementation when D3D12MA is not available
// Falls back to CreateCommittedResource

namespace D3D12MA
{
	/** Stub allocation implementation using committed resources */
	class StubAllocation : public Allocation
	{
	public:
		StubAllocation(ID3D12Resource* resource)
			: mResource(resource)
		{
			if (mResource)
				mResource->AddRef();
		}

		~StubAllocation() override
		{
			if (mResource)
			{
				mResource->Release();
				mResource = nullptr;
			}
		}

		ID3D12Resource* GetResource() const override
		{
			return mResource;
		}

		void Release() override
		{
			delete this;
		}

	private:
		ID3D12Resource* mResource = nullptr;
	};

	/** Stub allocator implementation using CreateCommittedResource */
	class StubAllocator : public Allocator
	{
	public:
		StubAllocator(ID3D12Device* device)
			: mDevice(device)
		{
			if (mDevice)
				mDevice->AddRef();
		}

		~StubAllocator() override
		{
			if (mDevice)
			{
				mDevice->Release();
				mDevice = nullptr;
			}
		}

		HRESULT CreateResource(
			const ALLOCATION_DESC* pAllocDesc,
			const D3D12_RESOURCE_DESC* pResourceDesc,
			D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE* pOptimizedClearValue,
			Allocation** ppAllocation,
			REFIID riidResource,
			void** ppvResource) override
		{
			if (!mDevice || !pAllocDesc || !pResourceDesc || !ppAllocation || !ppvResource)
				return E_INVALIDARG;

			// Convert to D3D12 heap properties
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = pAllocDesc->HeapType;
			heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapProps.CreationNodeMask = 0;
			heapProps.VisibleNodeMask = 0;

			// Create committed resource
			ID3D12Resource* resource = nullptr;
			HRESULT hr = mDevice->CreateCommittedResource(
				&heapProps,
				pAllocDesc->ExtraHeapFlags,
				pResourceDesc,
				InitialResourceState,
				pOptimizedClearValue,
				riidResource,
				ppvResource
			);

			if (FAILED(hr))
				return hr;

			// Create allocation wrapper
			resource = static_cast<ID3D12Resource*>(*ppvResource);
			*ppAllocation = new StubAllocation(resource);

			return S_OK;
		}

		void Release() override
		{
			delete this;
		}

	private:
		ID3D12Device* mDevice = nullptr;
	};

	HRESULT CreateAllocator(const ALLOCATOR_DESC* pDesc, Allocator** ppAllocator)
	{
		if (!pDesc || !pDesc->pDevice || !ppAllocator)
			return E_INVALIDARG;

		*ppAllocator = new StubAllocator(pDesc->pDevice);
		return S_OK;
	}

} // namespace D3D12MA

#endif // !B3D_USE_D3D12MA
