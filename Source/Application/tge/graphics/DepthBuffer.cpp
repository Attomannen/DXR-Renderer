#include "stdafx.h"
#include "DepthBuffer.h"

#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>

using namespace Tga;

rhi::DsvHandle DepthBuffer::GetDsv() const
{
	if (!myRhiDsv && myDepth)
		if (rhi::IDevice* r = DX11::Rhi())
			myRhiDsv.handle = r->WrapNativeDsv(myDepth.Get());
	return myRhiDsv.handle;
}

void DepthBuffer::SetAsActiveTarget()
{
	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		rhi::ICommandContext& ctx = dev->GetContext();
		rhi::DsvHandle dsv = GetDsv();
		ctx.SetRenderTargets(0, nullptr, dsv);
		ctx.SetViewport(myViewport.TopLeftX, myViewport.TopLeftY, myViewport.Width, myViewport.Height, myViewport.MinDepth, myViewport.MaxDepth);
		return;
	}

	DX11::Context->OMSetRenderTargets(0, nullptr, GetDepthStencilView());
	DX11::Context->RSSetViewports(1, &myViewport);
}

void DepthBuffer::Clear(float aClearDepthValue /* = 1.0f */, uint8_t aClearStencilValue /* = 0 */)
{
	rhi::IDevice* dev = DX11::Rhi();
	if (dev && dev->GetBackend() == rhi::Backend::DX12)
	{
		// DepthBuffer::Create() always uses D32_Float internally, which has
		// NO stencil plane at all -- D3D11's ClearDepthStencilView tolerates
		// a stencil-clear flag on a stencil-less resource silently, but
		// D3D12 does not (clearing a nonexistent plane touches memory the
		// resource doesn't have). Depth-only clear here, regardless of the
		// clearStencil-shaped DX11 call below.
		dev->GetContext().ClearDepthStencil(GetDsv(), aClearDepthValue, aClearStencilValue, /*clearDepth=*/true, /*clearStencil=*/false);
		return;
	}
	DX11::Context->ClearDepthStencilView(myDepth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, aClearDepthValue, aClearStencilValue);
}

DepthBuffer DepthBuffer::Create(Vector2ui aSize)
{
	rhi::IDevice* rhiDev = DX11::Rhi();
	if (rhiDev && rhiDev->GetBackend() == rhi::Backend::DX12)
	{
		rhi::TextureDesc desc = {};
		desc.width = aSize.X;
		desc.height = aSize.Y;
		// The *logical* depth format goes here, not a typeless one -- CreateTexture
		// checks IsDepth(desc.format) itself to decide the actual (typeless)
		// resource format AND the DSV clear-value format; passing R32_Typeless
		// directly skips that path and leaves the clear value format unset,
		// which D3D12 rejects (CreateCommittedResource -> E_INVALIDARG) for a
		// resource with the ALLOW_DEPTH_STENCIL flag.
		desc.format = rhi::Format::D32_Float;
		desc.bind = rhi::TextureBind::DepthStencil | rhi::TextureBind::ShaderResource;
		desc.debugName = "DepthBuffer";

		rhi::TextureHandle texHandle = rhiDev->CreateTexture(desc);
		assert(texHandle.IsValid());

		// Default descs: CreateDsv/CreateSrv both derive the correct DSV/linear-SRV
		// formats automatically from the depth texture's own format when no
		// override is given (see Dx12Device::CreateSrv/CreateDsv).
		rhi::DsvHandle dsvHandle = rhiDev->CreateDsv(texHandle, {});
		rhi::SrvHandle srvHandle = rhiDev->CreateSrv(texHandle, {});

		DepthBuffer textureResult;
		textureResult.myRhiTexture.handle = texHandle;
		textureResult.myRhiDsv.handle = dsvHandle;
		textureResult.myRhiSrv.handle = srvHandle;
		textureResult.myViewport = {
				0,
				0,
				static_cast<float>(aSize.X),
				static_cast<float>(aSize.Y),
				0,
				1
		};
		return textureResult;
	}

	HRESULT result;
	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width = aSize.X;
	desc.Height = aSize.Y;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	ID3D11Texture2D* texture;
	result = DX11::Device->CreateTexture2D(&desc, nullptr, &texture);
	assert(SUCCEEDED(result));

	ID3D11DepthStencilView* DSV;
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

	dsvDesc.Flags = 0;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	result = DX11::Device->CreateDepthStencilView(texture, &dsvDesc, &DSV);
	assert(SUCCEEDED(result));

	DepthBuffer textureResult;
	textureResult.myDepth = DSV;
	DSV->Release();

	ID3D11ShaderResourceView* SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc{};
	srDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = std::numeric_limits<UINT>::max();
	result = DX11::Device->CreateShaderResourceView(texture, &srDesc, &SRV);
	assert(SUCCEEDED(result));
	textureResult.mySRV = SRV;
	SRV->Release();

	textureResult.myViewport = {
			0,
			0,
			static_cast<float>(aSize.X),
			static_cast<float>(aSize.Y),
			0,
			1
	};

	texture->Release();
	return textureResult;
}
