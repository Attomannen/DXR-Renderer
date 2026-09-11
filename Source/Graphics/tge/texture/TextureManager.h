/*
This class handles all the textures created, please use this when you create a texture
*/

#pragma once
#include "Texture.h"
#include <dxgiformat.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <vector>
#include <unordered_map>
#include <tge/loaders/tgaloader.h>



struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
namespace Tga
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

		/* Requires DX11 includes */
		ID3D11ShaderResourceView* GetDefaultNormalMapResource() const { return myDefaultNormalMapResource.Get(); }
	private:
		DXGI_FORMAT GetTextureFormat(struct ID3D11ShaderResourceView* aResourceView) const;

		std::unordered_map<StringId, std::unique_ptr<Texture>> myResourceViews;
		void CreateErrorSquareTexture();
		ComPtr<ID3D11ShaderResourceView> CreateWhiteSquareTexture();
		void CreateDefaultNormalmapTexture();
		void OnTextureChanged(StringId aFile);
		
		ComPtr<ID3D11ShaderResourceView> myFailedResource;
		ComPtr<ID3D11ShaderResourceView> myDefaultNormalMapResource;
		std::unique_ptr<Texture> myWhiteSquareTexture;
	};
}