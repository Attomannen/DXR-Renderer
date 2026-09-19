#include "stdafx.h"
#include <age/editor/PlaySession.h>
#include <age/log/Log.h>

namespace
{
	Ag::PlaySessionHooks locHooks;
	bool locPlaying = false;
}

void Ag::PlaySession::Install(const PlaySessionHooks& aHooks)
{
	locHooks = aHooks;
}

bool Ag::PlaySession::Available()
{
	return locHooks.start && locHooks.tick && locHooks.stop;
}

bool Ag::PlaySession::IsPlaying()
{
	return locPlaying;
}

bool Ag::PlaySession::Start(const char* aLevelPath, unsigned int aWidth, unsigned int aHeight)
{
	if (locPlaying) return true;
	if (!Available()) return false;
	locPlaying = locHooks.start(aLevelPath ? aLevelPath : "", aWidth, aHeight);
	if (!locPlaying)
		ERROR_PRINT("%s", "Play: the in-editor session could not start.");
	return locPlaying;
}

void Ag::PlaySession::Tick(float aDeltaSeconds, RenderTarget* aColor, DepthBuffer* aDepth,
                           int aOriginX, int aOriginY,
                           unsigned int aWidth, unsigned int aHeight, bool aInputActive)
{
	if (!locPlaying || !locHooks.tick) return;
	locHooks.tick(aDeltaSeconds, aColor, aDepth, aOriginX, aOriginY, aWidth, aHeight, aInputActive);
}

void Ag::PlaySession::Stop()
{
	if (!locPlaying) return;
	locPlaying = false;
	if (locHooks.stop) locHooks.stop();
}

void Ag::PlaySession::WinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam)
{
	if (locPlaying && locHooks.winProc) locHooks.winProc(aMessage, aWParam, aLParam);
}
