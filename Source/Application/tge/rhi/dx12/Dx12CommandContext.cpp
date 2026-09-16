#include "stdafx.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/Format.h"
#include "tge/graphics/DX11.h"   // GenerateMips: reuses DX11::Load{Vertex,Pixel}Shader (backend-agnostic since this session's shader-loading fix) for its fullscreen-copy blit shaders, rather than compiling anything new
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
		List()->SetComputeRootDescriptorTable(Dx12Device::kRaySceneRootParameter,
			myDevice.RaySceneDescriptorGpuStart());
		// Same GPU address as above, bound a second time under the space4 root
		// parameter so a shader can read the identical slots as Texture2D --
		// see kRaySceneTexRootParameter's comment.
		List()->SetComputeRootDescriptorTable(Dx12Device::kRaySceneTexRootParameter,
			myDevice.RaySceneDescriptorGpuStart());
		// Only once the first BuildRaytracingTlas of the process has actually
		// run -- until then both addresses are 0, and binding a null address
		// to a root SRV is invalid (unlike an unused descriptor-table slot,
		// which the null descriptors above cover). Scenes without DXR content
		// (or before the first frame's TLAS build) simply never bind these;
		// no compute shader is required to declare space2 unless it uses it.
		myDevice.BindRaytracingSceneForCompute();

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
	// SEH-isolated + retried: found debugging DeferredRenderer::GiProjectProbe
	// (2026-09-11) -- the FIRST OMSetRenderTargets call after ANY compute-
	// pipeline Dispatch (e.g. a GI probe's SH-projection pass) reliably
	// access-violates deep inside the D3D12 runtime/driver on this hardware,
	// then works completely normally on every subsequent identical call using
	// the exact same RTV/DSV handles -- i.e. it is not a resource lifetime bug
	// (the handles, heap slots, and underlying resources are all confirmed
	// valid before and after via bisection) and not debug-layer-specific
	// (reproduces identically with the D3D12 debug layer OFF, and the layer
	// logs nothing unusual around the fault). It looks like a real, narrow
	// driver/runtime quirk in this specific compute-to-graphics transition --
	// something about the FIRST such call specifically doesn't take, but a
	// second, identical one right after always does. Retrying in place (not
	// just swallowing the fault) turns this from "that frame's geometry pass
	// silently never gets its render targets bound, so it draws over
	// whatever was bound before -- visibly, the model disappears and only the
	// skybox/ambient shows" into a transparent, fully-recovered no-op: this is
	// the actual fix for that visible symptom, found immediately after the
	// crash-containment version above shipped. Revisit if this GPU/driver
	// combo changes, or if a real root cause surfaces (candidates not yet
	// ruled out: a PIX/NSight capture of the exact faulting frame; an NVIDIA
	// driver update) -- then this retry (and the SEH itself) can go.
	static void SEH_OMSetRenderTargets(ID3D12GraphicsCommandList* list, UINT n,
	                                    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles,
	                                    const D3D12_CPU_DESCRIPTOR_HANDLE* pDsv)
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			__try
			{
				list->OMSetRenderTargets(n, n ? rtvHandles : nullptr, FALSE, pDsv);
				return;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				ERROR_PRINT("Dx12: OMSetRenderTargets faulted (0x%08X) on attempt %d, retrying -- see SetRenderTargets's class comment", (unsigned)GetExceptionCode(), attempt);
			}
		}
		ERROR_PRINT("%s", "Dx12: OMSetRenderTargets faulted on every retry -- frame dropped");
	}

	void Dx12CommandContext::SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
		const uint32_t n = count > 8 ? 8 : count;
		myBoundRtvCount = n;
		for (uint32_t i = 0; i < n; ++i)
		{
			if (RtvRec* rtv = myDevice.GetRtv(rtvs[i]))
				TransitionResource(rtv->texture, ResourceState::RenderTarget);
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
			if (DsvRec* dsvRec = myDevice.GetDsv(dsv))
				TransitionResource(dsvRec->texture, ResourceState::DepthWrite);
			dsvHandle = myDevice.DsvCpuHandle(*dsvSlot);
			pDsv = &dsvHandle;
		}
		SEH_OMSetRenderTargets(List(), n, rtvHandles, pDsv);
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
		if (RtvRec* rec = myDevice.GetRtv(rtv))
			TransitionResource(rec->texture, ResourceState::RenderTarget);
		uint32_t* slot = myDevice.GetRtvSlot(rtv);
		if (!slot) return;
		FlushBarriers();
		List()->ClearRenderTargetView(myDevice.RtvCpuHandle(*slot), rgba, 0, nullptr);
	}

	void Dx12CommandContext::ClearDepthStencil(DsvHandle dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil)
	{
		if (DsvRec* rec = myDevice.GetDsv(dsv))
			TransitionResource(rec->texture, ResourceState::DepthWrite);
		uint32_t* slot = myDevice.GetDsvSlot(dsv);
		if (!slot) return;
		D3D12_CLEAR_FLAGS flags = {};
		if (clearDepth) flags |= D3D12_CLEAR_FLAG_DEPTH;
		if (clearStencil) flags |= D3D12_CLEAR_FLAG_STENCIL;
		if (!flags) return;
		D3D12_CPU_DESCRIPTOR_HANDLE h = myDevice.DsvCpuHandle(*slot);
		FlushBarriers();
		List()->ClearDepthStencilView(h, flags, depth, stencil, 0, nullptr);
	}

	void Dx12CommandContext::ClearUnorderedAccessFloat(UavHandle h, const float rgba[4])
	{
		// Implemented 2026-09-12: this milestone-2-era stub's original blocker
		// ("UavHandle doesn't remember which texture/buffer owns that view") was
		// closed by UavRec gaining owner texture/buffer fields (Dx12Device.h,
		// same session as SetShaderResource/SetUnorderedAccess's owner-based
		// auto-transitions) -- nothing left to defer.
		UavRec* rec = myDevice.GetUav(h);
		uint32_t* permSlot = myDevice.GetUavSlot(h);
		if (!rec || !permSlot) return;

		ID3D12Resource* resource = nullptr;
		if (rec->texture) { if (TextureRec* t = myDevice.GetTexture(rec->texture)) resource = t->res.Get(); }
		if (rec->buffer)  { if (BufferRec* b = myDevice.GetBuffer(rec->buffer))  resource = b->res.Get(); }
		if (!resource) return;

		// ClearUnorderedAccessViewFloat is unusual: it needs a CPU handle from a
		// NON-shader-visible heap (the UAV's permanent, freelist-allocated one)
		// *and* a GPU handle that is currently resident in the shader-visible
		// heap actually bound on this command list. Copy the permanent
		// descriptor into a fresh slot of this frame's own CBV/SRV/UAV scratch
		// heap (already bound every frame via OnBeginFrame's SetDescriptorHeaps)
		// just for this one call -- same pattern FlushGraphicsTables/
		// FlushComputeTables use to populate their descriptor tables.
		D3D12_CPU_DESCRIPTOR_HANDLE permCpu = myDevice.CbvSrvUavCpuHandle(*permSlot);
		uint32_t scratchSlot = myDevice.CbvSrvUavScratch().AllocateRange(1);
		D3D12_CPU_DESCRIPTOR_HANDLE scratchCpu = myDevice.CbvSrvUavScratch().Cpu(scratchSlot);
		D3D12_GPU_DESCRIPTOR_HANDLE scratchGpu = myDevice.CbvSrvUavScratch().Gpu(scratchSlot);
		myDevice.Raw()->CopyDescriptorsSimple(1, scratchCpu, permCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		FlushBarriers();
		List()->ClearUnorderedAccessViewFloat(scratchGpu, permCpu, resource, rgba, 0, nullptr);
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
		// A command list has exactly ONE active pipeline-state slot regardless
		// of type -- SetPipelineState here just silently replaced whatever
		// graphics PSO was bound. SetGraphicsPipeline's "already bound, skip
		// the redundant SetPipelineState" cache would otherwise stay fooled:
		// the next Draw reusing the same GraphicsPipelineHandle as before this
		// dispatch would see it match myBoundGfxPipeline and skip re-binding,
		// leaving this COMPUTE pso active for a Draw call -- a real, silent
		// GPU-side crash (found debugging DeferredRenderer::GiProjectProbe,
		// which runs between two cubemap-face graphics draws using the exact
		// same PSO). Invalidate so the next SetGraphicsPipeline always rebinds.
		myBoundGfxPipeline = {};
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
		if (SrvRec* rec = myDevice.GetSrv(h))
		{
			// A graphics SRV may be consumed by either VS or PS; use the legal
			// combined read state so one descriptor-table binding is valid for both.
			const ResourceState state = HasStage(stage, ShaderStage::Compute)
				? ResourceState::NonPixelShaderResource
				: ResourceState::PixelShaderResource | ResourceState::NonPixelShaderResource;
			if (rec->texture) TransitionResource(rec->texture, state);
			if (rec->buffer)  TransitionResource(rec->buffer, ResourceState::GenericRead);
		}
		// Dirty-track on actual change, not on every call: a caller that rebinds
		// the same handle every iteration of a loop (e.g. GenerateMips's mip
		// chain reusing one sampler, or any repeated redundant bind) must not
		// burn a fresh descriptor-table range each time -- see SetSampler's
		// identical reasoning, which is where this was actually found to matter
		// (the CBV/SRV/UAV scratch heap is far larger, so this is more a
		// consistency fix here than a currently-hit limit).
		if (myBoundSrv[slot] != h)
		{
			myBoundSrv[slot] = h;
			if (HasStage(stage, ShaderStage::Vertex) || HasStage(stage, ShaderStage::Pixel)) mySrvTableDirtyGraphics = true;
			if (HasStage(stage, ShaderStage::Compute)) mySrvTableDirtyCompute = true;
		}
	}

	void Dx12CommandContext::SetShaderResources(ShaderStage stage, uint32_t firstSlot, uint32_t count, const SrvHandle* handles)
	{
		for (uint32_t i = 0; i < count && firstSlot + i < kNumSrv; ++i)
			SetShaderResource(stage, firstSlot + i, handles[i]);
	}

	void Dx12CommandContext::SetUnorderedAccess(uint32_t slot, UavHandle h)
	{
		if (slot >= kNumUav) return;
		if (UavRec* rec = myDevice.GetUav(h))
		{
			if (rec->texture) TransitionResource(rec->texture, ResourceState::UnorderedAccess);
			if (rec->buffer)  TransitionResource(rec->buffer, ResourceState::UnorderedAccess);
		}
		// See SetShaderResource's identical dirty-on-change reasoning.
		if (myBoundUav[slot] != h)
		{
			myBoundUav[slot] = h;
			myUavTableDirtyCompute = true;
		}
	}

	void Dx12CommandContext::SetUnorderedAccesses(uint32_t firstSlot, uint32_t count, const UavHandle* handles)
	{
		for (uint32_t i = 0; i < count && firstSlot + i < kNumUav; ++i)
			SetUnorderedAccess(firstSlot + i, handles[i]);
	}

	void Dx12CommandContext::SetSampler(ShaderStage stage, uint32_t slot, SamplerHandle h)
	{
		if (slot >= kNumSampler) return;
		// Dirty-track on actual change, not on every call. The sampler scratch
		// heap is hard-capped at 2048 (the real D3D12 hardware ceiling -- see
		// kSamplerScratchPerFrame's comment -- there is no higher capacity to
		// raise it to), and unlike the much larger CBV/SRV/UAV heap this limit
		// is real and reachable: a caller that rebinds the identical sampler
		// on every iteration of a loop (found 2026-09-12: GenerateMips's mip
		// chain and CubemapPrefilter's per-mip prefilter dispatches both bind
		// one fixed sampler dozens to hundreds of times per cubemap, across
		// many cubemaps per GI-probe bake) was burning a fresh descriptor-
		// table range every single time even though nothing had changed,
		// exhausting the heap mid-frame well before any real variety of
		// samplers was in play.
		if (myBoundSampler[slot] == h) return;
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
	// LogDrawCall() calls below: match Dx11CommandContext's identical calls at each of its
	// 4 Draw* sites -- a plain static counter (DX11.h), not a DX11-specific object, so it's
	// backend-agnostic and safe here. Was missing entirely for DX12 (found 2026-09-12): every
	// DX12 bench/perf-overlay draw-call count read 0 regardless of real scene complexity.
	void Dx12CommandContext::Draw(uint32_t vertexCount, uint32_t startVertex)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		FlushBarriers();
		List()->DrawInstanced(vertexCount, 1, startVertex, 0);
		Tga::DX11::LogDrawCall();
	}
	void Dx12CommandContext::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		FlushBarriers();
		List()->DrawInstanced(vertexCountPerInstance, instanceCount, startVertex, startInstance);
		Tga::DX11::LogDrawCall();
	}
	void Dx12CommandContext::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		FlushBarriers();
		List()->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
		Tga::DX11::LogDrawCall();
	}
	void Dx12CommandContext::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
	{
		ResolveGraphicsPipeline();
		FlushGraphicsTables();
		FlushBarriers();
		List()->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
		Tga::DX11::LogDrawCall();
	}
	void Dx12CommandContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
	{
		FlushComputeTables();
		FlushBarriers();
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

	void Dx12CommandContext::UpdateTexture(TextureHandle h, const void* data, uint32_t rowPitch)
	{
		// Mid-lifetime full-subresource-0 overwrite (video decode frames,
		// TextService's font atlas) -- unlike CreateTexture's initial-data
		// path (its own one-off command list + synchronous WaitForGpuIdle,
		// fine at load time), this records into the CURRENT frame's own
		// command list, so it can't block on the GPU here. The staging
		// UPLOAD resource is kept alive via Dx12Device::KeepAliveUntilFrameRetires
		// until this frame-in-flight's next BeginFrame confirms the GPU is done.
		TextureRec* t = myDevice.GetTexture(h);
		if (!t || !t->res || !data) return;

		ID3D12Device* device = myDevice.Raw();
		D3D12_RESOURCE_DESC desc = t->res->GetDesc();

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		UINT64 totalBytes = 0;
		device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

		uint64_t uploadOffset = 0;
		void* mapped = nullptr;
		ID3D12Resource* uploadRes = nullptr;
		myDevice.AllocateUploadSpace((uint32_t)totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, uploadOffset, mapped, uploadRes);

		if (!uploadRes || !mapped)
			return;

		const uint8_t* src = static_cast<const uint8_t*>(data);
		for (UINT row = 0; row < numRows; ++row)
		{
			memcpy(static_cast<uint8_t*>(mapped) + row * footprint.Footprint.RowPitch,
			       src + row * rowPitch,
			       (size_t)rowSizeInBytes);
		}

		footprint.Offset += uploadOffset;

		const D3D12_RESOURCE_STATES before = t->state;
		if (before != D3D12_RESOURCE_STATE_COPY_DEST)
		{
			D3D12_RESOURCE_BARRIER toCopyDest = {};
			toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			toCopyDest.Transition.pResource = t->res.Get();
			toCopyDest.Transition.StateBefore = before;
			toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			myBarriers.push_back(toCopyDest);
		}

		D3D12_TEXTURE_COPY_LOCATION dstLoc = { t->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		dstLoc.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { uploadRes, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
		srcLoc.PlacedFootprint = footprint;
		FlushBarriers();
		List()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		if (before != D3D12_RESOURCE_STATE_COPY_DEST)
		{
			D3D12_RESOURCE_BARRIER back = {};
			back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			back.Transition.pResource = t->res.Get();
			back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			back.Transition.StateAfter = before;
			back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			myBarriers.push_back(back);
		}
		// t->state is unchanged overall (restored to `before` above), so no
		// update to the tracked state needed.
	}
	void Dx12CommandContext::CopyTexture(TextureHandle dstH, TextureHandle srcH)
	{
		TextureRec* dst = myDevice.GetTexture(dstH);
		TextureRec* src = myDevice.GetTexture(srcH);
		if (!dst || !src || !dst->res || !src->res) return;
		FlushBarriers();
		List()->CopyResource(dst->res.Get(), src->res.Get());
	}
	void Dx12CommandContext::CopyTextureRegion(TextureHandle dstH, uint32_t dstMip, uint32_t dstArray, TextureHandle srcH, uint32_t srcMip, uint32_t srcArray)
	{
		// Self-managed barriers, since (unlike D3D11) a copy needs its exact
		// subresource in COPY_DEST/COPY_SOURCE first -- brackets the SPECIFIC
		// subresource being touched (not TextureRec::state's whole-resource
		// value, which stays valid for every OTHER subresource) and restores
		// it before returning, same self-contained pattern as GenerateMips/
		// UpdateTexture. First real caller: CubemapPrefilter's per-face copy
		// into a cubemap array slice (dst is multi-subresource; each of its 6
		// calls is independently bracketed).
		TextureRec* dst = myDevice.GetTexture(dstH);
		TextureRec* src = myDevice.GetTexture(srcH);
		if (!dst || !src || !dst->res || !src->res) return;
		// D3D12CalcSubresource's formula (that helper lives in d3dx12.h, not
		// vendored here -- MipSlice + ArraySlice * MipLevels, PlaneSlice always 0).
		const uint32_t dstMips = dst->desc.mipLevels ? dst->desc.mipLevels : 1;
		const uint32_t srcMips = src->desc.mipLevels ? src->desc.mipLevels : 1;
		const UINT dstSub = dstMip + dstArray * dstMips;
		const UINT srcSub = srcMip + srcArray * srcMips;

		auto barrier = [&](ID3D12Resource* res, UINT sub, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
		{
			if (before == after) return;
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = res;
			b.Transition.StateBefore = before;
			b.Transition.StateAfter = after;
			b.Transition.Subresource = sub;
			myBarriers.push_back(b);
		};

		const D3D12_RESOURCE_STATES dstBefore = dst->state;
		const D3D12_RESOURCE_STATES srcBefore = src->state;
		barrier(dst->res.Get(), dstSub, dstBefore, D3D12_RESOURCE_STATE_COPY_DEST);
		barrier(src->res.Get(), srcSub, srcBefore, D3D12_RESOURCE_STATE_COPY_SOURCE);

		D3D12_TEXTURE_COPY_LOCATION dstLoc = { dst->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		dstLoc.SubresourceIndex = dstSub;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = { src->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
		srcLoc.SubresourceIndex = srcSub;

		FlushBarriers();
		List()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

		barrier(dst->res.Get(), dstSub, D3D12_RESOURCE_STATE_COPY_DEST, dstBefore);
		barrier(src->res.Get(), srcSub, D3D12_RESOURCE_STATE_COPY_SOURCE, srcBefore);
	}
	void Dx12CommandContext::GenerateMips(SrvHandle, TextureHandle owner)
	{
		// DX12 has no driver-magic GenerateMips like D3D11's -- built explicitly
		// here as a chain of fullscreen-copy blits (bilinear-sampled, so each
		// pass is a cheap box-filter-like downsample), one mip level at a time:
		// bind mip N as a render target, mip N-1 as its source SRV, draw a
		// fullscreen triangle. Reuses the engine's existing PostprocessVS/
		// PostprocessCopyPS shader pair via DX11::Load*Shader (both backends
		// produce a valid RHI ShaderModuleHandle since this session's
		// DX11::Device-null-guard fix) rather than compiling anything new.
		// Handles a plain 2D texture (the font atlas) or a Tex2DArray/TexCube
		// (CubemapPrefilter's per-face captures) -- each array slice's mip
		// chain is generated independently (a face's mip 1 only ever
		// downsamples that SAME face's mip 0, never another face). 3D
		// textures are out of scope until a real call site needs them.
		TextureRec* t = myDevice.GetTexture(owner);
		if (!t || !t->res)
		{
			assert(false && "Dx12: GenerateMips -- invalid owner texture handle");
			return;
		}

		const D3D12_RESOURCE_DESC desc = t->res->GetDesc();
		const uint32_t mipCount = desc.MipLevels;
		const uint32_t baseW = static_cast<uint32_t>(desc.Width);
		const uint32_t baseH = desc.Height;
		const uint32_t arraySize = desc.DepthOrArraySize;
		if (mipCount <= 1 || baseW == 0 || baseH == 0)
			return;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		{
			assert(false && "Dx12: GenerateMips -- only Texture2D/2DArray/Cube supported, no call site needs 3D yet");
			return;
		}

		const VertexShader* vs = DX11::LoadVertexShader("Shaders/PostprocessVS");
		const PixelShader* ps = DX11::LoadPixelShader("Shaders/PostprocessCopyPS");
		if (!vs || !ps || !vs->module.IsValid() || !ps->module.IsValid())
		{
			assert(false && "Dx12: GenerateMips -- fullscreen-copy shader failed to load");
			return;
		}

		if (!myMipGenSampler.IsValid())
			myMipGenSampler = myDevice.CreateSampler({});   // default: Bilinear + Clamp

		const D3D12_RESOURCE_STATES before = t->state;

		// Per-subresource state tracking, local to this call -- TextureRec::state
		// only tracks one state for the WHOLE resource, but a mip chain
		// inherently needs different subresources in different states at once
		// (destination = RENDER_TARGET while source = PIXEL_SHADER_RESOURCE).
		// D3D12 subresource index = mip + arraySlice * mipCount.
		std::vector<D3D12_RESOURCE_STATES> subState(static_cast<size_t>(mipCount) * arraySize, before);

		auto barrierOne = [&](uint32_t sub, D3D12_RESOURCE_STATES to)
		{
			if (subState[sub] == to) return;
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = t->res.Get();
			b.Transition.StateBefore = subState[sub];
			b.Transition.StateAfter = to;
			b.Transition.Subresource = sub;
			List()->ResourceBarrier(1, &b);
			subState[sub] = to;
		};
		auto subIndex = [&](uint32_t mip, uint32_t slice) { return mip + slice * mipCount; };

		// Destinations (mip 1..N-1, every slice) must be RENDER_TARGET to be written.
		for (uint32_t slice = 0; slice < arraySize; ++slice)
			for (uint32_t mip = 1; mip < mipCount; ++mip)
				barrierOne(subIndex(mip, slice), D3D12_RESOURCE_STATE_RENDER_TARGET);

		for (uint32_t slice = 0; slice < arraySize; ++slice)
		{
			for (uint32_t mip = 1; mip < mipCount; ++mip)
			{
				const uint32_t srcMip = mip - 1;
				const uint32_t w = std::max<uint32_t>(1u, baseW >> mip);
				const uint32_t h = std::max<uint32_t>(1u, baseH >> mip);

				barrierOne(subIndex(srcMip, slice), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				RtvDesc rtvDesc = {}; rtvDesc.mipSlice = mip; rtvDesc.firstArraySlice = slice; rtvDesc.arraySize = 1;
				RtvHandle rtv = myDevice.CreateRtv(owner, rtvDesc);
				SrvDesc srvDesc = {};
				srvDesc.mostDetailedMip = srcMip; srvDesc.mipLevels = 1;
				srvDesc.firstArraySlice = slice; srvDesc.arraySize = 1;   // single slice -- forces the Tex2DArray SRV path even on a TexCube
				SrvHandle srcSrv = myDevice.CreateSrv(owner, srvDesc);

				// NOT SetRenderTargets()/SetShaderResource(): both would call
				// TransitionResource(owner, ...), which reads/writes the single
				// WHOLE-RESOURCE t->state this function is deliberately bypassing
				// (see subState above) -- owner's destination mip is RENDER_TARGET
				// while its source mip is simultaneously PIXEL_SHADER_RESOURCE, a
				// state TransitionResource cannot represent. Letting either call
				// through corrupts t->state for the rest of the resource's life
				// (found 2026-09-11: it desyncs to whatever state the last such
				// call happened to set, so every later bind of this texture
				// anywhere else in the engine emits a StateBefore that doesn't
				// match the resource's real state -- D3D12 debug layer flags it
				// immediately, and it's a real, confirmed cause of a DEVICE_HUNG
				// a few frames later). Bind directly instead; barrierOne() above
				// already emits the correct per-subresource transition.
				{
					uint32_t* rtvSlot = myDevice.GetRtvSlot(rtv);
					D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = myDevice.RtvCpuHandle(rtvSlot ? *rtvSlot : 0);
					myBoundRtvCount = 1;
					myBoundRtvFormats[0] = myDevice.GetRtvFormat(rtv);
					myBoundDsvFormat = myDevice.GetDsvFormat(DsvHandle{});
					SEH_OMSetRenderTargets(List(), 1, &rtvCpu, nullptr);
				}
				SetViewport(0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f);
				SetPrimitiveTopology(Topology::TriangleList);
				SetVertexBuffer(0, {}, 0, 0);
				SetIndexBuffer({}, Format::R32_UInt, 0);
				SetVertexShader(vs->module);
				SetPixelShader(ps->module);
				// PostprocessCopyPS: FullscreenTexture1, register(t1) -- see comment above for why this isn't SetShaderResource().
				myBoundSrv[1] = srcSrv;
				mySrvTableDirtyGraphics = true;
				SetSampler(ShaderStage::Pixel, 0, myMipGenSampler); // DefaultSampler, register(s0)
				Draw(3, 0);

				myDevice.Destroy(rtv);
				myDevice.Destroy(srcSrv);
			}
		}

		// Restore every subresource to its original (whole-resource) state --
		// t->state itself is left untouched, since it's true again once this
		// loop finishes.
		for (uint32_t slice = 0; slice < arraySize; ++slice)
			for (uint32_t mip = 0; mip < mipCount; ++mip)
				barrierOne(subIndex(mip, slice), before);
	}

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
		if (s == ResourceState::RaytracingAccelerationStructure) return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
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

	void Dx12CommandContext::FlushBarriers()
	{
		if (myBarriers.empty()) return;
		List()->ResourceBarrier((UINT)myBarriers.size(), myBarriers.data());
		myBarriers.clear();
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
		myBarriers.push_back(b);
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
		myBarriers.push_back(b);
		buf->state = to;
	}

	void Dx12CommandContext::UavBarrier(TextureHandle h)
	{
		TextureRec* t = myDevice.GetTexture(h);
		if (!t || !t->res) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		b.UAV.pResource = t->res.Get();
		myBarriers.push_back(b);
	}

	void Dx12CommandContext::UavBarrier(BufferHandle h)
	{
		BufferRec* buf = myDevice.GetBuffer(h);
		if (!buf || !buf->res) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		b.UAV.pResource = buf->res.Get();
		myBarriers.push_back(b);
	}

	// ---- timestamps / markers (milestone 3) ----
	void Dx12CommandContext::WriteTimestampBegin(TimestampQueryHandle h)
	{
		TimestampRec* r = myDevice.GetTimestampRec(h);
		if (!r) return;
		const uint32_t slot = r->queryIndex * 2;
		List()->EndQuery(myDevice.TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
		r->hasBegin = true;
		r->frame = myDevice.FrameIndex();
		myDevice.MarkTimestampSlotUsed(slot);
	}
	void Dx12CommandContext::WriteTimestampEnd(TimestampQueryHandle h)
	{
		TimestampRec* r = myDevice.GetTimestampRec(h);
		if (!r) return;
		const uint32_t slot = r->queryIndex * 2 + 1;
		List()->EndQuery(myDevice.TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
		r->hasEnd = true;
		myDevice.MarkTimestampSlotUsed(slot);
	}
	void Dx12CommandContext::PushMarker(const char* name)
	{
		(void)name;
#ifdef USE_PIX
		PIXBeginEvent(List(), PIX_COLOR_DEFAULT, "%s", name);
#endif
	}
	void Dx12CommandContext::PopMarker()
	{
#ifdef USE_PIX
		PIXEndEvent(List());
#endif
	}
}
