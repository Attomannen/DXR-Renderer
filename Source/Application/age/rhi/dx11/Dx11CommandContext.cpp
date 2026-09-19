#include "stdafx.h"
#include "age/rhi/dx11/Dx11CommandContext.h"
#include "age/rhi/dx11/Dx11Device.h"
#include "age/rhi/dx11/Dx11Format.h"
#include <age/graphics/DX11.h>

#include <cstring>
#include <string>

namespace Ag::rhi::dx11
{
	ID3D11DeviceContext* Dx11CommandContext::Ctx() { return myDevice.RawCtx(); }

	// ------------------------------------------------------------- targets / viewport / clears
	void Dx11CommandContext::SetRenderTargets(uint32_t count, const RtvHandle* rtvs, DsvHandle dsv)
	{
		ID3D11RenderTargetView* raw[8] = {};
		const uint32_t n = count > 8 ? 8 : count;
		for (uint32_t i = 0; i < n; ++i) raw[i] = rtvs ? myDevice.GetRtvPtr(rtvs[i]) : nullptr;
		Ctx()->OMSetRenderTargets(n, raw, myDevice.GetDsvPtr(dsv));
	}

	void Dx11CommandContext::SetViewport(float x, float y, float w, float h, float minZ, float maxZ)
	{
		D3D11_VIEWPORT vp = { x, y, w, h, minZ, maxZ };
		Ctx()->RSSetViewports(1, &vp);
	}

	void Dx11CommandContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
	{
		D3D11_RECT r = { x, y, x + (LONG)w, y + (LONG)h };
		Ctx()->RSSetScissorRects(1, &r);
	}

	void Dx11CommandContext::ClearRenderTarget(RtvHandle rtv, const float rgba[4])
	{
		if (ID3D11RenderTargetView* v = myDevice.GetRtvPtr(rtv)) Ctx()->ClearRenderTargetView(v, rgba);
	}

	void Dx11CommandContext::ClearUnorderedAccessFloat(UavHandle uav, const float rgba[4])
	{
		if (ID3D11UnorderedAccessView* v = myDevice.GetUavPtr(uav)) Ctx()->ClearUnorderedAccessViewFloat(v, rgba);
	}

	void Dx11CommandContext::ClearDepthStencil(DsvHandle dsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil)
	{
		if (ID3D11DepthStencilView* v = myDevice.GetDsvPtr(dsv))
		{
			UINT f = 0;
			if (clearDepth)   f |= D3D11_CLEAR_DEPTH;
			if (clearStencil) f |= D3D11_CLEAR_STENCIL;
			Ctx()->ClearDepthStencilView(v, f, depth, stencil);
		}
	}

	// ------------------------------------------------------------- pipeline + bindings
	void Dx11CommandContext::SetGraphicsPipeline(GraphicsPipelineHandle h)
	{
		GfxPipelineRec* p = myDevice.GetGfxPipeline(h);
		if (!p) return;
		Ctx()->IASetInputLayout(p->layout.Get());
		Ctx()->VSSetShader(p->vs, nullptr, 0);
		Ctx()->PSSetShader(p->ps, nullptr, 0);
		const float bf[4] = { 1,1,1,1 };
		Ctx()->OMSetBlendState(p->blend, bf, 0xFFFFFFFF);
		Ctx()->OMSetDepthStencilState(p->depth, 0);
		Ctx()->RSSetState(p->raster);
		Ctx()->IASetPrimitiveTopology(p->topo);
	}

	void Dx11CommandContext::SetComputePipeline(ComputePipelineHandle h)
	{
		ComputePipelineRec* p = myDevice.GetComputePipeline(h);
		Ctx()->CSSetShader(p ? p->cs : nullptr, nullptr, 0);
	}

