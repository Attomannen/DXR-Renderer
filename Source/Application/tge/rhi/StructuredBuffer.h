#pragma once
#include "tge/rhi/Device.h"

namespace Tga::rhi
{
	// A structured GPU buffer plus its SRV (and optionally a UAV for compute
	// read/write). Replaces the scattered "D3D11_BUFFER_DESC + CreateBuffer +
	// D3D11_SHADER_RESOURCE_VIEW_DESC + CreateShaderResourceView (+ UAV)"
	// boilerplate with one type -- one place to change for DX12.
	//
	// Two flavours: CPU-updated (Upload memory, `Update()` does a
	// Map(WRITE_DISCARD)-equivalent via IDevice::AllocateDynamicConstants-style
	// upload) for buffers the CPU fills every frame (lights, local shadow
	// transforms), or GPU-only (Default memory, no `Update()`) for buffers a
	// compute shader writes via its UAV (cluster lists, GI SH coefficients).
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
			myBuffer = aDevice.CreateBuffer(bd);
			if (!myBuffer.IsValid())
				return;

			SrvDesc sd = {};
			sd.asStructuredOrRaw = true;
			sd.bufferNumElements = aElementCount;
			mySrv = aDevice.CreateSrv(myBuffer, sd);

			if (aWithUav)
			{
				UavDesc ud = {};
				ud.bufferNumElements = aElementCount;
				myUav = aDevice.CreateUav(myBuffer, ud);
			}
		}

		void Destroy(IDevice& aDevice)
		{
			if (myUav.IsValid())    aDevice.Destroy(myUav);
			if (mySrv.IsValid())    aDevice.Destroy(mySrv);
			if (myBuffer.IsValid()) aDevice.Destroy(myBuffer);
			myUav = {}; mySrv = {}; myBuffer = {};
		}

		bool IsValid() const { return myBuffer.IsValid() && mySrv.IsValid(); }

		BufferHandle Handle() const { return myBuffer; }
		SrvHandle    Srv() const    { return mySrv; }
		UavHandle    Uav() const    { return myUav; }

		// CPU-updated buffers only (created with aCpuUpdatable = true).
		void Update(ICommandContext& aCtx, const void* aData, uint32_t aByteSize) const
		{
			aCtx.UpdateBuffer(myBuffer, aData, aByteSize);
		}

	private:
		BufferHandle myBuffer;
		SrvHandle    mySrv;
		UavHandle    myUav;
	};
}
