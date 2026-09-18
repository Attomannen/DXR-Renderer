#pragma once
#include <array>
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
			myDevice   = &aDevice;
			mySlotCount = aDevice.GetBackend() == Backend::DX12 ? kDx12SlotCount : 1;
			for (uint32_t i = 0; i < mySlotCount; ++i)
				myBuffers[i] = aDevice.CreateBuffer(bd);
			myActiveBuffer = myBuffers[0];
			myCapacity = aByteSize;
			myStage    = aStage;
			mySlot     = aSlot;
		}

		void Destroy(IDevice& aDevice)
		{
			for (uint32_t i = 0; i < mySlotCount; ++i)
			{
				if (myBuffers[i].IsValid())
					aDevice.Destroy(myBuffers[i]);
				myBuffers[i] = {};
			}
			myActiveBuffer = {};
			myDevice = nullptr;
			mySlotCount = 0;
			myCapacity = 0;
			myLastFrame = ~0u;
			myUpdatesThisFrame = 0;
		}

		bool IsValid() const { return myActiveBuffer.IsValid(); }
		BufferHandle Handle() const { return myActiveBuffer; }

		void Update(ICommandContext& aCtx, const void* aData, uint32_t aSize) const
		{
			// Map/Unmap on a D3D12 upload heap does not have DX11's WRITE_DISCARD
			// rename semantics.  Every write therefore needs distinct storage until
			// its recorded draw has retired.  The frame index is fence-paced by the
			// device; within a frame, allocate another persistent slot per update.
			const uint32_t frame = myDevice ? myDevice->GetFrameIndex() : 0;
			if (frame != myLastFrame)
			{
				myLastFrame = frame;
				myUpdatesThisFrame = 0;
			}

			const uint32_t frameBase = (frame % kDx12FramesInFlight) * kUpdatesPerFrame;
			const uint32_t update = mySlotCount > 1
				? (myUpdatesThisFrame < kUpdatesPerFrame ? myUpdatesThisFrame++ : kUpdatesPerFrame - 1)
				: 0;
			myActiveBuffer = myBuffers[mySlotCount > 1 ? frameBase + update : 0];
			aCtx.UpdateBuffer(myActiveBuffer, aData, aSize < myCapacity ? aSize : myCapacity);
		}
		template <class T>
		void Update(ICommandContext& aCtx, const T& aValue) const { Update(aCtx, &aValue, sizeof(T)); }

		// Bind to the stage + slot given at Create().
		void Bind(ICommandContext& aCtx) const { aCtx.SetConstantBuffer(myStage, mySlot, myActiveBuffer); }
		// Bind to an explicit stage + slot (for buffers used at more than one register).
		void Bind(ICommandContext& aCtx, ShaderStage aStage, uint32_t aSlot) const
		{
			aCtx.SetConstantBuffer(aStage, aSlot, myActiveBuffer);
		}

	private:
		// DX12 records three fence-paced frames.  Keep this in sync with
		// Dx12Device::kFramesInFlight.  Thirty-two updates per constant-buffer
		// object/frame covers multi-pass effects without ever rewriting a root-CBV
		// address already recorded into the command list.
		static constexpr uint32_t kDx12FramesInFlight = 3;   // must equal Dx12Device::kFramesInFlight: with 2, frames 0 and 2 shared a slot and the CPU overwrote constants the GPU was still reading
		static constexpr uint32_t kUpdatesPerFrame = 32;
		static constexpr uint32_t kDx12SlotCount = kDx12FramesInFlight * kUpdatesPerFrame;
		std::array<BufferHandle, kDx12SlotCount> myBuffers = {};
		IDevice*     myDevice = nullptr;
		mutable BufferHandle myActiveBuffer;
		mutable uint32_t myLastFrame = ~0u;
		mutable uint32_t myUpdatesThisFrame = 0;
		uint32_t     mySlotCount = 0;
		uint32_t     myCapacity = 0;
		ShaderStage  myStage = ShaderStage::AllGraphics;
		uint32_t     mySlot  = 0;
	};
}
