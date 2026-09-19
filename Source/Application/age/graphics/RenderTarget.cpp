#include "stdafx.h"
#include "RenderTarget.h"
#include <age/Graphics/DepthBuffer.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>

using namespace Ag;

rhi::RtvHandle RenderTarget::GetRtv() const
{
	if (myIsDx12BackBuffer)
	{
		// DX12's flip-model swapchain has a different resource per frame
		// index -- re-resolve the device's *current* backbuffer view every
		// call rather than caching one (see the header's class comment).
		rhi::IDevice* r = DX11::Rhi();
		return r ? r->GetBackBufferRtv(mySrgbBackBuffer) : rhi::RtvHandle{};
	}
	if (myRhiRtv) return myRhiRtv.handle;   // already owns a real DX12 handle directly, or a cached DX11 wrap
	if (myRenderTarget)
		if (rhi::IDevice* r = DX11::Rhi())
			myRhiRtv.handle = r->WrapNativeRtv(myRenderTarget.Get());
	return myRhiRtv.handle;
}

RenderTarget::RenderTarget() : myRenderTarget(nullptr), myViewport(nullptr) {}

RenderTarget::~RenderTarget() {}

Vector2ui RenderTarget::GetResolution() const
{
	return { (unsigned int)myViewport->Width, (unsigned int)myViewport->Height };
}

RenderTarget RenderTarget::Create(Vector2ui aSize, rhi::Format aFormat)
{
	rhi::IDevice* dev = DX11::Rhi();
	assert(dev && "RenderTarget::Create(size,format) needs the RHI device -- "
	              "use Create(ID3D11Texture2D*) during DX11::Init bootstrap");

	rhi::TextureDesc desc = {};
	desc.width = aSize.X;
	desc.height = aSize.Y;
	desc.format = aFormat;
	desc.bind = rhi::TextureBind::RenderTarget | rhi::TextureBind::ShaderResource;
	desc.debugName = "RenderTarget";

	rhi::TextureHandle texHandle = dev->CreateTexture(desc);
	assert(texHandle.IsValid());
	rhi::RtvHandle rtvHandle = dev->CreateRtv(texHandle, {});
	rhi::SrvHandle srvHandle = dev->CreateSrv(texHandle, {});

	RenderTarget textureResult;
	textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
		0, 0, static_cast<float>(aSize.X), static_cast<float>(aSize.Y), 0, 1 });

	if (dev->GetBackend() == rhi::Backend::DX12)
	{
		// No raw view pointer exists to extract under DX12 -- the handles
		// ARE the resource. Store them directly so MigrationView's destructor
		// frees them when this RenderTarget is destroyed/reassigned, and its
		// "copy does not propagate" semantics (each copy gets an independent
		// empty handle) keep RenderTarget safely copyable -- the same
		// property the DX11 lazy-wrap path already relies on for myRhiRtv/
		// myRhiSrv, just fed directly instead of via a wrapped raw pointer.
		// myRhiTexture additionally keeps the owning texture alive: unlike a
		// D3D11 view, a D3D12 descriptor holds no reference of its own.
		textureResult.myRhiTexture.handle = texHandle;
		textureResult.myRhiRtv.handle = rtvHandle;
		textureResult.myRhiSrv.handle = srvHandle;
	}
	else
	{
		// The RHI's own handles are freed immediately below -- the ComPtrs
		// just populated hold their own ref (GetNative*'s pointer is AddRef'd
		// on assignment), so nothing depends on the pool entry staying alive.
		textureResult.myRenderTarget = static_cast<ID3D11RenderTargetView*>(dev->GetNativeRtv(rtvHandle));
		textureResult.mySRV = static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(srvHandle));
		dev->Destroy(rtvHandle);
		dev->Destroy(srvHandle);
		dev->Destroy(texHandle);
	}
	return textureResult;
}

