#include "stdafx.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/Format.h"
#include <cassert>

namespace Tga::rhi::dx12
{
	Dx12CommandContext::Dx12CommandContext(Dx12Device& aDevice) : myDevice(aDevice) {}

	ID3D12GraphicsCommandList* Dx12CommandContext::List() { return myDevice.RawList(); }

	void Dx12CommandContext::OnBeginFrame()
	{
		// Root signatures are immutable for the app's whole life -- bind both
		// once per frame rather than tracking "did it change" (see the class
		// comment). Root arguments/tables don't survive a Reset() of the
		// command list, so everything below must be considered unbound again
		// at the start of every frame.
		List()->SetGraphicsRootSignature(myDevice.GraphicsRootSignature());
		List()->SetComputeRootSignature(myDevice.ComputeRootSignature());

		for (auto& h : myBoundSrv) h = {};
		for (auto& h : myBoundUav) h = {};
		for (auto& h : myBoundSampler) h = {};
		mySrvTableDirtyGraphics = mySrvTableDirtyCompute = true;
		myUavTableDirtyCompute = true;
		mySamplerTableDirtyGraphics = mySamplerTableDirtyCompute = true;
		myBoundGfxPipeline = {};
		myBoundTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		for (auto& h : myBoundVb) h = {};
		myBoundIb = {};
	}

	// ---- targets / viewport / clears ----
	void Dx12CommandContext::SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
		const uint32_t n = count > 8 ? 8 : count;
		myBoundRtvCount = n;
		for (uint32_t i = 0; i < n; ++i)
		{
			uint32_t* slot = myDevice.GetRtvSlot(rtvs[i]);
			assert(slot && "SetRenderTargets: invalid RtvHandle");
			rtvHandles[i] = myDevice.RtvCpuHandle(slot ? *slot : 0);
			myBoundRtvFormats[i] = myDevice.GetRtvFormat(rtvs[i]);
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
		D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
		myBoundDsvFormat = myDevice.GetDsvFormat(dsv);
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
		// ClearUnorderedAccessViewFloat needs BOTH a shader-visible GPU handle
		// (easy -- copy into scratch like FlushComputeTables does) AND the raw
		// ID3D12Resource* it views (this milestone's UavHandle only carries a
		// heap slot index, via Pool<uint32_t,UavHandle> -- it doesn't remember
		// which texture/buffer owns that view). No DX12 call site needs this
		// yet (DeferredRenderer::ClearGi() only runs on the DX11 backend until
		// milestone 3 wires the engine itself onto DX12), so it's deferred
		// rather than guessed at.
		assert(false && "Dx12: ClearUnorderedAccessFloat -- deferred, see comment");
	}

