#pragma once

#include <tge/Math/Vector.h>
#include <tge/graphics/TextureResource.h>
#include <wrl/client.h>
#include <dxgiformat.h>
#include <memory>

using Microsoft::WRL::ComPtr;

struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct D3D11_VIEWPORT;
struct ID3D11Buffer;

namespace Tga
{

class DepthBuffer;

class RenderTarget : public TextureResource
{
	ComPtr<ID3D11RenderTargetView> myRenderTarget;
	std::shared_ptr<const D3D11_VIEWPORT> myViewport;
	mutable MigrationView<rhi::RtvHandle> myRhiRtv;   // lazily wraps myRenderTarget (bridge)

public:
	RenderTarget();
	~RenderTarget();

	static RenderTarget Create(Vector2ui aSize, DXGI_FORMAT aFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
	static RenderTarget Create(Vector2ui aSize, DXGI_FORMAT aFormat, DXGI_FORMAT aRenderTargetFormat, DXGI_FORMAT aShaderResourceFormat);

	static RenderTarget Create(ID3D11Texture2D* aTexture);
	static RenderTarget Create(ID3D11Texture2D* aTexture, DXGI_FORMAT aFormat);

	void Clear(Vector4f aClearColor = { 0,0,0,0 });
	void SetAsActiveTarget(DepthBuffer* aDepth = nullptr);

	// For multi-render-target (deferred) setups that bind several RTs at once.
	ID3D11RenderTargetView* GetRenderTargetView() const { return myRenderTarget.Get(); }
	// rhi handle onto the same RTV (created on first use). Preferred by migrated code.
	rhi::RtvHandle GetRtv() const;

	Vector2ui GetResolution() const;
};

} // namespace Tga