RenderTarget RenderTarget::Create(Vector2ui aSize, rhi::Format aFormat, rhi::Format aRenderTargetFormat, rhi::Format aShaderResourceFormat)
{
	rhi::IDevice* dev = DX11::Rhi();
	assert(dev && "RenderTarget::Create(size,format,format,format) needs the RHI device -- "
	              "use Create(ID3D11Texture2D*) during DX11::Init bootstrap");

	rhi::TextureDesc desc = {};
	desc.width = aSize.X;
	desc.height = aSize.Y;
	desc.format = aFormat;   // typeless-friendly resource format
	desc.bind = rhi::TextureBind::RenderTarget | rhi::TextureBind::ShaderResource;
	desc.debugName = "RenderTarget";

	rhi::TextureHandle texHandle = dev->CreateTexture(desc);
	assert(texHandle.IsValid());

	rhi::RtvDesc rtvDesc = {};
	rtvDesc.formatOverride = aRenderTargetFormat;
	rhi::SrvDesc srvDesc = {};
	srvDesc.formatOverride = aShaderResourceFormat;

	rhi::RtvHandle rtvHandle = dev->CreateRtv(texHandle, rtvDesc);
	rhi::SrvHandle srvHandle = dev->CreateSrv(texHandle, srvDesc);

	RenderTarget textureResult;
	textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
		0, 0, static_cast<float>(aSize.X), static_cast<float>(aSize.Y), 0, 1 });

	if (dev->GetBackend() == rhi::Backend::DX12)
	{
		textureResult.myRhiTexture.handle = texHandle;
		textureResult.myRhiRtv.handle = rtvHandle;
		textureResult.myRhiSrv.handle = srvHandle;
	}
	else
	{
		textureResult.myRenderTarget = static_cast<ID3D11RenderTargetView*>(dev->GetNativeRtv(rtvHandle));
		textureResult.mySRV = static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(srvHandle));
		dev->Destroy(rtvHandle);
		dev->Destroy(srvHandle);
		dev->Destroy(texHandle);
	}
	return textureResult;
}

RenderTarget RenderTarget::CreateFromDeviceBackBuffer(bool aSrgb, Vector2ui aResolution)
{
	RenderTarget textureResult;
	textureResult.myIsDx12BackBuffer = true;
	textureResult.mySrgbBackBuffer = aSrgb;
	textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
		0, 0, static_cast<float>(aResolution.X), static_cast<float>(aResolution.Y), 0, 1 });
	return textureResult;
}

RenderTarget RenderTarget::Create(ID3D11Texture2D* aTexture)
{
	HRESULT result;

	ID3D11RenderTargetView* RTV;
	result = DX11::Device->CreateRenderTargetView(
		aTexture,
		nullptr,
		&RTV);
	assert(SUCCEEDED(result));

	RenderTarget textureResult;

	if (aTexture)
	{
		D3D11_TEXTURE2D_DESC desc;
		aTexture->GetDesc(&desc);
		textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
				0,
				0,
				static_cast<float>(desc.Width),
				static_cast<float>(desc.Height),
				0,
				1
			});
	}
	
	textureResult.myRenderTarget = RTV;
	RTV->Release();
	return textureResult;
}

RenderTarget RenderTarget::Create(ID3D11Texture2D* aTexture, DXGI_FORMAT aFormat)
{
	HRESULT result;

	ID3D11RenderTargetView* RTV;

	D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
	viewDesc.Format = aFormat;
	viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D = {};

	result = DX11::Device->CreateRenderTargetView(
		aTexture,
		&viewDesc,
		&RTV);
	assert(SUCCEEDED(result));

	RenderTarget textureResult;

	if (aTexture)
	{
		D3D11_TEXTURE2D_DESC desc;
		aTexture->GetDesc(&desc);
		textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
				0,
				0,
				static_cast<float>(desc.Width),
				static_cast<float>(desc.Height),
				0,
				1
			});
	}

	textureResult.myRenderTarget = RTV;
	RTV->Release();
	return textureResult;
}

void RenderTarget::Clear(Vector4f aClearColor)
{
	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		dev->GetContext().ClearRenderTarget(GetRtv(), &aClearColor.X);
		return;
	}
	DX11::Context->ClearRenderTargetView(myRenderTarget.Get(), &aClearColor.X);
}

void RenderTarget::SetAsActiveTarget(DepthBuffer* aDepth)
{
	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		rhi::ICommandContext& ctx = dev->GetContext();
		rhi::RtvHandle rtv = GetRtv();
		rhi::DsvHandle dsv = aDepth ? aDepth->GetDsv() : rhi::DsvHandle{};
		ctx.SetRenderTargets(1, &rtv, dsv);
		if (myViewport)
			ctx.SetViewport(myViewport->TopLeftX, myViewport->TopLeftY, myViewport->Width, myViewport->Height, myViewport->MinDepth, myViewport->MaxDepth);
		return;
	}

	if(aDepth)
	{
		DX11::Context->OMSetRenderTargets(1, myRenderTarget.GetAddressOf(), aDepth->GetDepthStencilView());
	}
	else
	{
		DX11::Context->OMSetRenderTargets(1, myRenderTarget.GetAddressOf(), nullptr);
	}

	DX11::Context->RSSetViewports(1, myViewport.get());
}
