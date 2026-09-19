// Drives a GameWorld inside the editor viewport.
//
// This lives in GameEditor rather than in Editor because it is the only target
// that links both sides: the editor libraries know nothing about GameWorld, and
// making them depend on it would tie the editor to one game. Editor exposes
// PlaySessionHooks; this installs them.
#include <memory>

#include <age/editor/PlaySession.h>

#include "GameWorld.h"
#include "Go.h"

namespace
{
	std::unique_ptr<GameWorld> locWorld;

	bool PlayStart(const char* aLevelPath, unsigned int aWidth, unsigned int aHeight)
	{
		// GameWorld::Init takes its scene from the startup-scene slot (or
		// BENCH_SCENE), the same one the standalone game fills from argv[1].
		SetStartupScene(aLevelPath);

		// Loading the level overwrites the editor's renderer settings; Stop
		// puts them back.
		GameWorld::SaveSharedRendererState();

		locWorld = std::make_unique<GameWorld>();
		// Size first: Init builds the camera projection and sizes the shared
		// deferred renderer, and both must already be the viewport's.
		locWorld->SetEmbeddedTarget(nullptr, nullptr, 0, 0, aWidth, aHeight);
		locWorld->Init();
		return true;
	}

	void PlayTick(float aDeltaSeconds, Ag::RenderTarget* aColor, Ag::DepthBuffer* aDepth,
	              int aOriginX, int aOriginY,
	              unsigned int aWidth, unsigned int aHeight, bool aInputActive)
	{
		if (!locWorld) return;
		locWorld->SetEmbeddedInput(aInputActive);
		// Set every frame: the viewport target is recreated whenever the panel
		// is resized, so a handle cached at Start would dangle.
		locWorld->SetEmbeddedTarget(aColor, aDepth, aOriginX, aOriginY, aWidth, aHeight);
		locWorld->Update(aDeltaSeconds);
		locWorld->Render();
	}

	void PlayStop()
	{
		if (locWorld) locWorld->SetEmbeddedTarget(nullptr, nullptr, 0, 0, 0, 0);
		locWorld.reset();

		GameWorld::RestoreSharedRendererState();
	}

	void PlayWinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam)
	{
		if (locWorld) locWorld->OnWinProc(aMessage, aWParam, aLParam);
	}
}

void InstallPlaySession()
{
	Ag::PlaySessionHooks hooks;
	hooks.start = &PlayStart;
	hooks.tick = &PlayTick;
	hooks.stop = &PlayStop;
	hooks.winProc = &PlayWinProc;
	Ag::PlaySession::Install(hooks);
}
