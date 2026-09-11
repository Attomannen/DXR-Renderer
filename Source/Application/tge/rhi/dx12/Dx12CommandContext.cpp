#include "stdafx.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/Format.h"
#include <cassert>

namespace Tga::rhi::dx12
{
	ID3D12GraphicsCommandList* Dx12CommandContext::List() { return myDevice.RawList(); }

	// ---- targets / viewport / clears (implemented -- BeginFrame/EndFrame need these) ----
	void Dx12CommandContext::SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
		const uint32_t n = count > 8 ? 8 : count;
		for (uint32_t i = 0; i < n; ++i)
		{
			uint32_t* slot = myDevice.GetRtvSlot(rtvs[i]);
			assert(slot && "SetRenderTargets: invalid RtvHandle");
			rtvHandles[i] = myDevice.RtvCpuHandle(slot ? *slot : 0);
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
		D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
		if (uint32_t* dsvSlot = myDevice.GetDsvSlot(dsv))
		{
			dsvHandle = myDevice.DsvCpuHandle(*dsvSlot);
			pDsv = &dsvHandle;
		}
		List()->OMSetRenderTargets(n, n ? rtvHandles : nullptr, FALSE, pDsv);
	}

	void Dx12CommandContext::SetViewport(float x, float y, float w, float h, float minZ, float maxZ)
	{
		D3D12_VIEWPORT vp{ x, y, w, h, minZ, maxZ };
		List()->RSSetViewports(1, &vp);
		// DX12 always requires an explicit scissor rect even with scissor
		// testing conceptually "off" -- default it to the viewport bounds.
		D3D12_RECT sc{ (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) };
		List()->RSSetScissorRects(1, &sc);
	}

	void Dx12CommandContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
	{
		D3D12_RECT sc{ x, y, x + (LONG)w, y + (LONG)h };
		List()->RSSetScissorRects(1, &sc);
	}

	void Dx12CommandContext::ClearRenderTarget(RtvHandle rtv, const float rgba[4])
	{
		uint32_t* slot = myDevice.GetRtvSlot(rtv);
		if (!slot) return;
		List()->ClearRenderTargetView(myDevice.RtvCpuHandle(*slot), rgba, 0, nullptr);
	}

	void Dx12CommandContext::ClearDepthStencil(DsvHandle dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil)
	{
		uint32_t* slot = myDevice.GetDsvSlot(dsv);
		if (!slot) return;
		D3D12_CLEAR_FLAGS flags = {};
		if (clearDepth) flags |= D3D12_CLEAR_FLAG_DEPTH;
		if (clearStencil) flags |= D3D12_CLEAR_FLAG_STENCIL;
		if (!flags) return;
		List()->ClearDepthStencilView(myDevice.DsvCpuHandle(*slot), flags, depth, stencil, 0, nullptr);
	}

	void Dx12CommandContext::ClearUnorderedAccessFloat(UavHandle, const float[4])
	{
		assert(false && "Dx12: ClearUnorderedAccessFloat needs a shader-visible UAV descriptor copy -- milestone 2");
	}

