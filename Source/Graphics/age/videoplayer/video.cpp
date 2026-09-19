#include "stdafx.h"
#include <age/videoplayer/video.h>
#include <age/videoplayer/videoplayer.h>

#ifdef USE_VIDEO
#include <age/sprite/sprite.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/texture/TextureManager.h>
#include <age/log/Log.h>
#include <age/application.h>
using namespace Ag;
Ag::Video::Video() : myPlayer(nullptr)
{
	myBuffer = nullptr;
	myUpdateTime = 0.0f;
	myStatus = VideoStatus::Idle;
	myWantsToPlay = false;
	myTexture = nullptr;
	myIsLooping = false;
}

Ag::Video::~Video()
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

	if (rhi::IDevice* dev = DX11::Rhi())
	{
		if (myVideoSrv.IsValid()) dev->Destroy(myVideoSrv);
		if (myVideoTex.IsValid()) dev->Destroy(myVideoTex);
	}

	delete myPlayer;
	myPlayer = nullptr;

}

void Ag::Video::Play(bool aLoop)
{
	myWantsToPlay = true;
	myIsLooping = aLoop;
}

void Ag::Video::Pause()
{
	myWantsToPlay = false;
}

void Ag::Video::Stop()
{
	myStatus = VideoStatus::Idle;
	myWantsToPlay = false;
	if (myPlayer) 
	{
		myPlayer->Stop();
	}
}

void Ag::Video::Restart()
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
		if (!Ag::Settings::ResolveAssetPath(aPath, resolvedPath))
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
		rhi::IDevice* dev = DX11::Rhi();

		rhi::TextureDesc td;
		td.width = myPowerSizeX;
		td.height = myPowerSizeY;
		td.format = rhi::Format::R8G8B8A8_UNorm_sRGB;
		td.bind = rhi::TextureBind::ShaderResource;
		td.debugName = "Video texture";
		myVideoTex = dev->CreateTexture(td);
		myVideoSrv = dev->CreateSrv(myVideoTex, {});

		// myShaderResource takes its own ref (ComPtr's raw-pointer ctor AddRefs) so
		// the legacy TextureResource(ID3D11ShaderResourceView*) API keeps working;
		// myVideoTex/myVideoSrv remain the RHI-owned handles, destroyed in ~Video().
		myShaderResource = static_cast<ID3D11ShaderResourceView*>(dev->GetNativeSrv(myVideoSrv));

		myTexture = new TextureResource(myShaderResource.Get());
	}

	bool wantsToPlay = myWantsToPlay;
	myWantsToPlay = true;

	if (myShaderResource && myVideoTex.IsValid())
	{
		unsigned int* dest = reinterpret_cast<unsigned int*>(myBuffer);
		myPlayer->Update(dest, myPowerSizeX, myPowerSizeY);
		DX11::Rhi()->GetContext().UpdateTexture(myVideoTex, myBuffer, myPowerSizeX * 4);
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
		if (myShaderResource && myVideoTex.IsValid())
		{
			int status = myPlayer->GrabNextFrame();

			if (status < 0)
			{
				myStatus = VideoStatus::ReachedEnd;
				if (myIsLooping) Restart();
				else myWantsToPlay = false;
				return;
			}

			unsigned int* dest = reinterpret_cast<unsigned int*>(myBuffer);
			myPlayer->Update(dest, myPowerSizeX, myPowerSizeY);
			DX11::Rhi()->GetContext().UpdateTexture(myVideoTex, myBuffer, myPowerSizeX * 4);
		}
		myUpdateTime -= (float)frameTime;
		framesDecoded++;
	}
}
#endif