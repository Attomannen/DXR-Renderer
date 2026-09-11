#include "stdafx.h"
#include "tge/rhi/dx11/Dx11Device.h"
#include "tge/rhi/dx11/Dx11CommandContext.h"
#include "tge/rhi/dx11/Dx11Format.h"

#include <tge/graphics/DX11.h>
#include <tge/graphics/RenderTarget.h>
#include <tge/graphics/DepthBuffer.h>

#include <cassert>
#include <cstring>

namespace Tga::rhi::dx11
{
	// ------------------------------------------------------------------ helpers
	static uint32_t Align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

	static void HashCombine(uint64_t& h, uint64_t v)
	{
		h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	}
	static uint64_t HashDesc(const GraphicsPipelineDesc& d)
	{
		uint64_t h = 1469598103934665603ull;
		HashCombine(h, ((uint64_t)d.vs.index << 32) | d.vs.generation);
		HashCombine(h, ((uint64_t)d.ps.index << 32) | d.ps.generation);
		HashCombine(h, (uint64_t)d.blend | ((uint64_t)d.depth << 8) | ((uint64_t)d.raster << 16)
		             | ((uint64_t)d.topology << 24) | ((uint64_t)d.alphaToCoverage << 32));
		HashCombine(h, d.renderTargetCount);
		for (uint32_t i = 0; i < 8; ++i) HashCombine(h, (uint64_t)d.rtvFormats[i]);
		HashCombine(h, (uint64_t)d.dsvFormat);
		HashCombine(h, d.inputLayoutCount);
		for (uint32_t i = 0; i < d.inputLayoutCount; ++i)
		{
			const InputElement& e = d.inputLayout[i];
			uint64_t eh = 1469598103934665603ull;
			for (const char* p = e.semanticName; p && *p; ++p) HashCombine(eh, (uint8_t)*p);
			HashCombine(eh, e.semanticIndex);
			HashCombine(eh, (uint64_t)e.format);
			HashCombine(eh, e.inputSlot);
			HashCombine(eh, e.alignedByteOffset);
			HashCombine(eh, (uint64_t)e.perInstance | ((uint64_t)e.instanceStepRate << 1));
			HashCombine(h, eh);
		}
		return h;
	}

	// ------------------------------------------------------------------ ctor / dtor
	Dx11Device::Dx11Device(const DeviceDesc&)
	{
		myDevice = Tga::DX11::Device;
		myCtx    = Tga::DX11::Context;
		assert(myDevice && myCtx && "Tga::DX11 must be Init()ed before rhi::CreateDevice(DX11)");
		myCtx->QueryInterface(IID_PPV_ARGS(myCtx1.GetAddressOf()));   // D3D11.1: offset CB binds
		assert(myCtx1 && "ID3D11DeviceContext1 required (D3D11.1)");
		myContext = std::make_unique<Dx11CommandContext>(*this);
		AdoptSwapchainResources();
	}

	Dx11Device::~Dx11Device() = default;

