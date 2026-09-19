#include "stdafx.h"
#include <age/texture/Texture.h>
#include <ScreenGrab/ScreenGrab11.h>
#include <age/texture/TextureManager.h>

Ag::Texture::Texture()
{
	myPath = "undefined";
	myIsFailedTexture = false;
}

Ag::Texture::~Texture()
{
	if (myIsFailedTexture)
	{
		return;
	}
	myPath = "undefined";
	myID = 0;
}