	// ---- pipeline state ----
	void Dx12CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle h)
	{
		if (h == myBoundGfxPipeline) return;
		GfxPipelineRec* rec = myDevice.GetGfxPipeline(h);
		if (!rec || !rec->pso) return;
		List()->SetPipelineState(rec->pso.Get());
		if (rec->topo != myBoundTopology) { List()->IASetPrimitiveTopology(rec->topo); myBoundTopology = rec->topo; }
		myBoundGfxPipeline = h;
	}

	void Dx12CommandContext::SetComputePipeline(ComputePipelineHandle h)
	{
		ComputePipelineRec* rec = myDevice.GetComputePipeline(h);
		if (!rec || !rec->pso) return;   // {} (unbind) has no PSO to restore to on DX12; harmless no-op, matches "nothing to draw with" intent
		List()->SetPipelineState(rec->pso.Get());
	}

	void Dx12CommandContext::SetBlendState(BlendMode m) { myPendingBlend = m; }
	void Dx12CommandContext::SetDepthStencilState(DepthMode m) { myPendingDepth = m; }
	void Dx12CommandContext::SetRasterizerState(RasterMode m) { myPendingRaster = m; }
	void Dx12CommandContext::SetVertexShader(ShaderModuleHandle h) { myPendingVs = h; }
	void Dx12CommandContext::SetPixelShader(ShaderModuleHandle h) { myPendingPs = h; }
	void Dx12CommandContext::SetInputLayout(ShaderModuleHandle, const InputElement* elems, uint32_t count)
	{
		myPendingInputLayout.assign(elems, elems + count);
	}

	void Dx12CommandContext::SetPrimitiveTopology(Topology t) { myPendingTopology = t; }

	void Dx12CommandContext::ResolveGraphicsPipeline()
	{
		GraphicsPipelineDesc d = {};
		d.vs = myPendingVs;
		d.ps = myPendingPs;
		d.blend = myPendingBlend;
		d.depth = myPendingDepth;
		d.raster = myPendingRaster;
		d.topology = myPendingTopology;
		d.renderTargetCount = myBoundRtvCount;
		for (uint32_t i = 0; i < myBoundRtvCount && i < 8; ++i) d.rtvFormats[i] = myBoundRtvFormats[i];
		d.dsvFormat = myBoundDsvFormat;
		d.inputLayout = myPendingInputLayout.empty() ? nullptr : myPendingInputLayout.data();
		d.inputLayoutCount = (uint32_t)myPendingInputLayout.size();

		GraphicsPipelineHandle h = myDevice.CreateGraphicsPipeline(d);
		SetGraphicsPipeline(h);
	}

	// ---- bind-by-slot ----
	void Dx12CommandContext::SetConstantBuffer(ShaderStage stage, uint32_t slot, BufferHandle h)
	{
		if (slot >= Dx12Device::kNumCbvRegisters) return;
		BufferRec* b = myDevice.GetBuffer(h);
		D3D12_GPU_VIRTUAL_ADDRESS addr = (b && b->res) ? b->res->GetGPUVirtualAddress() : 0;
		if (!addr) return;
		if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) List()->SetGraphicsRootConstantBufferView(slot, addr);
		if (HasStage(stage, ShaderStage::Compute)) List()->SetComputeRootConstantBufferView(slot, addr);
	}

	void Dx12CommandContext::SetDynamicConstantBuffer(ShaderStage stage, uint32_t slot, const DynamicAlloc& a)
	{
		if (slot >= Dx12Device::kNumCbvRegisters || !a.IsValid()) return;
		BufferRec* b = myDevice.GetBuffer(a.buffer);
		if (!b || !b->res) return;
		D3D12_GPU_VIRTUAL_ADDRESS addr = b->res->GetGPUVirtualAddress() + a.offset;
		if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) List()->SetGraphicsRootConstantBufferView(slot, addr);
		if (HasStage(stage, ShaderStage::Compute)) List()->SetComputeRootConstantBufferView(slot, addr);
	}

	void Dx12CommandContext::SetShaderResource(ShaderStage stage, uint32_t slot, SrvHandle h)
	{
		if (slot >= kNumSrv) return;
		myBoundSrv[slot] = h;
		if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) mySrvTableDirtyGraphics = true;
		if (HasStage(stage, ShaderStage::Compute)) mySrvTableDirtyCompute = true;
	}

	void Dx12CommandContext::SetShaderResources(ShaderStage stage, uint32_t firstSlot, uint32_t count, const SrvHandle* handles)
	{
		for (uint32_t i = 0; i < count && firstSlot + i < kNumSrv; ++i) myBoundSrv[firstSlot + i] = handles[i];
		if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) mySrvTableDirtyGraphics = true;
		if (HasStage(stage, ShaderStage::Compute)) mySrvTableDirtyCompute = true;
	}

	void Dx12CommandContext::SetUnorderedAccess(uint32_t slot, UavHandle h)
	{
		if (slot >= kNumUav) return;
		myBoundUav[slot] = h;
		myUavTableDirtyCompute = true;
	}

	void Dx12CommandContext::SetUnorderedAccesses(uint32_t firstSlot, uint32_t count, const UavHandle* handles)
	{
		for (uint32_t i = 0; i < count && firstSlot + i < kNumUav; ++i) myBoundUav[firstSlot + i] = handles[i];
		myUavTableDirtyCompute = true;
	}

	void Dx12CommandContext::SetSampler(ShaderStage stage, uint32_t slot, SamplerHandle h)
	{
		if (slot >= kNumSampler) return;
		myBoundSampler[slot] = h;
		if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) mySamplerTableDirtyGraphics = true;
		if (HasStage(stage, ShaderStage::Compute)) mySamplerTableDirtyCompute = true;
	}

	// ---- descriptor table flush (see Dx12CommandContext.h's class comment) ----
	void Dx12CommandContext::FlushGraphicsTables()
	{
		ID3D12Device* dev = myDevice.Raw();
		if (mySrvTableDirtyGraphics)
		{
			uint32_t destStart = myDevice.CbvSrvUavScratch().AllocateRange(kNumSrv);
			D3D12_CPU_DESCRIPTOR_HANDLE destHandle = myDevice.CbvSrvUavScratch().Cpu(destStart);
			D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kNumSrv];
			UINT srcSizes[kNumSrv];
			for (uint32_t i = 0; i < kNumSrv; ++i)
			{
				uint32_t* slot = myDevice.GetSrvSlot(myBoundSrv[i]);
				srcHandles[i] = myDevice.CbvSrvUavCpuHandle(slot ? *slot : myDevice.NullSrvSlot());
				srcSizes[i] = 1;
			}
			UINT destSize = kNumSrv;
			dev->CopyDescriptors(1, &destHandle, &destSize, kNumSrv, srcHandles, srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			List()->SetGraphicsRootDescriptorTable(Dx12Device::kNumCbvRegisters, myDevice.CbvSrvUavScratch().Gpu(destStart));
			mySrvTableDirtyGraphics = false;
		}
		if (mySamplerTableDirtyGraphics)
		{
			uint32_t destStart = myDevice.SamplerScratch().AllocateRange(kNumSampler);
			D3D12_CPU_DESCRIPTOR_HANDLE destHandle = myDevice.SamplerScratch().Cpu(destStart);
			D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kNumSampler];
			UINT srcSizes[kNumSampler];
			for (uint32_t i = 0; i < kNumSampler; ++i)
			{
				uint32_t* slot = myDevice.GetSamplerSlot(myBoundSampler[i]);
				srcHandles[i] = myDevice.SamplerCpuHandle(slot ? *slot : myDevice.NullSamplerSlot());
				srcSizes[i] = 1;
			}
			UINT destSize = kNumSampler;
			dev->CopyDescriptors(1, &destHandle, &destSize, kNumSampler, srcHandles, srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			List()->SetGraphicsRootDescriptorTable(Dx12Device::kNumCbvRegisters + 1, myDevice.SamplerScratch().Gpu(destStart));
			mySamplerTableDirtyGraphics = false;
		}
	}

	void Dx12CommandContext::FlushComputeTables()
	{
		ID3D12Device* dev = myDevice.Raw();
		if (mySrvTableDirtyCompute)
		{
			uint32_t destStart = myDevice.CbvSrvUavScratch().AllocateRange(kNumSrv);
			D3D12_CPU_DESCRIPTOR_HANDLE destHandle = myDevice.CbvSrvUavScratch().Cpu(destStart);
			D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kNumSrv];
			UINT srcSizes[kNumSrv];
			for (uint32_t i = 0; i < kNumSrv; ++i)
			{
				uint32_t* slot = myDevice.GetSrvSlot(myBoundSrv[i]);
				srcHandles[i] = myDevice.CbvSrvUavCpuHandle(slot ? *slot : myDevice.NullSrvSlot());
				srcSizes[i] = 1;
			}
			UINT destSize = kNumSrv;
			dev->CopyDescriptors(1, &destHandle, &destSize, kNumSrv, srcHandles, srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			List()->SetComputeRootDescriptorTable(Dx12Device::kNumCbvRegisters, myDevice.CbvSrvUavScratch().Gpu(destStart));
			mySrvTableDirtyCompute = false;
		}
		if (myUavTableDirtyCompute)
		{
			uint32_t destStart = myDevice.CbvSrvUavScratch().AllocateRange(kNumUav);
			D3D12_CPU_DESCRIPTOR_HANDLE destHandle = myDevice.CbvSrvUavScratch().Cpu(destStart);
			D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kNumUav];
			UINT srcSizes[kNumUav];
			for (uint32_t i = 0; i < kNumUav; ++i)
			{
				uint32_t* slot = myDevice.GetUavSlot(myBoundUav[i]);
				srcHandles[i] = myDevice.CbvSrvUavCpuHandle(slot ? *slot : myDevice.NullUavSlot());
				srcSizes[i] = 1;
			}
			UINT destSize = kNumUav;
			dev->CopyDescriptors(1, &destHandle, &destSize, kNumUav, srcHandles, srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			List()->SetComputeRootDescriptorTable(Dx12Device::kNumCbvRegisters + 1, myDevice.CbvSrvUavScratch().Gpu(destStart));
			myUavTableDirtyCompute = false;
		}
		if (mySamplerTableDirtyCompute)
		{
			uint32_t destStart = myDevice.SamplerScratch().AllocateRange(kNumSampler);
			D3D12_CPU_DESCRIPTOR_HANDLE destHandle = myDevice.SamplerScratch().Cpu(destStart);
			D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[kNumSampler];
			UINT srcSizes[kNumSampler];
			for (uint32_t i = 0; i < kNumSampler; ++i)
			{
				uint32_t* slot = myDevice.GetSamplerSlot(myBoundSampler[i]);
				srcHandles[i] = myDevice.SamplerCpuHandle(slot ? *slot : myDevice.NullSamplerSlot());
				srcSizes[i] = 1;
			}
			UINT destSize = kNumSampler;
			dev->CopyDescriptors(1, &destHandle, &destSize, kNumSampler, srcHandles, srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
			List()->SetComputeRootDescriptorTable(Dx12Device::kNumCbvRegisters + 2, myDevice.SamplerScratch().Gpu(destStart));
			mySamplerTableDirtyCompute = false;
		}
	}

	// ---- geometry ----
	void Dx12CommandContext::SetVertexBuffer(uint32_t slot, BufferHandle h, uint32_t stride, uint32_t offset)
	{
		if (slot >= 4) return;
		myBoundVb[slot] = h; myVbStride[slot] = stride; myVbOffset[slot] = offset;
		BufferRec* b = myDevice.GetBuffer(h);
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		if (b && b->res)
		{
			vbv.BufferLocation = b->res->GetGPUVirtualAddress() + offset;
			vbv.SizeInBytes = (UINT)b->desc.byteSize - offset;
			vbv.StrideInBytes = stride;
		}
		List()->IASetVertexBuffers(slot, 1, &vbv);
	}

	void Dx12CommandContext::SetIndexBuffer(BufferHandle h, Format indexFormat, uint32_t offset)
	{
		myBoundIb = h; myIbFormat = indexFormat; myIbOffset = offset;
		BufferRec* b = myDevice.GetBuffer(h);
		D3D12_INDEX_BUFFER_VIEW ibv = {};
		if (b && b->res)
		{
			ibv.BufferLocation = b->res->GetGPUVirtualAddress() + offset;
			ibv.SizeInBytes = (UINT)b->desc.byteSize - offset;
			ibv.Format = ToDxgi(indexFormat);
		}
		List()->IASetIndexBuffer(&ibv);
	}

	// ---- draw / dispatch ----
	void Dx12CommandContext::Draw(uint32_t vertexCount, uint32_t startVertex)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		List()->DrawInstanced(vertexCount, 1, startVertex, 0);
	}
	void Dx12CommandContext::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		List()->DrawInstanced(vertexCountPerInstance, instanceCount, startVertex, startInstance);
	}
	void Dx12CommandContext::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		List()->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
	}
	void Dx12CommandContext::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		List()->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
	}
	void Dx12CommandContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
	{
		FlushComputeTables();
		List()->Dispatch(x, y, z);
	}

	// ---- resource ops ----
	void Dx12CommandContext::UpdateBuffer(BufferHandle h, const void* data, uint32_t byteSize, uint32_t dstOffset)
	{
		BufferRec* b = myDevice.GetBuffer(h);
		if (!b || !b->res) return;
		assert(b->desc.memory == MemoryType::Upload && "Dx12: UpdateBuffer on a non-upload buffer needs a copy-based path (not needed by any call site yet)");
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (SUCCEEDED(b->res->Map(0, &readRange, &mapped)))
		{
			memcpy(static_cast<uint8_t*>(mapped) + dstOffset, data, byteSize);
			D3D12_RANGE written{ dstOffset, dstOffset + byteSize };
			b->res->Unmap(0, &written);
		}
	}

	void Dx12CommandContext::UpdateTexture(TextureHandle, const void*, uint32_t)
	{
		assert(false && "Dx12: UpdateTexture (mid-lifetime, e.g. video/font-atlas frame updates) needs the upload-heap copy path -- milestone 3");
	}
	void Dx12CommandContext::CopyTexture(TextureHandle dstH, TextureHandle srcH)
	{
		TextureRec* dst = myDevice.GetTexture(dstH);
		TextureRec* src = myDevice.GetTexture(srcH);
		if (!dst || !src || !dst->res || !src->res) return;
		List()->CopyResource(dst->res.Get(), src->res.Get());
	}
	void Dx12CommandContext::CopyTextureRegion(TextureHandle dstH, uint32_t dstMip, uint32_t dstArray, TextureHandle srcH, uint32_t srcMip, uint32_t srcArray)
	{
		TextureRec* dst = myDevice.GetTexture(dstH);
		TextureRec* src = myDevice.GetTexture(srcH);
		if (!dst || !src || !dst->res || !src->res) return;
		// D3D12CalcSubresource's formula (that helper lives in d3dx12.h, not
		// vendored here -- MipSlice + ArraySlice * MipLevels, PlaneSlice always 0).
		const uint32_t dstMips = dst->desc.mipLevels ? dst->desc.mipLevels : 1;
		const uint32_t srcMips = src->desc.mipLevels ? src->desc.mipLevels : 1;
		const UINT dstSub = dstMip + dstArray * dstMips;
		const UINT srcSub = srcMip + srcArray * srcMips;
		D3D12_TEXTURE_COPY_LOCATION dstLoc = { dst->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		dstLoc.SubresourceIndex = dstSub;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { src->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		srcLoc.SubresourceIndex = srcSub;
		List()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
	}
	void Dx12CommandContext::GenerateMips(SrvHandle) { assert(false && "Dx12: no native equivalent -- needs a compute-shader mip generator, deferred"); }

	// ---- barriers ----
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

	// ---- timestamps / markers (milestone 3) ----
	void Dx12CommandContext::WriteTimestampBegin(TimestampQueryHandle) { /* milestone 3 */ }
	void Dx12CommandContext::WriteTimestampEnd(TimestampQueryHandle) { /* milestone 3 */ }
	void Dx12CommandContext::PushMarker(const char*) { /* PIX markers: trivial to add via WinPixEventRuntime's DX12 entry points, not done yet */ }
	void Dx12CommandContext::PopMarker() { }
}
