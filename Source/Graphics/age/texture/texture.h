/*
This class will store a texture bound to DX11
*/
#pragma once

#include <age/graphics/TextureResource.h>

using Microsoft::WRL::ComPtr;
namespace Ag
{

	enum class TextureSrgbMode
	{
		None,
		ForceSrgbFormat,
		ForceNoSrgbFormat
	};

	class Texture : public TextureResource
	{
	public:
		Texture();
		~Texture();

	public:
		TextureSrgbMode mySrgbMode;
		std::string myPath;
		std::string myUnresolvedPath;
		uint64_t myID;
		Vector2f mySize;
		Vector2ui myImageSize;
		bool myIsFailedTexture;
		bool myIsReleased;
	};
}
