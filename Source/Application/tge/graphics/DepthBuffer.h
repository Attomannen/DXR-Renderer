#pragma once

#include <tge/Math/Vector.h>
#include <tge/Graphics/TextureResource.h>
#include <wrl/client.h>
#include <dxgiformat.h>
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

struct ID3D11DepthStencilView;
struct ID3D11Texture2D;
struct D3D11_VIEWPORT;

namespace Tga
{

class DepthBuffer : public TextureResource
{
	ComPtr<ID3D11DepthStencilView> myDepth = 0;
	D3D11_VIEWPORT myViewport = {};
	mutable MigrationView<rhi::DsvHandle> myRhiDsv;   // lazily wraps myDepth on DX11; owns the real handle directly on DX12 (bridge)
	// myRhiTexture (DX12 only: keeps the owning texture alive) is inherited from TextureResource.

public:

	static DepthBuffer Create(Vector2ui aSize);

	void Clear(float aClearDepthValue = 1.0f, uint8_t aClearStencilValue = 0);
	ID3D11DepthStencilView* GetDepthStencilView() { return myDepth.Get(); };
	// rhi handle onto the same DSV (created on first use). Preferred by migrated code.
	rhi::DsvHandle GetDsv() const;

	void SetAsActiveTarget();
};

} // namespace Tga