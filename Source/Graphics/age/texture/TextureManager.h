/*
This class handles all the textures created, please use this when you create a texture
*/

#pragma once
#include "Texture.h"
#include <dxgiformat.h>
#include <age/stringRegistry/StringRegistry.h>
#include <vector>
#include <unordered_map>
#include <age/loaders/tgaloader.h>



struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
namespace Ag
{
	class GraphicsEngine;

	class TextureManager
	{
	public:
		TextureManager(void);
		~TextureManager(void);
		void Init();
		Texture* GetTexture(const char* aTexturePath, TextureSrgbMode aSrgbMode = TextureSrgbMode::ForceSrgbFormat, bool aForceReload = false);
		Texture* TryGetTexture(const char* aTexturePath, TextureSrgbMode aSrgbMode = TextureSrgbMode::ForceSrgbFormat, bool aForceReload = false);

		Texture* GetWhiteSquareTexture() { return myWhiteSquareTexture.get(); }
		static Vector2f GetTextureSize(struct ID3D11ShaderResourceView* aResourceView, bool aNormalize = true);

		Texture * CreateTextureFromTarga(Tga32::Image * aImage);

		void ReleaseTexture(Texture* aTexture);

		void Update();

		/* Requires DX11 includes. DX11-only -- returns null under DX12, matching
		   GetRenderTargetView()'s existing "no raw D3D11 view exists" convention
		   for this port (unused externally today, checked via grep). */
		ID3D11ShaderResourceView* GetDefaultNormalMapResource() const { return myDefaultNormalMapResource ? myDefaultNormalMapResource->GetShaderResourceView() : nullptr; }
	private:
		DXGI_FORMAT GetTextureFormat(struct ID3D11ShaderResourceView* aResourceView) const;
		// DX12 counterpart of TryGetTexture's DDS/WIC/TGA loading block below
		// -- see the .cpp for why this needs to be a wholly separate path
		// rather than an inline branch (DirectXTex's *11-suffixed loaders
		// this file otherwise uses take an ID3D11Device* directly; no DX12
		// equivalent is vendored). Returns null on any load failure.
		Texture* LoadTextureDx12(rhi::IDevice& aDevice, const char* aResolvedPathUtf8, const std::wstring& aResolvedPathW,
		                          const char* aUnresolvedPath, TextureSrgbMode aSrgbMode, Texture* aExistingTexture);

		std::unordered_map<StringId, std::unique_ptr<Texture>> myResourceViews;

	public:
		// Reads and decodes these DDS files (engine asset paths) on worker
		// threads, so the GetTexture calls that follow only create GPU
		// resources. Anything not requested afterwards is dropped by
		// ClearPrefetchedTextures().
		void PrefetchTextures(const std::vector<std::string>& aTexturePaths);
		void ClearPrefetchedTextures();

	private:
		struct PrefetchedImage;
		std::unordered_map<std::wstring, std::unique_ptr<PrefetchedImage>> myPrefetched;
		void CreateErrorSquareTexture();
		void CreateWhiteSquareTexture();
		void CreateDefaultNormalmapTexture();
		void OnTextureChanged(StringId aFile);

		// Procedural fallback textures. Full Texture objects (not a raw
		// ComPtr<ID3D11ShaderResourceView>, matching myWhiteSquareTexture's
		// existing shape) so they can hold either backend's storage via
		// CreateSolidTexture -- see the .cpp.
		std::unique_ptr<Texture> myFailedResource;
		std::unique_ptr<Texture> myDefaultNormalMapResource;
		std::unique_ptr<Texture> myWhiteSquareTexture;
	};
}