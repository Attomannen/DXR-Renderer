#pragma once
#include "tge/rhi/Device.h"

namespace Tga::rhi
{
	// A persistent GPU constant buffer (Upload heap) plus update/bind helpers.
	//
	// Replaces the scattered "D3D11_BUFFER_DESC + CreateBuffer + Map(WRITE_DISCARD)
	// + memcpy + Unmap + XSSetConstantBuffers(slot,...)" boilerplate with one type.
	// The register slot + shader stage live on the object (set once at Create) so
	// call sites read `cb.Bind(ctx)` instead of repeating a magic slot number, and
	// the DX12 migration has a single place to change.
	class ConstantBuffer
	{
	public:
		void Create(IDevice& aDevice, uint32_t aByteSize, ShaderStage aStage, uint32_t aSlot,
		            const char* aDebugName)
		{
			BufferDesc bd = {};
			bd.byteSize  = aByteSize;
			bd.usage     = BufferUsage::Constant;
			bd.memory    = MemoryType::Upload;
			bd.debugName = aDebugName;
			myBuffer   = aDevice.CreateBuffer(bd);
			myCapacity = aByteSize;
			myStage    = aStage;
			mySlot     = aSlot;
		}

		void Destroy(IDevice& aDevice)
		{
			if (myBuffer.IsValid())
				aDevice.Destroy(myBuffer);
			myBuffer = {};
			myCapacity = 0;
		}

		bool IsValid() const { return myBuffer.IsValid(); }
		BufferHandle Handle() const { return myBuffer; }

		void Update(ICommandContext& aCtx, const void* aData, uint32_t aSize) const
		{
			aCtx.UpdateBuffer(myBuffer, aData, aSize < myCapacity ? aSize : myCapacity);
		}
		template <class T>
		void Update(ICommandContext& aCtx, const T& aValue) const { Update(aCtx, &aValue, sizeof(T)); }

		// Bind to the stage + slot given at Create().
		void Bind(ICommandContext& aCtx) const { aCtx.SetConstantBuffer(myStage, mySlot, myBuffer); }
		// Bind to an explicit stage + slot (for buffers used at more than one register).
		void Bind(ICommandContext& aCtx, ShaderStage aStage, uint32_t aSlot) const
		{
			aCtx.SetConstantBuffer(aStage, aSlot, myBuffer);
		}

	private:
		BufferHandle myBuffer;
		uint32_t     myCapacity = 0;
		ShaderStage  myStage = ShaderStage::AllGraphics;
		uint32_t     mySlot  = 0;
	};
}
