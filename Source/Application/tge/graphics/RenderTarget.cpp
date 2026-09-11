#include "stdafx.h"
#include "RenderTarget.h"
#include <tge/Graphics/DepthBuffer.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>

using namespace Tga;

rhi::RtvHandle RenderTarget::GetRtv() const
{
	if (!myRhiRtv && myRenderTarget)
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

	// The RHI's own handles are freed immediately below -- the ComPtrs just
	// populated hold their own ref (GetNative*'s pointer is AddRef'd on
	// assignment), so nothing depends on the pool entry staying alive. This
	// sidesteps RenderTarget's copy semantics entirely: it's a value type
	// (returned by value, reassigned wholesale on resize), so an owned
	// rhi::TextureHandle with no ref-counting would need its own shared-
	// ownership wrapper to copy safely -- not worth it when the existing
	// ComPtr-based storage already does the job.
	RenderTarget textureResult;
	textureResult.myRenderTarget = static_cast<ID3D11RenderTargetView*>(dev->GetNativeRtv(rtvHandle));
	textureResult.mySRV = static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(srvHandle));
	textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
		0, 0, static_cast<float>(aSize.X), static_cast<float>(aSize.Y), 0, 1 });

	dev->Destroy(rtvHandle);
	dev->Destroy(srvHandle);
	dev->Destroy(texHandle);
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
	textureResult.myRenderTarget = static_cast<ID3D11RenderTargetView*>(dev->GetNativeRtv(rtvHandle));
	textureResult.mySRV = static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(srvHandle));
	textureResult.myViewport = std::make_shared<const D3D11_VIEWPORT>(D3D11_VIEWPORT{
		0, 0, static_cast<float>(aSize.X), static_cast<float>(aSize.Y), 0, 1 });

	dev->Destroy(rtvHandle);
	dev->Destroy(srvHandle);
	dev->Destroy(texHandle);
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
	DX11::Context->ClearRenderTargetView(myRenderTarget.Get(), &aClearColor.X);
}

void RenderTarget::SetAsActiveTarget(DepthBuffer* aDepth)
{
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
