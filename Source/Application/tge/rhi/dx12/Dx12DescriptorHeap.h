#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cassert>

// Fixed-capacity descriptor heap with free-list slot allocation. One instance
// per heap type (RTV, DSV, CBV/SRV/UAV, Sampler) on the DX12 device. Slot
// indices are what rhi::Handle::index maps to for RtvHandle/DsvHandle/
// SrvHandle/UavHandle/SamplerHandle -- the heap itself never grows, sized
// generously up front (matches how the DX11 backend's Pool<T> never shrinks
// either, just via a fixed descriptor table size instead of a std::vector).
namespace Tga::rhi::dx12
{
	using Microsoft::WRL::ComPtr;

	class Dx12DescriptorHeap
	{
	public:
		void Init(ID3D12Device* aDevice, D3D12_DESCRIPTOR_HEAP_TYPE aType, uint32_t aCapacity, bool aShaderVisible)
		{
			myType = aType;
			myCapacity = aCapacity;
			myShaderVisible = aShaderVisible;

			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = aType;
			desc.NumDescriptors = aCapacity;
			desc.Flags = aShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			HRESULT hr = aDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(myHeap.GetAddressOf()));
			assert(SUCCEEDED(hr)); (void)hr;

			myDescriptorSize = aDevice->GetDescriptorHandleIncrementSize(aType);
			myCpuStart = myHeap->GetCPUDescriptorHandleForHeapStart();
			if (aShaderVisible)
				myGpuStart = myHeap->GetGPUDescriptorHandleForHeapStart();
		}

		// Slot 0 is reserved (never handed out) so index 0 can mean "null",
		// matching rhi::Handle's {index=0} == invalid convention.
		uint32_t Allocate()
		{
			if (!myReservedSlotZero) { myReservedSlotZero = true; myNextFree = 1; }
			uint32_t idx;
			if (!myFreeList.empty()) { idx = myFreeList.back(); myFreeList.pop_back(); }
			else { assert(myNextFree < myCapacity && "Dx12DescriptorHeap exhausted -- raise its Init() capacity"); idx = myNextFree++; }
			return idx;
		}

		void Free(uint32_t aSlot)
		{
			if (aSlot == 0 || aSlot >= myCapacity) return;
			myFreeList.push_back(aSlot);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t aSlot) const
		{
			D3D12_CPU_DESCRIPTOR_HANDLE h = myCpuStart;
			h.ptr += static_cast<SIZE_T>(aSlot) * myDescriptorSize;
			return h;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t aSlot) const
		{
			assert(myShaderVisible);
			D3D12_GPU_DESCRIPTOR_HANDLE h = myGpuStart;
			h.ptr += static_cast<UINT64>(aSlot) * myDescriptorSize;
			return h;
		}

		ID3D12DescriptorHeap* Heap() const { return myHeap.Get(); }
		uint32_t DescriptorSize() const { return myDescriptorSize; }

	private:
		ComPtr<ID3D12DescriptorHeap> myHeap;
		D3D12_DESCRIPTOR_HEAP_TYPE myType = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		D3D12_CPU_DESCRIPTOR_HANDLE myCpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE myGpuStart{};
		uint32_t myDescriptorSize = 0;
		uint32_t myCapacity = 0;
		uint32_t myNextFree = 1;
		bool myShaderVisible = false;
		bool myReservedSlotZero = false;
		std::vector<uint32_t> myFreeList;
	};
}