	// ------------------------------------------------------------------ swapchain adoption
	void Dx11Device::AdoptSwapchainResources()
	{
		// Release any previously-adopted handles (Resize path).
		myBuffers.Free(BufferHandle{});   // no-op for null
		mySrvs.Free(SrvHandle{});
		if (myBackBufferRtvSrgb.IsValid())  { myRtvs.Free(myBackBufferRtvSrgb);  myBackBufferRtvSrgb = {}; }
		if (myBackBufferRtvLinear.IsValid()){ myRtvs.Free(myBackBufferRtvLinear);myBackBufferRtvLinear = {}; }
		if (myDepthDsv.IsValid())           { myDsvs.Free(myDepthDsv);           myDepthDsv = {}; }
		if (myBackBufferTex.IsValid())      { myTextures.Free(myBackBufferTex);  myBackBufferTex = {}; }
		if (myDepthTex.IsValid())           { myTextures.Free(myDepthTex);       myDepthTex = {}; }

		RenderTarget* bb  = Tga::DX11::BackBuffer;
		RenderTarget* bbL = Tga::DX11::BackBufferNoSrgbConversion;
		Tga::DepthBuffer* db = Tga::DX11::DepthBuffer;
		// During a resize the wrappers are transiently emptied -> nothing to adopt
		// yet; the Free() calls above already released the stale handles.
		if (!bb || !bb->GetRenderTargetView()) return;

		// Backbuffer texture handle (resource not owned here).
		{
			ComPtr<ID3D11Resource> res;
			if (ID3D11ShaderResourceView* srv = bb->GetShaderResourceView())
				srv->GetResource(res.GetAddressOf());
			TextureRec rec;
			rec.res = res;
			rec.ownsResource = false;
			const Vector2ui r = Tga::DX11::GetResolution();
			rec.desc.width = r.x; rec.desc.height = r.y;
			rec.desc.format = Format::B8G8R8A8_UNorm;
			rec.desc.bind = TextureBind::RenderTarget | TextureBind::ShaderResource;
			myBackBufferTex = myTextures.Alloc(std::move(rec));
		}
		if (ID3D11RenderTargetView* rtv = bb->GetRenderTargetView())
			myBackBufferRtvSrgb = myRtvs.Alloc(ComPtr<ID3D11RenderTargetView>(rtv));
		if (bbL)
			if (ID3D11RenderTargetView* rtv = bbL->GetRenderTargetView())
				myBackBufferRtvLinear = myRtvs.Alloc(ComPtr<ID3D11RenderTargetView>(rtv));
		if (!myBackBufferRtvLinear.IsValid()) myBackBufferRtvLinear = myBackBufferRtvSrgb;

		if (db)
		{
			ComPtr<ID3D11Resource> res;
			if (ID3D11ShaderResourceView* srv = db->GetShaderResourceView())
				srv->GetResource(res.GetAddressOf());
			TextureRec rec;
			rec.res = res;
			rec.ownsResource = false;
			const Vector2ui r = Tga::DX11::GetResolution();
			rec.desc.width = r.x; rec.desc.height = r.y;
			rec.desc.format = Format::D32_Float;
			rec.desc.bind = TextureBind::DepthStencil | TextureBind::ShaderResource;
			myDepthTex = myTextures.Alloc(std::move(rec));
			if (ID3D11DepthStencilView* dsv = db->GetDepthStencilView())
				myDepthDsv = myDsvs.Alloc(ComPtr<ID3D11DepthStencilView>(dsv));
		}
	}

	Vector2ui Dx11Device::GetResolution() const { return Tga::DX11::GetResolution(); }

	bool Dx11Device::Resize(uint32_t, uint32_t)
	{
		// Stage 1: the actual swapchain resize is still driven by Tga::DX11.
		// Re-adopt whatever it produced.
		AdoptSwapchainResources();
		return true;
	}

