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
	// A user-declared destructor suppresses the compiler-generated move
	// constructor/assignment (Rule of Five) -- without these explicit
	// defaults, `derived = Derived::Create(...)` (DepthBuffer/RenderTarget/
	// Texture's factory-by-value pattern) silently falls back to COPY
	// assignment instead of move. MigrationView's copy ctor/assignment
	// deliberately does NOT propagate ownership (each instance owns only
	// what it creates itself) -- under a copy, the temporary returned by
	// Create() keeps ownership and destroys the real DX12 resource/view the
	// moment it goes out of scope at the end of the assignment statement,
	// leaving the just-assigned object holding a dangling handle. Restoring
	// real move here (matching MigrationView.h's own design comment, "lets
	// RenderTarget/DepthBuffer/TextureResource stay copyable/movable with
	// the compiler-generated special members") transfers ownership instead.
	TextureResource(const TextureResource&) = default;
	TextureResource(TextureResource&&) noexcept = default;
	TextureResource& operator=(const TextureResource&) = default;
	TextureResource& operator=(TextureResource&&) noexcept = default;

	void SetAsResourceOnSlot(unsigned int aSlot) const;
	ID3D11ShaderResourceView* GetShaderResourceView() const { return mySRV.Get(); };
	void SetShaderResourceView(ID3D11ShaderResourceView* aSRV);
	// DX12 (or any future non-COM backend) equivalent of SetShaderResourceView:
	// takes ownership of RHI-native handles directly instead of a raw D3D11
	// pointer, since a DX12 view can't be extracted as one. Mirrors
	// RenderTarget/DepthBuffer's identical storage-migration pattern.
	// aTakesOwnership=false makes this instance a non-owning alias instead --
	// for a caller (e.g. TextService's font atlas) whose handles are already
	// owned/destroyed elsewhere with a longer lifetime; wrapping them as a
	// second owner here would double-destroy them.
	void SetRhiTexture(rhi::TextureHandle aTexture, rhi::SrvHandle aSrv, bool aTakesOwnership = true);
	// rhi handle onto the same view (created on first use). Preferred by migrated code.
	rhi::SrvHandle GetSrv() const;
	// The owning texture, DX12 only (empty/invalid on DX11 -- that path never
	// populates myRhiTexture, since a D3D11 view already carries its own
	// resource reference). For code that needs the texture itself, not just
	// a view onto it (e.g. CubemapPrefilter's per-face CopyTextureRegion).
	rhi::TextureHandle GetTextureHandle() const { return myRhiTexture.handle; }
	Vector2ui CalculateTextureSize() const;
};

} // namespace Tga
