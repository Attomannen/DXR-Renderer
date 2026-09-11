#pragma once

#include <tge/Math/Vector.h>
#include <tge/graphics/TextureResource.h>
#include <tge/rhi/Descs.h>
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

	static RenderTarget Create(Vector2ui aSize, rhi::Format aFormat = rhi::Format::R8G8B8A8_UNorm);
	static RenderTarget Create(Vector2ui aSize, rhi::Format aFormat, rhi::Format aRenderTargetFormat, rhi::Format aShaderResourceFormat);

	// Adopts an externally-created texture (the swapchain backbuffer) as a
	// RenderTarget. Stays on the raw D3D11 path deliberately: both call sites
	// (DX11::Init/ResizeToWindowSize) run *before* the RHI device exists, so
	// there is nothing to route through yet.
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