	// ------------------------------------------------------------------ buffers
	BufferHandle Dx11Device::CreateBuffer(const BufferDesc& d, const void* initialData)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)d.byteSize;
		bd.StructureByteStride = d.stride;

		UINT bind = 0;
		if (HasUsage(d.usage, BufferUsage::Vertex))     bind |= D3D11_BIND_VERTEX_BUFFER;
		if (HasUsage(d.usage, BufferUsage::Index))      bind |= D3D11_BIND_INDEX_BUFFER;
		if (HasUsage(d.usage, BufferUsage::Constant))   bind |= D3D11_BIND_CONSTANT_BUFFER;
		if (HasUsage(d.usage, BufferUsage::Structured) || HasUsage(d.usage, BufferUsage::ByteAddress))
			bind |= D3D11_BIND_SHADER_RESOURCE;
		if (HasUsage(d.usage, BufferUsage::UAV))        bind |= D3D11_BIND_UNORDERED_ACCESS;
		bd.BindFlags = bind;

		if (HasUsage(d.usage, BufferUsage::Structured))
			bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		if (HasUsage(d.usage, BufferUsage::ByteAddress))
			bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (HasUsage(d.usage, BufferUsage::Indirect))
			bd.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

		if (d.memory == MemoryType::Upload)
		{
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		}
		else if (d.memory == MemoryType::Readback)
		{
			bd.Usage = D3D11_USAGE_STAGING;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			bd.BindFlags = 0;
		}
		else
		{
			bd.Usage = D3D11_USAGE_DEFAULT;
		}

		D3D11_SUBRESOURCE_DATA srd = {};
		srd.pSysMem = initialData;
		BufferRec rec;
		rec.desc = d;
		HRESULT hr = myDevice->CreateBuffer(&bd, initialData ? &srd : nullptr, rec.res.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return myBuffers.Alloc(std::move(rec));
	}

	// ------------------------------------------------------------------ textures
	TextureHandle Dx11Device::CreateTexture(const TextureDesc& d, const SubresourceData* initial, uint32_t initialCount)
	{
		const bool depth = IsDepth(d.format);
		UINT bind = 0;
		if (HasBind(d.bind, TextureBind::ShaderResource))  bind |= D3D11_BIND_SHADER_RESOURCE;
		if (HasBind(d.bind, TextureBind::RenderTarget))    bind |= D3D11_BIND_RENDER_TARGET;
		if (HasBind(d.bind, TextureBind::DepthStencil))    bind |= D3D11_BIND_DEPTH_STENCIL;
		if (HasBind(d.bind, TextureBind::UnorderedAccess)) bind |= D3D11_BIND_UNORDERED_ACCESS;

		std::vector<D3D11_SUBRESOURCE_DATA> srd;
		if (initial && initialCount)
		{
			srd.resize(initialCount);
			for (uint32_t i = 0; i < initialCount; ++i)
			{
				srd[i].pSysMem = initial[i].data;
				srd[i].SysMemPitch = initial[i].rowPitch;
				srd[i].SysMemSlicePitch = initial[i].slicePitch;
			}
		}

		TextureRec rec;
		rec.desc = d;

		if (d.dimension == TextureDimension::Tex3D)
		{
			D3D11_TEXTURE3D_DESC td = {};
			td.Width = d.width; td.Height = d.height; td.Depth = d.depthOrArraySize;
			td.MipLevels = d.mipLevels;
			td.Format = ToDxgi(d.format);
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = bind;
			ComPtr<ID3D11Texture3D> tex;
			HRESULT hr = myDevice->CreateTexture3D(&td, srd.empty() ? nullptr : srd.data(), tex.GetAddressOf());
			assert(SUCCEEDED(hr)); (void)hr;
			rec.res = tex;
		}
		else
		{
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = d.width; td.Height = d.height;
			td.MipLevels = d.mipLevels;
			td.ArraySize = (d.dimension == TextureDimension::TexCube) ? 6 * d.depthOrArraySize : d.depthOrArraySize;
			td.Format = depth ? ToTypeless(d.format) : ToDxgi(d.format);
			td.SampleDesc.Count = d.sampleCount ? d.sampleCount : 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = bind;
			if (d.dimension == TextureDimension::TexCube) td.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
			if (d.mipLevels != 1 && HasBind(d.bind, TextureBind::RenderTarget))
				td.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
			ComPtr<ID3D11Texture2D> tex;
			HRESULT hr = myDevice->CreateTexture2D(&td, srd.empty() ? nullptr : srd.data(), tex.GetAddressOf());
			assert(SUCCEEDED(hr)); (void)hr;
			rec.res = tex;
		}
		return myTextures.Alloc(std::move(rec));
	}

	// ------------------------------------------------------------------ views
	SrvHandle Dx11Device::CreateSrv(TextureHandle h, const SrvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
		const Format vf = d.formatOverride != Format::Unknown ? d.formatOverride
		                : (IsDepth(t->desc.format) ? FromDxgi(ToDepthSrvFormat(t->desc.format)) : t->desc.format);
		vd.Format = IsDepth(t->desc.format) ? ToDepthSrvFormat(t->desc.format) : ToDxgi(vf);

		const uint32_t mips = (d.mipLevels == kAllMips) ? (t->desc.mipLevels ? t->desc.mipLevels : 1) : d.mipLevels;
		if (t->desc.dimension == TextureDimension::Tex3D)
		{
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
			vd.Texture3D.MostDetailedMip = d.mostDetailedMip;
			vd.Texture3D.MipLevels = mips;
		}
		else if (d.asCube || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			vd.TextureCube.MostDetailedMip = d.mostDetailedMip;
			vd.TextureCube.MipLevels = mips;
		}
		else if (t->desc.dimension == TextureDimension::Tex2DArray)
		{
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2DArray.MipLevels = mips;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = (d.arraySize == kAllSlices) ? t->desc.depthOrArraySize : d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MostDetailedMip = d.mostDetailedMip;
			vd.Texture2D.MipLevels = mips;
		}
		ComPtr<ID3D11ShaderResourceView> srv;
		HRESULT hr = myDevice->CreateShaderResourceView(t->res.Get(), &vd, srv.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return mySrvs.Alloc(std::move(srv));
	}

	SrvHandle Dx11Device::CreateSrv(BufferHandle h, const SrvDesc& d)
	{
		BufferRec* b = myBuffers.Get(h);
		if (!b || !b->res) return {};
		D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
		vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		vd.Format = HasUsage(b->desc.usage, BufferUsage::Structured) ? DXGI_FORMAT_UNKNOWN
		          : (d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : DXGI_FORMAT_R32_UINT);
		vd.Buffer.FirstElement = d.bufferFirstElement;
		vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements
		    : (b->desc.stride ? (UINT)(b->desc.byteSize / b->desc.stride) : (UINT)(b->desc.byteSize / 4));
		ComPtr<ID3D11ShaderResourceView> srv;
		HRESULT hr = myDevice->CreateShaderResourceView(b->res.Get(), &vd, srv.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return mySrvs.Alloc(std::move(srv));
	}

	UavHandle Dx11Device::CreateUav(TextureHandle h, const UavDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : ToDxgi(t->desc.format);
		if (t->desc.dimension == TextureDimension::Tex3D)
		{
			vd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			vd.Texture3D.MipSlice = d.mipSlice;
			vd.Texture3D.FirstWSlice = d.firstArraySlice;
			vd.Texture3D.WSize = (d.arraySize == kAllSlices) ? t->desc.depthOrArraySize : d.arraySize;
		}
		else if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = (d.arraySize == kAllSlices)
			    ? ((t->desc.dimension == TextureDimension::TexCube) ? 6u : t->desc.depthOrArraySize) : d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}
		ComPtr<ID3D11UnorderedAccessView> uav;
		HRESULT hr = myDevice->CreateUnorderedAccessView(t->res.Get(), &vd, uav.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return myUavs.Alloc(std::move(uav));
	}

	UavHandle Dx11Device::CreateUav(BufferHandle h, const UavDesc& d)
	{
		BufferRec* b = myBuffers.Get(h);
		if (!b || !b->res) return {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC vd = {};
		vd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		vd.Format = HasUsage(b->desc.usage, BufferUsage::Structured) ? DXGI_FORMAT_UNKNOWN
		          : (d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : DXGI_FORMAT_R32_UINT);
		vd.Buffer.FirstElement = d.bufferFirstElement;
		vd.Buffer.NumElements = d.bufferNumElements ? d.bufferNumElements
		    : (b->desc.stride ? (UINT)(b->desc.byteSize / b->desc.stride) : (UINT)(b->desc.byteSize / 4));
		if (HasUsage(b->desc.usage, BufferUsage::ByteAddress)) vd.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
		ComPtr<ID3D11UnorderedAccessView> uav;
		HRESULT hr = myDevice->CreateUnorderedAccessView(b->res.Get(), &vd, uav.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return myUavs.Alloc(std::move(uav));
	}

	RtvHandle Dx11Device::CreateRtv(TextureHandle h, const RtvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D11_RENDER_TARGET_VIEW_DESC vd = {};
		vd.Format = d.formatOverride != Format::Unknown ? ToDxgi(d.formatOverride) : ToDxgi(t->desc.format);
		if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = d.arraySize;
		}
		else if (t->desc.dimension == TextureDimension::Tex3D)
		{
			vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
			vd.Texture3D.MipSlice = d.mipSlice;
			vd.Texture3D.FirstWSlice = d.firstArraySlice;
			vd.Texture3D.WSize = d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}
		ComPtr<ID3D11RenderTargetView> rtv;
		HRESULT hr = myDevice->CreateRenderTargetView(t->res.Get(), &vd, rtv.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return myRtvs.Alloc(std::move(rtv));
	}

	DsvHandle Dx11Device::CreateDsv(TextureHandle h, const DsvDesc& d)
	{
		TextureRec* t = myTextures.Get(h);
		if (!t || !t->res) return {};
		D3D11_DEPTH_STENCIL_VIEW_DESC vd = {};
		vd.Format = ToDsvFormat(d.formatOverride != Format::Unknown ? d.formatOverride : t->desc.format);
		if (d.readOnly) vd.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
		if (t->desc.dimension == TextureDimension::Tex2DArray || t->desc.dimension == TextureDimension::TexCube)
		{
			vd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			vd.Texture2DArray.MipSlice = d.mipSlice;
			vd.Texture2DArray.FirstArraySlice = d.firstArraySlice;
			vd.Texture2DArray.ArraySize = d.arraySize;
		}
		else
		{
			vd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			vd.Texture2D.MipSlice = d.mipSlice;
		}
		ComPtr<ID3D11DepthStencilView> dsv;
		HRESULT hr = myDevice->CreateDepthStencilView(t->res.Get(), &vd, dsv.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return myDsvs.Alloc(std::move(dsv));
	}

	SamplerHandle Dx11Device::CreateSampler(const SamplerDesc& d)
	{
		D3D11_SAMPLER_DESC sd = {};
		switch (d.filter)
		{
		case FilterMode::Point:              sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; break;
		case FilterMode::Bilinear:           sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT; break;
		case FilterMode::Trilinear:          sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; break;
		case FilterMode::Anisotropic:        sd.Filter = D3D11_FILTER_ANISOTROPIC; break;
		case FilterMode::ComparisonBilinear: sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; break;
		}
		if (d.comparison && d.filter != FilterMode::ComparisonBilinear)
			sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		D3D11_TEXTURE_ADDRESS_MODE am = D3D11_TEXTURE_ADDRESS_CLAMP;
		switch (d.address)
		{
		case AddressMode::Clamp:  am = D3D11_TEXTURE_ADDRESS_CLAMP; break;
		case AddressMode::Wrap:   am = D3D11_TEXTURE_ADDRESS_WRAP; break;
		case AddressMode::Mirror: am = D3D11_TEXTURE_ADDRESS_MIRROR; break;
		case AddressMode::Border: am = D3D11_TEXTURE_ADDRESS_BORDER; break;
		}
		sd.AddressU = sd.AddressV = sd.AddressW = am;
		sd.MipLODBias = d.mipBias;
		sd.MaxAnisotropy = d.maxAnisotropy ? d.maxAnisotropy : 1;
		sd.ComparisonFunc = (d.comparison || d.filter == FilterMode::ComparisonBilinear)
		                    ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
		for (int i = 0; i < 4; ++i) sd.BorderColor[i] = d.borderColor[i];
		sd.MinLOD = 0.f;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> s;
		HRESULT hr = myDevice->CreateSamplerState(&sd, s.GetAddressOf());
		assert(SUCCEEDED(hr)); (void)hr;
		return mySamplers.Alloc(std::move(s));
	}

	// ------------------------------------------------------------------ shaders
	ShaderModuleHandle Dx11Device::CreateShaderModule(ShaderKind kind, const void* bytecode, size_t size)
	{
		ShaderRec rec;
		rec.kind = kind;
		rec.bytecode.assign((const char*)bytecode, size);
		HRESULT hr = S_OK;
		switch (kind)
		{
		case ShaderKind::Vertex:  hr = myDevice->CreateVertexShader(bytecode, size, nullptr, rec.vs.GetAddressOf()); break;
		case ShaderKind::Pixel:   hr = myDevice->CreatePixelShader(bytecode, size, nullptr, rec.ps.GetAddressOf()); break;
		case ShaderKind::Compute: hr = myDevice->CreateComputeShader(bytecode, size, nullptr, rec.cs.GetAddressOf()); break;
		}
		assert(SUCCEEDED(hr)); (void)hr;
		return myShaders.Alloc(std::move(rec));
	}

	// ------------------------------------------------------------------ pipeline state resolution
	void Dx11Device::BuildStates()
	{
		if (myStatesBuilt) return;
		myStatesBuilt = true;

		D3D11_BLEND_DESC bd = {};
		auto& rt0 = bd.RenderTarget[0];
		rt0.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		rt0.BlendEnable = FALSE;
		rt0.SrcBlend = rt0.DestBlend = D3D11_BLEND_ZERO; rt0.BlendOp = D3D11_BLEND_OP_ADD;
		rt0.SrcBlendAlpha = rt0.DestBlendAlpha = D3D11_BLEND_ZERO; rt0.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		myDevice->CreateBlendState(&bd, myBlendStates[(int)BlendMode::Disabled].GetAddressOf());

		bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
		myDevice->CreateBlendState(&bd, myBlendStates[(int)BlendMode::AlphaBlend].GetAddressOf());

		bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		myDevice->CreateBlendState(&bd, myBlendStates[(int)BlendMode::AdditiveBlend].GetAddressOf());

		auto mkDepth = [&](D3D11_COMPARISON_FUNC f, bool write, int slot)
		{
			D3D11_DEPTH_STENCIL_DESC dd = {};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			dd.DepthFunc = f;
			myDevice->CreateDepthStencilState(&dd, myDepthStates[slot].GetAddressOf());
		};
		mkDepth(D3D11_COMPARISON_LESS,          true,  (int)DepthMode::WriteLess);
		mkDepth(D3D11_COMPARISON_LESS_EQUAL,    true,  (int)DepthMode::WriteLessOrEqual);
		mkDepth(D3D11_COMPARISON_LESS,          false, (int)DepthMode::ReadOnlyLess);
		mkDepth(D3D11_COMPARISON_LESS_EQUAL,    false, (int)DepthMode::ReadOnlyLessOrEqual);

		auto mkRaster = [&](D3D11_FILL_MODE fill, D3D11_CULL_MODE cull, int slot)
		{
			D3D11_RASTERIZER_DESC rd = {};
			rd.FillMode = fill; rd.CullMode = cull;
			rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
			myDevice->CreateRasterizerState(&rd, myRasterStates[slot].GetAddressOf());
		};
		// BackfaceCulling stays null (matches GraphicsStateStack: D3D11 default state).
		mkRaster(D3D11_FILL_SOLID,     D3D11_CULL_FRONT, (int)RasterMode::FrontFaceCulling);
		mkRaster(D3D11_FILL_SOLID,     D3D11_CULL_NONE,  (int)RasterMode::NoFaceCulling);
		mkRaster(D3D11_FILL_WIREFRAME, D3D11_CULL_BACK,  (int)RasterMode::Wireframe);
		mkRaster(D3D11_FILL_WIREFRAME, D3D11_CULL_NONE,  (int)RasterMode::WireframeNoCulling);
	}

	ID3D11BlendState*        Dx11Device::BlendFor(BlendMode m) { BuildStates(); return myBlendStates[(int)m].Get(); }
	ID3D11DepthStencilState* Dx11Device::DepthFor(DepthMode m) { BuildStates(); return myDepthStates[(int)m].Get(); }
	ID3D11RasterizerState*   Dx11Device::RasterFor(RasterMode m) { BuildStates(); return myRasterStates[(int)m].Get(); }
	D3D11_PRIMITIVE_TOPOLOGY Dx11Device::TopoFor(Topology t)
	{
		switch (t)
		{
		case Topology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case Topology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case Topology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case Topology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case Topology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		}
		return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	GraphicsPipelineHandle Dx11Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& d)
	{
		const uint64_t key = HashDesc(d);
		if (auto it = myGfxCache.find(key); it != myGfxCache.end()) return it->second;

		GfxPipelineRec rec;
		if (ShaderRec* v = myShaders.Get(d.vs)) rec.vs = v->vs.Get();
		if (ShaderRec* p = myShaders.Get(d.ps)) rec.ps = p->ps.Get();
		rec.blend = BlendFor(d.blend);
		rec.depth = DepthFor(d.depth);
		rec.raster = RasterFor(d.raster);
		rec.topo = TopoFor(d.topology);
		rec.alphaToCoverage = d.alphaToCoverage;

		if (d.inputLayout && d.inputLayoutCount)
		{
			const void* blob = d.vsBytecodeForReflection;
			size_t blobSize = d.vsBytecodeSize;
			if (!blob)
				if (ShaderRec* v = myShaders.Get(d.vs)) { blob = v->bytecode.data(); blobSize = v->bytecode.size(); }
			if (blob && blobSize)
			{
				std::vector<D3D11_INPUT_ELEMENT_DESC> el;
				BuildInputElements(d.inputLayout, d.inputLayoutCount, el);
				HRESULT hr = myDevice->CreateInputLayout(el.data(), (UINT)el.size(), blob, blobSize, rec.layout.GetAddressOf());
				assert(SUCCEEDED(hr)); (void)hr;
			}
		}

		GraphicsPipelineHandle h = myGfxPipelines.Alloc(std::move(rec));
		myGfxCache.emplace(key, h);
		return h;
	}

	ComputePipelineHandle Dx11Device::CreateComputePipeline(const ComputePipelineDesc& d)
	{
		const uint64_t key = ((uint64_t)d.cs.index << 32) | d.cs.generation;
		if (auto it = myComputeCache.find(key); it != myComputeCache.end()) return it->second;
		ComputePipelineRec rec;
		if (ShaderRec* c = myShaders.Get(d.cs)) rec.cs = c->cs.Get();
		ComputePipelineHandle h = myComputePipelines.Alloc(std::move(rec));
		myComputeCache.emplace(key, h);
		return h;
	}

	// ------------------------------------------------------------------ destroy
	void Dx11Device::Destroy(BufferHandle h)        { myBuffers.Free(h); }
	void Dx11Device::Destroy(TextureHandle h)       { myTextures.Free(h); }
	void Dx11Device::Destroy(SrvHandle h)           { mySrvs.Free(h); }
	void Dx11Device::Destroy(UavHandle h)           { myUavs.Free(h); }
	void Dx11Device::Destroy(RtvHandle h)           { myRtvs.Free(h); }
	void Dx11Device::Destroy(DsvHandle h)           { myDsvs.Free(h); }
	void Dx11Device::Destroy(SamplerHandle h)       { mySamplers.Free(h); }
	void Dx11Device::Destroy(ShaderModuleHandle h)  { myShaders.Free(h); }

	// ------------------------------------------------------------------ dynamic constants
	DynamicAlloc Dx11Device::AllocateDynamicConstants(const void* data, uint32_t byteSize)
	{
		// 256-byte alignment => offset is a multiple of 16 constants, satisfying
		// *SetConstantBuffers1's pFirstConstant/pNumConstants constraints.
		const uint32_t sz = Align(byteSize ? byteSize : 16, 256);
		assert(sz <= kDynBufferBytes);

		if (myDynByteCursor + sz > kDynBufferBytes)
		{
			++myDynBufferIndex;
			myDynByteCursor = 0;
		}
		while (myDynBufferIndex >= myDynRing.size())
		{
			BufferDesc bd;
			bd.byteSize = kDynBufferBytes;
			bd.usage = BufferUsage::Constant;
			bd.memory = MemoryType::Upload;
			bd.debugName = "rhi.dynConstRing";
			myDynRing.push_back(CreateBuffer(bd, nullptr));
		}

		BufferHandle bh = myDynRing[myDynBufferIndex];
		BufferRec* b = myBuffers.Get(bh);
		const uint32_t offset = myDynByteCursor;
		if (b && b->res && data)
		{
			// DISCARD the first time we touch a ring buffer this frame (recycles
			// last frame's contents); NO_OVERWRITE afterwards so the driver never
			// stalls or renames.
			const D3D11_MAP mode = (offset == 0) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
			D3D11_MAPPED_SUBRESOURCE m = {};
			if (SUCCEEDED(myCtx->Map(b->res.Get(), 0, mode, 0, &m)))
			{
				memcpy((char*)m.pData + offset, data, byteSize);
				myCtx->Unmap(b->res.Get(), 0);
			}
		}
		myDynByteCursor += sz;

		DynamicAlloc a;
		a.buffer = bh;
		a.offset = offset;
		a.size = byteSize;
		return a;
	}

	// ------------------------------------------------------------------ frame
	ICommandContext& Dx11Device::BeginFrame()
	{
		myDynBufferIndex = 0;
		myDynByteCursor = 0;

		++myFrameCounter;
		myCurTsSlot = (uint32_t)(myFrameCounter % kTsFrames);
		if (!myDisjoint[myCurTsSlot])
		{
			D3D11_QUERY_DESC d{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			myDevice->CreateQuery(&d, myDisjoint[myCurTsSlot].GetAddressOf());
		}
		if (myDisjoint[myCurTsSlot])
		{
			myCtx->Begin(myDisjoint[myCurTsSlot].Get());
			myTsSlotOpen = true;
		}
		return *myContext;
	}

	ICommandContext& Dx11Device::GetContext()
	{
		return *myContext;
	}

	void Dx11Device::EndFrame(bool vsync)
	{
		// Stage 1: Tga::DX11::EndFrame still owns Present. When DX11.cpp becomes a
		// facade over this device (step 1) this presents instead.
		(void)vsync;

		if (myTsSlotOpen)
		{
			myCtx->End(myDisjoint[myCurTsSlot].Get());
			myTsSlotOpen = false;
		}
	}

	// ------------------------------------------------------------------ timestamps
	TimestampQueryHandle Dx11Device::CreateTimestampQuery()
	{
		TimestampRec r;
		D3D11_QUERY_DESC d{ D3D11_QUERY_TIMESTAMP, 0 };
		if (FAILED(myDevice->CreateQuery(&d, r.begin.GetAddressOf())) ||
		    FAILED(myDevice->CreateQuery(&d, r.end.GetAddressOf())))
			return {};
		return myTimestamps.Alloc(std::move(r));
	}

	void Dx11Device::DestroyTimestampQuery(TimestampQueryHandle h) { myTimestamps.Free(h); }

	bool Dx11Device::GetTimestampMs(TimestampQueryHandle h, double& outMs)
	{
		TimestampRec* r = myTimestamps.Get(h);
		if (!r || r->frame == ~0ull)
			return false;

		const uint32_t slot = (uint32_t)(r->frame % kTsFrames);
		ID3D11Query* dj = myDisjoint[slot].Get();
		if (!dj)
			return false;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT djd{};
		if (myCtx->GetData(dj, &djd, sizeof(djd), 0) != S_OK)
			return false;   // not ready — caller retries next frame
		if (djd.Disjoint || djd.Frequency == 0)
		{
			r->frame = ~0ull;   // clocks moved this frame; drop it
			return false;
		}

		uint64_t t0 = 0, t1 = 0;
		if (myCtx->GetData(r->begin.Get(), &t0, sizeof(t0), 0) != S_OK) return false;
		if (myCtx->GetData(r->end.Get(),   &t1, sizeof(t1), 0) != S_OK) return false;
		// idempotent: leave r->frame set so repeated reads in the same buffered
		// window return the same value; the next WriteTimestampBegin re-stamps it.
		if (t1 < t0)
			return false;

		outMs = double(t1 - t0) / double(djd.Frequency) * 1000.0;
		return true;
	}

	// ------------------------------------------------------------------ escape hatches
	void* Dx11Device::GetNativeDevice()  { return myDevice; }
	void* Dx11Device::GetNativeContext() { return myCtx; }
	void* Dx11Device::GetNativeSrv(SrvHandle h) { return GetSrvPtr(h); }
	void* Dx11Device::GetNativeRtv(RtvHandle h) { return GetRtvPtr(h); }
	void* Dx11Device::GetNativeTexture(TextureHandle h) { auto* t = myTextures.Get(h); return t ? t->res.Get() : nullptr; }
	void* Dx11Device::ImGuiTextureId(SrvHandle h) { return GetSrvPtr(h); }

	SrvHandle Dx11Device::WrapNativeSrv(void* p)
	{
		if (!p) return {};
		return mySrvs.Alloc(ComPtr<ID3D11ShaderResourceView>((ID3D11ShaderResourceView*)p));
	}
	RtvHandle Dx11Device::WrapNativeRtv(void* p)
	{
		if (!p) return {};
		return myRtvs.Alloc(ComPtr<ID3D11RenderTargetView>((ID3D11RenderTargetView*)p));
	}
	DsvHandle Dx11Device::WrapNativeDsv(void* p)
	{
		if (!p) return {};
		return myDsvs.Alloc(ComPtr<ID3D11DepthStencilView>((ID3D11DepthStencilView*)p));
	}

	void Dx11Device::BuildInputElements(const InputElement* elems, uint32_t count,
	                                    std::vector<D3D11_INPUT_ELEMENT_DESC>& out)
	{
		out.resize(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			const InputElement& s = elems[i];
			D3D11_INPUT_ELEMENT_DESC& e = out[i];
			e.SemanticName = s.semanticName;
			e.SemanticIndex = s.semanticIndex;
			e.Format = ToDxgi(s.format);
			e.InputSlot = s.inputSlot;
			e.AlignedByteOffset = (s.alignedByteOffset == ~0u) ? D3D11_APPEND_ALIGNED_ELEMENT : s.alignedByteOffset;
			e.InputSlotClass = s.perInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
			e.InstanceDataStepRate = s.instanceStepRate;
		}
	}

	void* Dx11Device::CreateInputLayoutNative(const InputElement* elems, uint32_t count,
	                                          const void* vsBytecode, uint32_t vsSize)
	{
		if (!elems || !count || !vsBytecode || !vsSize) return nullptr;

		uint64_t key = 1469598103934665603ull;
		HashCombine(key, count);
		for (uint32_t i = 0; i < count; ++i)
		{
			const InputElement& e = elems[i];
			for (const char* p = e.semanticName; p && *p; ++p) HashCombine(key, (uint8_t)*p);
			HashCombine(key, e.semanticIndex);
			HashCombine(key, (uint64_t)e.format);
			HashCombine(key, e.inputSlot);
			HashCombine(key, e.alignedByteOffset);
			HashCombine(key, (uint64_t)e.perInstance | ((uint64_t)e.instanceStepRate << 1));
		}
		// The VS bytecode disambiguates layouts that share element lists but were
		// reflected against different signatures.
		HashCombine(key, vsSize);
		HashCombine(key, (uint64_t)(reinterpret_cast<const uint8_t*>(vsBytecode)[0]) |
		                 ((uint64_t)(reinterpret_cast<const uint8_t*>(vsBytecode)[vsSize / 2]) << 8) |
		                 ((uint64_t)(reinterpret_cast<const uint8_t*>(vsBytecode)[vsSize - 1]) << 16));

		if (auto it = myInputLayoutCache.find(key); it != myInputLayoutCache.end())
		{
			ID3D11InputLayout* p = it->second.Get();
			if (p) p->AddRef();
			return p;
		}

		std::vector<D3D11_INPUT_ELEMENT_DESC> el;
		BuildInputElements(elems, count, el);

		ComPtr<ID3D11InputLayout> layout;
		HRESULT hr = myDevice->CreateInputLayout(el.data(), (UINT)el.size(), vsBytecode, vsSize, layout.GetAddressOf());
		if (FAILED(hr) || !layout) return nullptr;

		myInputLayoutCache.emplace(key, layout);
		layout->AddRef();
		return layout.Get();
	}
}