	void Dx11CommandContext::SetBlendState(BlendMode m)
	{
		const float bf[4] = { 1, 1, 1, 1 };
		Ctx()->OMSetBlendState(myDevice.BlendFor(m), bf, 0xFFFFFFFF);
	}
	void Dx11CommandContext::SetDepthStencilState(DepthMode m)
	{
		Ctx()->OMSetDepthStencilState(myDevice.DepthFor(m), 0);
	}
	void Dx11CommandContext::SetRasterizerState(RasterMode m)
	{
		Ctx()->RSSetState(myDevice.RasterFor(m));
	}
	void Dx11CommandContext::SetVertexShader(ShaderModuleHandle h)
	{
		ShaderRec* r = myDevice.GetShader(h);
		Ctx()->VSSetShader(r ? r->vs.Get() : nullptr, nullptr, 0);
	}
	void Dx11CommandContext::SetPixelShader(ShaderModuleHandle h)
	{
		ShaderRec* r = myDevice.GetShader(h);
		Ctx()->PSSetShader(r ? r->ps.Get() : nullptr, nullptr, 0);
	}
	void Dx11CommandContext::SetInputLayout(ShaderModuleHandle vs, const InputElement* elems, uint32_t count)
	{
		if (!elems || !count)
		{
			Ctx()->IASetInputLayout(nullptr);
			return;
		}
		ShaderRec* r = myDevice.GetShader(vs);
		const void* code = r ? r->bytecode.data() : nullptr;
		const uint32_t size = r ? (uint32_t)r->bytecode.size() : 0;
		void* layout = myDevice.CreateInputLayoutNative(elems, count, code, size);   // cached
		Ctx()->IASetInputLayout(static_cast<ID3D11InputLayout*>(layout));
		if (layout) static_cast<ID3D11InputLayout*>(layout)->Release();   // cache keeps the owning ref
	}

	static void BindCB(ID3D11DeviceContext* c, ShaderStage s, uint32_t slot, ID3D11Buffer* b)
	{
		if (HasStage(s, ShaderStage::Vertex))  c->VSSetConstantBuffers(slot, 1, &b);
		if (HasStage(s, ShaderStage::Pixel))   c->PSSetConstantBuffers(slot, 1, &b);
		if (HasStage(s, ShaderStage::Compute)) c->CSSetConstantBuffers(slot, 1, &b);
	}

	void Dx11CommandContext::SetConstantBuffer(ShaderStage s, uint32_t slot, BufferHandle h)
	{
		BufferRec* b = myDevice.GetBuffer(h);
		BindCB(Ctx(), s, slot, b ? b->res.Get() : nullptr);
	}

	void Dx11CommandContext::SetDynamicConstantBuffer(ShaderStage s, uint32_t slot, const DynamicAlloc& a)
	{
		BufferRec* b = myDevice.GetBuffer(a.buffer);
		ID3D11Buffer* raw = b ? b->res.Get() : nullptr;
		ID3D11DeviceContext1* c1 = myDevice.RawCtx1();
		if (!raw || !c1)
		{
			BindCB(Ctx(), s, slot, raw);   // fallback: whole-buffer bind
			return;
		}
		// Bind the sub-range [offset, offset+size). Offsets/counts are in 16-float
		// constants and must be multiples of 16 — AllocateDynamicConstants aligns
		// offset+size to 256 bytes so this always holds.
		UINT first = a.offset / 16;
		UINT num   = ((a.size ? a.size : 16u) + 255u) / 256u * 16u;
		if (HasStage(s, ShaderStage::Vertex))  c1->VSSetConstantBuffers1(slot, 1, &raw, &first, &num);
		if (HasStage(s, ShaderStage::Pixel))   c1->PSSetConstantBuffers1(slot, 1, &raw, &first, &num);
		if (HasStage(s, ShaderStage::Compute)) c1->CSSetConstantBuffers1(slot, 1, &raw, &first, &num);
	}

	static void BindSRV(ID3D11DeviceContext* c, ShaderStage s, uint32_t slot, uint32_t count, ID3D11ShaderResourceView* const* v)
	{
		if (HasStage(s, ShaderStage::Vertex))  c->VSSetShaderResources(slot, count, v);
		if (HasStage(s, ShaderStage::Pixel))   c->PSSetShaderResources(slot, count, v);
		if (HasStage(s, ShaderStage::Compute)) c->CSSetShaderResources(slot, count, v);
	}

