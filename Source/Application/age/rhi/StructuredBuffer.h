#pragma once
#include <array>
#include "age/rhi/Device.h"

namespace Ag::rhi
{
	// A structured GPU buffer plus its SRV (and optionally a UAV for compute
	// read/write). Replaces the scattered "D3D11_BUFFER_DESC + CreateBuffer +
	// D3D11_SHADER_RESOURCE_VIEW_DESC + CreateShaderResourceView (+ UAV)"
	// boilerplate with one type -- one place to change for DX12.
	//
	// Two flavours: CPU-updated (Upload memory, with frame-owned backing
	// resources on DX12) for buffers the CPU fills every frame (lights, local
	// shadow transforms), or GPU-only (Default memory, no `Update()`) for
	// buffers a compute shader writes via its UAV (cluster lists, GI SH
	// coefficients).
	class StructuredBuffer
	{
	public:
		void Create(IDevice& aDevice, uint32_t aElementStride, uint32_t aElementCount,
		            bool aWithUav, bool aCpuUpdatable, const char* aDebugName)
		{
			Destroy(aDevice);

			BufferDesc bd = {};
			bd.byteSize  = (uint64_t)aElementStride * aElementCount;
			bd.stride    = aElementStride;
			bd.usage     = BufferUsage::Structured | (aWithUav ? BufferUsage::UAV : BufferUsage::None);
			bd.memory    = aCpuUpdatable ? MemoryType::Upload : MemoryType::Default;
			bd.debugName = aDebugName;
			myDevice = &aDevice;
			myCpuUpdatable = aCpuUpdatable;
			mySlotCount = (aCpuUpdatable && aDevice.GetBackend() == Backend::DX12) ? kDx12SlotCount : 1;
			myCapacity = bd.byteSize;
			for (uint32_t i = 0; i < mySlotCount; ++i)
			{
				myBuffers[i] = aDevice.CreateBuffer(bd);
				if (!myBuffers[i].IsValid())
					continue;

				SrvDesc sd = {};
				sd.bufferType = BufferSrvType::Structured;
				sd.bufferNumElements = aElementCount;
				mySrvs[i] = aDevice.CreateSrv(myBuffers[i], sd);

				if (aWithUav)
				{
					UavDesc ud = {};
					ud.bufferNumElements = aElementCount;
					myUavs[i] = aDevice.CreateUav(myBuffers[i], ud);
				}
			}
		}

		void Destroy(IDevice& aDevice)
		{
			for (uint32_t i = 0; i < mySlotCount; ++i)
			{
				if (myUavs[i].IsValid())    aDevice.Destroy(myUavs[i]);
				if (mySrvs[i].IsValid())    aDevice.Destroy(mySrvs[i]);
				if (myBuffers[i].IsValid()) aDevice.Destroy(myBuffers[i]);
				myUavs[i] = {}; mySrvs[i] = {}; myBuffers[i] = {};
			}
			myDevice = nullptr;
			mySlotCount = 0;
			myCapacity = 0;
			myActiveSlot = 0;
			myLastFrame = ~0u;
			myUpdatesThisFrame = 0;
		}

		bool IsValid() const { return myBuffers[myActiveSlot].IsValid() && mySrvs[myActiveSlot].IsValid(); }

		BufferHandle Handle() const { return myBuffers[myActiveSlot]; }
		SrvHandle    Srv() const    { return mySrvs[myActiveSlot]; }
		UavHandle    Uav() const    { return myUavs[myActiveSlot]; }

		// CPU-updated buffers only (created with aCpuUpdatable = true).
		void Update(ICommandContext& aCtx, const void* aData, uint32_t aByteSize)
		{
			if (!myCpuUpdatable || !myDevice)
				return;
			const uint32_t frame = myDevice->GetFrameIndex();
			if (frame != myLastFrame)
			{
				myLastFrame = frame;
				myUpdatesThisFrame = 0;
			}
			const uint32_t frameBase = (frame % kDx12FramesInFlight) * kUpdatesPerFrame;
			const uint32_t update = mySlotCount > 1
				? (myUpdatesThisFrame < kUpdatesPerFrame ? myUpdatesThisFrame++ : kUpdatesPerFrame - 1)
				: 0;
			myActiveSlot = mySlotCount > 1 ? frameBase + update : 0;
			aCtx.UpdateBuffer(myBuffers[myActiveSlot], aData, aByteSize < myCapacity ? aByteSize : (uint32_t)myCapacity);
		}

	private:
		// Structured lighting data is normally updated once, but the local-shadow
		// pass patches light slots and submits a second version.  As with CBVs,
		// each version needs its own DX12 resource until the frame's fence retires.
		static constexpr uint32_t kDx12FramesInFlight = 3; // must equal Dx12Device::kFramesInFlight
		static constexpr uint32_t kUpdatesPerFrame = 4;
		static constexpr uint32_t kDx12SlotCount = kDx12FramesInFlight * kUpdatesPerFrame;
		std::array<BufferHandle, kDx12SlotCount> myBuffers = {};
		std::array<SrvHandle, kDx12SlotCount> mySrvs = {};
		std::array<UavHandle, kDx12SlotCount> myUavs = {};
		IDevice* myDevice = nullptr;
		uint64_t myCapacity = 0;
		uint32_t mySlotCount = 0;
		uint32_t myActiveSlot = 0;
		uint32_t myLastFrame = ~0u;
		uint32_t myUpdatesThisFrame = 0;
		bool myCpuUpdatable = false;
	};
}