	// ---- pipeline / binding (milestone 2: needs the root signature + PSO cache) ----
	void Dx12CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetComputePipeline(ComputePipelineHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetBlendState(BlendMode) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetDepthStencilState(DepthMode) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetRasterizerState(RasterMode) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetVertexShader(ShaderModuleHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetPixelShader(ShaderModuleHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetInputLayout(ShaderModuleHandle, const InputElement*, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetConstantBuffer(ShaderStage, uint32_t, BufferHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetDynamicConstantBuffer(ShaderStage, uint32_t, const DynamicAlloc&) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetShaderResource(ShaderStage, uint32_t, SrvHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetShaderResources(ShaderStage, uint32_t, uint32_t, const SrvHandle*) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetUnorderedAccess(uint32_t, UavHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetUnorderedAccesses(uint32_t, uint32_t, const UavHandle*) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetSampler(ShaderStage, uint32_t, SamplerHandle) { assert(false && "Dx12: milestone 2"); }

	void Dx12CommandContext::SetVertexBuffer(uint32_t, BufferHandle, uint32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetIndexBuffer(BufferHandle, Format, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::SetPrimitiveTopology(Topology) { assert(false && "Dx12: milestone 2"); }

	void Dx12CommandContext::Draw(uint32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::DrawInstanced(uint32_t, uint32_t, uint32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::DrawIndexed(uint32_t, uint32_t, int32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::DrawIndexedInstanced(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::Dispatch(uint32_t, uint32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }

	void Dx12CommandContext::UpdateBuffer(BufferHandle h, const void* data, uint32_t byteSize, uint32_t dstOffset)
	{
		// Only the UPLOAD-heap (persistently mappable) path is implemented --
		// matches every current call site (constant buffers via
		// rhi::ConstantBuffer, which always uses MemoryType::Upload). A
		// DEFAULT-heap UpdateBuffer would need the same one-off-copy pattern
		// as UploadBufferData; not needed by anything yet.
		BufferRec* b = myDevice.GetBuffer(h);
		if (!b || !b->res) return;
		assert(b->desc.memory == MemoryType::Upload && "Dx12: UpdateBuffer on a non-upload buffer needs milestone 2's copy path");
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (SUCCEEDED(b->res->Map(0, &readRange, &mapped)))
		{
			memcpy(static_cast<uint8_t*>(mapped) + dstOffset, data, byteSize);
			D3D12_RANGE written{ dstOffset, dstOffset + byteSize };
			b->res->Unmap(0, &written);
		}
	}

	void Dx12CommandContext::UpdateTexture(TextureHandle, const void*, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::CopyTexture(TextureHandle, TextureHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::CopyTextureRegion(TextureHandle, uint32_t, uint32_t, TextureHandle, uint32_t, uint32_t) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::GenerateMips(SrvHandle) { assert(false && "Dx12: no native equivalent -- needs a compute-shader mip generator, deferred"); }

	// ---- barriers (implemented -- BeginFrame/EndFrame's backbuffer transition needs this) ----
	static D3D12_RESOURCE_STATES ToD3D12State(ResourceState s)
	{
		if (s == ResourceState::Common) return D3D12_RESOURCE_STATE_COMMON;
		if (s == ResourceState::RenderTarget) return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (s == ResourceState::UnorderedAccess) return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (s == ResourceState::DepthWrite) return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		if (s == ResourceState::DepthRead) return D3D12_RESOURCE_STATE_DEPTH_READ;
		if (s == ResourceState::CopyDest) return D3D12_RESOURCE_STATE_COPY_DEST;
		if (s == ResourceState::CopySource) return D3D12_RESOURCE_STATE_COPY_SOURCE;
		if (s == ResourceState::ResolveDest) return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		if (s == ResourceState::ResolveSource) return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		if (s == ResourceState::Present) return D3D12_RESOURCE_STATE_PRESENT;
		if (s == ResourceState::VertexAndConstantBuffer) return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (s == ResourceState::IndexBuffer) return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		if (s == ResourceState::PixelShaderResource) return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		if (s == ResourceState::NonPixelShaderResource) return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		if (s == ResourceState::GenericRead) return D3D12_RESOURCE_STATE_GENERIC_READ;
		if (s == (ResourceState::PixelShaderResource | ResourceState::NonPixelShaderResource))
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		assert(false && "Dx12: ToD3D12State -- unhandled ResourceState combination");
		return D3D12_RESOURCE_STATE_COMMON;
	}

	void Dx12CommandContext::TransitionResource(TextureHandle h, ResourceState after)
	{
		TextureRec* t = myDevice.GetTexture(h);
		if (!t || !t->res) return;
		const D3D12_RESOURCE_STATES to = ToD3D12State(after);
		if (t->state == to) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = t->res.Get();
		b.Transition.StateBefore = t->state;
		b.Transition.StateAfter = to;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		List()->ResourceBarrier(1, &b);
		t->state = to;
	}

	void Dx12CommandContext::TransitionResource(BufferHandle h, ResourceState after)
	{
		BufferRec* buf = myDevice.GetBuffer(h);
		if (!buf || !buf->res) return;
		const D3D12_RESOURCE_STATES to = ToD3D12State(after);
		if (buf->state == to) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = buf->res.Get();
		b.Transition.StateBefore = buf->state;
		b.Transition.StateAfter = to;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		List()->ResourceBarrier(1, &b);
		buf->state = to;
	}

	void Dx12CommandContext::UavBarrier(TextureHandle h)
	{
		TextureRec* t = myDevice.GetTexture(h);
		if (!t || !t->res) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		b.UAV.pResource = t->res.Get();
		List()->ResourceBarrier(1, &b);
	}

	void Dx12CommandContext::UavBarrier(BufferHandle h)
	{
		BufferRec* buf = myDevice.GetBuffer(h);
		if (!buf || !buf->res) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		b.UAV.pResource = buf->res.Get();
		List()->ResourceBarrier(1, &b);
	}

	// ---- timestamps / markers (milestone 2) ----
	void Dx12CommandContext::WriteTimestampBegin(TimestampQueryHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::WriteTimestampEnd(TimestampQueryHandle) { assert(false && "Dx12: milestone 2"); }
	void Dx12CommandContext::PushMarker(const char*) { /* no-op for milestone 1; PIX markers need WinPixEventRuntime's DX12 entry points, trivial to add in milestone 2 */ }
	void Dx12CommandContext::PopMarker() { }
}