	void Dx11CommandContext::SetShaderResource(ShaderStage s, uint32_t slot, SrvHandle h)
	{
		ID3D11ShaderResourceView* v = myDevice.GetSrvPtr(h);
		BindSRV(Ctx(), s, slot, 1, &v);
	}

	void Dx11CommandContext::SetShaderResources(ShaderStage s, uint32_t firstSlot, uint32_t count, const SrvHandle* h)
	{
		ID3D11ShaderResourceView* raw[16] = {};
		const uint32_t n = count > 16 ? 16 : count;
		for (uint32_t i = 0; i < n; ++i) raw[i] = h ? myDevice.GetSrvPtr(h[i]) : nullptr;
		BindSRV(Ctx(), s, firstSlot, n, raw);
	}

	void Dx11CommandContext::SetUnorderedAccess(uint32_t slot, UavHandle h)
	{
		ID3D11UnorderedAccessView* v = myDevice.GetUavPtr(h);
		Ctx()->CSSetUnorderedAccessViews(slot, 1, &v, nullptr);
	}

	void Dx11CommandContext::SetUnorderedAccesses(uint32_t firstSlot, uint32_t count, const UavHandle* h)
	{
		ID3D11UnorderedAccessView* raw[8] = {};
		const uint32_t n = count > 8 ? 8 : count;
		for (uint32_t i = 0; i < n; ++i) raw[i] = h ? myDevice.GetUavPtr(h[i]) : nullptr;
		Ctx()->CSSetUnorderedAccessViews(firstSlot, n, raw, nullptr);
	}

	void Dx11CommandContext::SetSampler(ShaderStage s, uint32_t slot, SamplerHandle h)
	{
		ID3D11SamplerState* v = myDevice.GetSamplerPtr(h);
		if (HasStage(s, ShaderStage::Vertex))  Ctx()->VSSetSamplers(slot, 1, &v);
		if (HasStage(s, ShaderStage::Pixel))   Ctx()->PSSetSamplers(slot, 1, &v);
		if (HasStage(s, ShaderStage::Compute)) Ctx()->CSSetSamplers(slot, 1, &v);
	}

	// ------------------------------------------------------------- geometry
	void Dx11CommandContext::SetVertexBuffer(uint32_t slot, BufferHandle h, uint32_t stride, uint32_t offset)
	{
		BufferRec* b = myDevice.GetBuffer(h);
		ID3D11Buffer* raw = b ? b->res.Get() : nullptr;
		Ctx()->IASetVertexBuffers(slot, 1, &raw, &stride, &offset);
	}

	void Dx11CommandContext::SetIndexBuffer(BufferHandle h, Format indexFormat, uint32_t offset)
	{
		BufferRec* b = myDevice.GetBuffer(h);
		Ctx()->IASetIndexBuffer(b ? b->res.Get() : nullptr, ToDxgi(indexFormat), offset);
	}

	void Dx11CommandContext::SetPrimitiveTopology(Topology t)
	{
		D3D11_PRIMITIVE_TOPOLOGY d = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		switch (t)
		{
		case Topology::TriangleList:  d = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
		case Topology::TriangleStrip: d = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
		case Topology::LineList:      d = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
		case Topology::LineStrip:     d = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
		case Topology::PointList:     d = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
		}
		Ctx()->IASetPrimitiveTopology(d);
	}

