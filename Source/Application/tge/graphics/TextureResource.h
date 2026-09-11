#pragma once

#include <tge/Math/Vector.h>
#include <tge/rhi/MigrationView.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D11ShaderResourceView;

namespace Tga
{

class TextureResource
{
protected:
	ComPtr<ID3D11ShaderResourceView> mySRV;
	mutable MigrationView<rhi::SrvHandle> myRhiSrv;   // lazily wraps mySRV on DX11; owns the real handle directly on DX12 (bridge)
	// DX12 only: keeps the owning texture alive (a D3D12 descriptor holds no
	// reference of its own, unlike a D3D11 view) -- shared here rather than
	// re-declared per derived class since RenderTarget/DepthBuffer/Texture
	// all need it identically.
	mutable MigrationView<rhi::TextureHandle> myRhiTexture;

public:
	TextureResource();
	TextureResource(ID3D11ShaderResourceView* aSRV);
	~TextureResource();

	void SetAsResourceOnSlot(unsigned int aSlot) const;
	ID3D11ShaderResourceView* GetShaderResourceView() const { return mySRV.Get(); };
	void SetShaderResourceView(ID3D11ShaderResourceView* aSRV);
	// DX12 (or any future non-COM backend) equivalent of SetShaderResourceView:
	// takes ownership of RHI-native handles directly instead of a raw D3D11
	// pointer, since a DX12 view can't be extracted as one. Mirrors
	// RenderTarget/DepthBuffer's identical storage-migration pattern.
	void SetRhiTexture(rhi::TextureHandle aTexture, rhi::SrvHandle aSrv);
	// rhi handle onto the same view (created on first use). Preferred by migrated code.
	rhi::SrvHandle GetSrv() const;
	Vector2ui CalculateTextureSize() const;
};

} // namespace Tga
