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
	mutable MigrationView<rhi::RtvHandle> myRhiRtv;       // lazily wraps myRenderTarget on DX11; owns the real handle directly on DX12 (bridge)
	// myRhiTexture (DX12 only: keeps the owning texture alive) is inherited from TextureResource.
	// True only for the DX12 swapchain's backbuffer wrapper (DX11::BackBuffer
	// under -rhi=dx12): DX12's flip-model swapchain has a DIFFERENT resource
	// per frame index, unlike DX11's single stable backbuffer, so this mode
	// makes GetRtv() re-resolve the device's *current* backbuffer view every
	// call instead of caching one at construction time. myRenderTarget/
	// myRhiRtv/myRhiTexture stay unused in this mode -- ownership belongs to
	// the Dx12Device, not this wrapper.
	bool myIsDx12BackBuffer = false;
	bool mySrgbBackBuffer = false;   // which of DX11::BackBuffer / BackBufferNoSrgbConversion this is, when myIsDx12BackBuffer

public:
	RenderTarget();
	~RenderTarget();

	static RenderTarget Create(Vector2ui aSize, rhi::Format aFormat = rhi::Format::R8G8B8A8_UNorm);
	static RenderTarget Create(Vector2ui aSize, rhi::Format aFormat, rhi::Format aRenderTargetFormat, rhi::Format aShaderResourceFormat);

	// Adopts an externally-created texture (the swapchain backbuffer) as a
	// RenderTarget. Stays on the raw D3D11 path deliberately: both call sites
	// (DX11::Init/ResizeToWindowSize) run *before* the RHI device exists, so
	// there is nothing to route through yet. DX11 only -- see
	// CreateFromDeviceBackBuffer for the DX12 equivalent.
	static RenderTarget Create(ID3D11Texture2D* aTexture);
	static RenderTarget Create(ID3D11Texture2D* aTexture, DXGI_FORMAT aFormat);

	// Wraps the RHI device's OWN swapchain backbuffer (DX12 only -- the DX11
	// bootstrap path still uses the two raw overloads above, since it runs
	// before the RHI device exists). aResolution is needed explicitly because
	// DX12's backbuffer view is re-resolved dynamically per call (see
	// myIsDx12BackBuffer above) rather than queried from a stored texture desc.
	static RenderTarget CreateFromDeviceBackBuffer(bool aSrgb, Vector2ui aResolution);

	void Clear(Vector4f aClearColor = { 0,0,0,0 });
	void SetAsActiveTarget(DepthBuffer* aDepth = nullptr);

	// For multi-render-target (deferred) setups that bind several RTs at once.
	// DX11 only -- returns null under the DX12 backend (no raw D3D11 view
	// exists to return); migrated code should use GetRtv() instead.
	ID3D11RenderTargetView* GetRenderTargetView() const { return myRenderTarget.Get(); }
	// rhi handle onto the same RTV. Preferred by migrated code -- works on
	// both backends.
	rhi::RtvHandle GetRtv() const;

	Vector2ui GetResolution() const;
};

} // namespace Tga