	// ------------------------------------------------------------- draw / dispatch
	void Dx11CommandContext::Draw(uint32_t vertexCount, uint32_t startVertex)
	{
		Ctx()->Draw(vertexCount, startVertex);
		Ag::DX11::LogDrawCall();
	}
	void Dx11CommandContext::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount,
	                                      uint32_t startVertex, uint32_t startInstance)
	{
		Ctx()->DrawInstanced(vertexCountPerInstance, instanceCount, startVertex, startInstance);
		Ag::DX11::LogDrawCall();
	}
	void Dx11CommandContext::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
	{
		Ctx()->DrawIndexed(indexCount, startIndex, baseVertex);
		Ag::DX11::LogDrawCall();
	}
	void Dx11CommandContext::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
	                                              int32_t baseVertex, uint32_t startInstance)
	{
		Ctx()->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
		Ag::DX11::LogDrawCall();
	}
	void Dx11CommandContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
	{
		Ctx()->Dispatch(x, y, z);
	}

	// ------------------------------------------------------------- resource ops
	void Dx11CommandContext::UpdateBuffer(BufferHandle h, const void* data, uint32_t byteSize, uint32_t dstOffset)
	{
		BufferRec* b = myDevice.GetBuffer(h);
		if (!b || !b->res) return;
		if (b->desc.memory == MemoryType::Upload)
		{
			D3D11_MAPPED_SUBRESOURCE m = {};
			if (SUCCEEDED(Ctx()->Map(b->res.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			{
				memcpy((char*)m.pData + dstOffset, data, byteSize);
				Ctx()->Unmap(b->res.Get(), 0);
			}
		}
		else
		{
			D3D11_BOX box = { dstOffset, 0, 0, dstOffset + byteSize, 1, 1 };
			Ctx()->UpdateSubresource(b->res.Get(), 0, dstOffset ? &box : nullptr, data, 0, 0);
		}
	}

	void Dx11CommandContext::UpdateTexture(TextureHandle h, const void* data, uint32_t rowPitch)
	{
		TextureRec* t = myDevice.GetTexture(h);
		if (!t || !t->res) return;
		Ctx()->UpdateSubresource(t->res.Get(), 0, nullptr, data, rowPitch, 0);
	}

	void Dx11CommandContext::CopyTexture(TextureHandle dst, TextureHandle src)
	{
		TextureRec* d = myDevice.GetTexture(dst);
		TextureRec* s = myDevice.GetTexture(src);
		if (d && s && d->res && s->res) Ctx()->CopyResource(d->res.Get(), s->res.Get());
	}

	void Dx11CommandContext::CopyTextureRegion(TextureHandle dst, uint32_t dstMip, uint32_t dstArray,
	                                           TextureHandle src, uint32_t srcMip, uint32_t srcArray)
	{
		TextureRec* d = myDevice.GetTexture(dst);
		TextureRec* s = myDevice.GetTexture(src);
		if (!d || !s || !d->res || !s->res) return;
		const UINT dSub = D3D11CalcSubresource(dstMip, dstArray, d->desc.mipLevels ? d->desc.mipLevels : 1);
		const UINT sSub = D3D11CalcSubresource(srcMip, srcArray, s->desc.mipLevels ? s->desc.mipLevels : 1);
		Ctx()->CopySubresourceRegion(d->res.Get(), dSub, 0, 0, 0, s->res.Get(), sSub, nullptr);
	}

	void Dx11CommandContext::GenerateMips(SrvHandle h, TextureHandle)
	{
		if (ID3D11ShaderResourceView* v = myDevice.GetSrvPtr(h)) Ctx()->GenerateMips(v);
	}

	// ------------------------------------------------------------- timestamps
	void Dx11CommandContext::WriteTimestampBegin(TimestampQueryHandle h)
	{
		if (TimestampRec* r = myDevice.GetTimestampRec(h))
		{
			Ctx()->End(r->begin.Get());   // timestamp queries are "inserted" with End()
			r->frame = myDevice.FrameCounter();
		}
	}
	void Dx11CommandContext::WriteTimestampEnd(TimestampQueryHandle h)
	{
		if (TimestampRec* r = myDevice.GetTimestampRec(h))
			Ctx()->End(r->end.Get());
	}

	// ------------------------------------------------------------- markers
	ID3DUserDefinedAnnotation* Dx11CommandContext::Annotation()
	{
		if (!myAnnotationTried)
		{
			myAnnotationTried = true;
			Ctx()->QueryInterface(IID_PPV_ARGS(myAnnotation.GetAddressOf()));
		}
		return myAnnotation.Get();
	}
	void Dx11CommandContext::PushMarker(const char* name)
	{
		if (ID3DUserDefinedAnnotation* a = Annotation())
		{
			const std::wstring w(name, name + std::char_traits<char>::length(name));
			a->BeginEvent(w.c_str());
		}
	}
	void Dx11CommandContext::PopMarker()
	{
		if (ID3DUserDefinedAnnotation* a = Annotation())
			a->EndEvent();
	}
}
