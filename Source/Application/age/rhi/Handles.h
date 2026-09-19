#pragma once
#include <cstdint>

// Opaque GPU-resource handles for the RHI seam (DX11 today, DX12 later).
// Trivially copyable PODs; null == {0,0}. Ownership is explicit via
// IDevice::Destroy(handle) -- handles do not ref-count.

namespace Ag::rhi
{
	template <class Tag>
	struct Handle
	{
		uint32_t index = 0;
		uint32_t generation = 0;

		bool IsValid() const { return index != 0 || generation != 0; }
		explicit operator bool() const { return IsValid(); }
		bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
		bool operator!=(const Handle& o) const { return !(*this == o); }
	};

	struct BufferTag {};
	struct TextureTag {};
	struct SrvTag {};
	struct UavTag {};
	struct RtvTag {};
	struct DsvTag {};
	struct SamplerTag {};
	struct GraphicsPipelineTag {};
	struct ComputePipelineTag {};
	struct ShaderModuleTag {};
	struct FenceTag {};
	struct TimestampQueryTag {};
	struct RaytracingBlasTag {};

	using BufferHandle           = Handle<BufferTag>;
	using TextureHandle          = Handle<TextureTag>;
	using SrvHandle              = Handle<SrvTag>;
	using UavHandle              = Handle<UavTag>;
	using RtvHandle              = Handle<RtvTag>;
	using DsvHandle              = Handle<DsvTag>;
	using SamplerHandle          = Handle<SamplerTag>;
	using GraphicsPipelineHandle = Handle<GraphicsPipelineTag>;
	using ComputePipelineHandle  = Handle<ComputePipelineTag>;
	using ShaderModuleHandle     = Handle<ShaderModuleTag>;
	using FenceHandle            = Handle<FenceTag>;
	using TimestampQueryHandle   = Handle<TimestampQueryTag>;
	using RaytracingBlasHandle   = Handle<RaytracingBlasTag>;

	// A slice of the per-frame dynamic-constant upload ring. On DX11 this maps to
	// a MAP_WRITE_DISCARD'd dynamic cbuffer; on DX12 to an offset in the upload heap.
	struct DynamicAlloc
	{
		BufferHandle buffer;   // backend-owned ring buffer
		uint32_t     offset = 0;
		uint32_t     size = 0;
		void*        cpuPtr = nullptr;   // write-combined; valid until EndFrame
		bool IsValid() const { return buffer.IsValid(); }
	};
}
