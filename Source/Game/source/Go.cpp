#define _CRT_SECURE_NO_WARNINGS

#include "GameWorld.h"

#include <tge/input/InputManager.h>
#include <tge/scene/Scene.h>
#include <tge/scene/SceneSerialize.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>
#include <tge/application.h>

#include "tge/graphics/GraphicsEngine.h"
#include <tge/windows/CrashHandler.h>

#include <cstdlib>

LRESULT WinProc([[maybe_unused]]HWND hWnd, UINT message, [[maybe_unused]]WPARAM wParam, [[maybe_unused]]LPARAM lParam)
{
	// Feed the benchmark's free-fly camera.
	if (GameWorld* gw = GameWorld::Get())
		gw->OnWinProc((unsigned int)message, (unsigned long long)wParam, (long long)lParam);

	switch (message)
	{
		// this message is read when the window is closed
	case WM_DESTROY:
	{
		// close the application entirely
		PostQuitMessage(0);
		return 0;
	}
	}
	return 0;
}


namespace Tga
{
	void EnsureScenePropertiesAreLoaded();
	void EnsureBasePropertiesAreLoaded();

	void EnsureStaticInitializedTypesAreLoaded()
	{
		EnsureScenePropertiesAreLoaded();
		EnsureBasePropertiesAreLoaded();
	}
}


void Go()
{
	Tga::InstallCrashHandler();

	// DX12 is the engine's default backend. Only set it when a scripted/bench
	// run hasn't already picked one itself (BENCH_* env vars, CI, etc.) --
	// DX11 stays fully supported for backporting via an explicit TGE_RHI=dx11.
	if (!std::getenv("TGE_RHI"))
		_putenv_s("TGE_RHI", "dx12");

	Tga::EnsureStaticInitializedTypesAreLoaded();

	Tga::LoadSettings(TGE_PROJECT_SETTINGS_FILE);

	Tga::ApplicationConfiguration& cfg = Tga::Settings::GetApplicationConfiguration();

	cfg.winProcCallback = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {return WinProc(hWnd, message, wParam, lParam); };
#ifdef _DEBUG
	cfg.activateDebugSystems = Tga::DebugFeature::Fps | Tga::DebugFeature::Mem | Tga::DebugFeature::Cpu | Tga::DebugFeature::Drawcalls | Tga::DebugFeature::OptimizeWarnings | Tga::DebugFeature::Log;
#else
	cfg.activateDebugSystems = Tga::DebugFeature::None;
#endif

	// A timed benchmark run (BENCH_FRAMES>0) must not be vsync-locked.
	if (const char* bf = std::getenv("BENCH_FRAMES"); bf && std::atoi(bf) > 0)
		cfg.enableVSync = false;
	if (const char* nv = std::getenv("BENCH_NOVSYNC"); nv && std::atoi(nv) != 0)
		cfg.enableVSync = false;
	
	cfg.enableVSync = false; // Forced off for testing

	if (!Tga::Application::Start() || !Tga::GraphicsEngine::Start())
	{
		ERROR_PRINT("Fatal error! Engine could not start!");
		system("pause");
		return;
	}

	{
		GameWorld gameWorld;
		gameWorld.Init();

		Tga::Application& application = *Tga::Application::GetInstance();
		Tga::GraphicsEngine& graphicsEngine = *Tga::GraphicsEngine::GetInstance();

		while (application.BeginFrame() && graphicsEngine.BeginFrame())
		{
			gameWorld.Update(application.GetDeltaTime());
			gameWorld.Render();

			graphicsEngine.EndFrame();
			application.EndFrame();
		}
	}

	Tga::GraphicsEngine::GetInstance()->Shutdown();
	Tga::Application::GetInstance()->Shutdown();
}

