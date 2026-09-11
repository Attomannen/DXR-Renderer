#include "stdafx.h"
#include <tge/videoplayer/video.h>
#include <tge/videoplayer/videoplayer.h>

#ifdef USE_VIDEO
#include <tge/sprite/sprite.h>
#include <tge/graphics/DX11.h>
#include <tge/texture/TextureManager.h>
#include <tge/log/Log.h>
#include <tge/application.h>
using namespace Tga;
Tga::Video::Video() : myPlayer(nullptr)
{
	myBuffer = nullptr;
	myUpdateTime = 0.0f;
	myStatus = VideoStatus::Idle;
	myWantsToPlay = false;
	myTexture = nullptr;
	myIsLooping = false;
}

Tga::Video::~Video()
{
	if (myTexture)
	{
		delete myTexture;
		myTexture = nullptr;
	}

	if (myBuffer)
	{
		delete[] myBuffer;
	}

	delete myPlayer;
	myPlayer = nullptr;
	
}

void Tga::Video::Play(bool aLoop)
{
	myWantsToPlay = true;
	myIsLooping = aLoop;
}

void Tga::Video::Pause()
{
	myWantsToPlay = false;
}

void Tga::Video::Stop()
{
	myStatus = VideoStatus::Idle;
	myWantsToPlay = false;
	if (myPlayer) 
	{
		myPlayer->Stop();
	}
}

void Tga::Video::Restart()
{
	myWantsToPlay = true;
	if (myPlayer)
	{
		myPlayer->RestartStream();
	}
}

bool Video::Init(const char* aPath, bool aPlayAudio)
{
	if (myPlayer) return false;

	myPlayer = new VideoPlayer();
	if (myPlayer)
	{
		FilePathStream resolvedPath;
		if (!Tga::Settings::ResolveAssetPath(aPath, resolvedPath))
		{
			ERROR_PRINT("%s %s %s", "Could not load video: ", aPath, ". File not found");
			return false;
		}

		VideoError error = myPlayer->Init(resolvedPath.GetData(), aPlayAudio);
		if (error == VideoError_WrongFormat || error == VideoError_FileNotFound)
		{
			ERROR_PRINT("%s %s %s", "Could not load video: ", aPath, ". Wrong format?");
			return false;
		}
	}

	if (!myPlayer->DoFirstFrame())
	{
		ERROR_PRINT("%s %s %s", "Video error: ", aPath, ". First frame not found?");
		return false;
	}
	
	mySize.x = myPlayer->GetAvVideoFrame()->width;
	mySize.y = myPlayer->GetAvVideoFrame()->height;

	myPowerSizeX = (int)powf(2.0f, ceilf(logf((float)mySize.x) / logf(2.0f)));
	myPowerSizeY = (int)powf(2.0f, ceilf(logf((float)mySize.y) / logf(2.0f)));

	myBuffer = new int[(myPowerSizeX * myPowerSizeY)];
	myStatus = VideoStatus::Playing;

	if (!myShaderResource)
	{
		D3D11_TEXTURE2D_DESC texture_desc;
		memset(&texture_desc, 0, sizeof(texture_desc));
		texture_desc.Width = myPowerSizeX;
		texture_desc.Height = myPowerSizeY;
		texture_desc.MipLevels = 1;
		texture_desc.ArraySize = 1;
		texture_desc.SampleDesc.Count = 1;
		texture_desc.SampleDesc.Quality = 0;
		texture_desc.Usage = D3D11_USAGE_DYNAMIC;
		texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		texture_desc.MiscFlags = 0;

		DX11::Device->CreateTexture2D(&texture_desc, nullptr, myD3DTexture.ReleaseAndGetAddressOf());
		DX11::Device->CreateShaderResourceView(myD3DTexture.Get(), NULL, myShaderResource.ReleaseAndGetAddressOf());

		myTexture = new TextureResource(myShaderResource.Get());
	}

	bool wantsToPlay = myWantsToPlay;
	myWantsToPlay = true;

	if (myShaderResource && myD3DTexture)
	{
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT result = DX11::Context->Map(myD3DTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

		if (SUCCEEDED(result))
		{
			unsigned int* dest = reinterpret_cast<unsigned int*>(mappedResource.pData);
			myPlayer->Update(dest, myPowerSizeX, myPowerSizeY);
			DX11::Context->Unmap(myD3DTexture.Get(), 0);
		}
	}
	myUpdateTime = 0.0f;
	myWantsToPlay = wantsToPlay;

	return true;
}

void Video::Update(float aDelta)
{
	if (!myWantsToPlay || !myPlayer) return;

	// Clamp the high frame initialization/stutter spike value
	aDelta = std::min(aDelta, 0.1f);
	myUpdateTime += aDelta;

	double fps = myPlayer->GetFps();
	if (fps <= 0.0) return;

	const double frameTime = 1.0 / fps;
	const int maxFramesPerUpdate = 8; // Higher parsing lookahead window
	int framesDecoded = 0;

	while (myUpdateTime >= frameTime && framesDecoded < maxFramesPerUpdate)
	{
		if (myShaderResource && myD3DTexture)
		{
			int status = myPlayer->GrabNextFrame();

			if (status < 0)
			{
				myStatus = VideoStatus::ReachedEnd;
				if (myIsLooping) Restart();
				else myWantsToPlay = false;
				return;
			}

			D3D11_MAPPED_SUBRESOURCE mappedResource;
			HRESULT result = DX11::Context->Map(myD3DTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

			if (SUCCEEDED(result))
			{
				unsigned int* dest = reinterpret_cast<unsigned int*>(mappedResource.pData);
				myPlayer->Update(dest, myPowerSizeX, myPowerSizeY);
				DX11::Context->Unmap(myD3DTexture.Get(), 0);
			}
		}
		myUpdateTime -= (float)frameTime;
		framesDecoded++;
	}
}
#endif