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
	mutable MigrationView<rhi::SrvHandle> myRhiSrv;   // lazily wraps mySRV (bridge)

public:
	TextureResource();
	TextureResource(ID3D11ShaderResourceView* aSRV);
	~TextureResource();

	void SetAsResourceOnSlot(unsigned int aSlot) const;
	ID3D11ShaderResourceView* GetShaderResourceView() const { return mySRV.Get(); };
	void SetShaderResourceView(ID3D11ShaderResourceView* aSRV);
	// rhi handle onto the same view (created on first use). Preferred by migrated code.
	rhi::SrvHandle GetSrv() const;
	Vector2ui CalculateTextureSize() const;
};

} // namespace Tga
