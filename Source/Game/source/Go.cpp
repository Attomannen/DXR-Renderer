#define _CRT_SECURE_NO_WARNINGS

#include "GameWorld.h"
#include <algorithm>
#include <string>
#include <age/debugging/CpuProfiler.h>
#include <chrono>
#include <cstdio>

#include <age/input/InputManager.h>
#include <age/scene/Scene.h>
#include <age/scene/SceneSerialize.h>
#include <age/settings/settings.h>
#include <age/log/Log.h>
#include <age/application.h>

#include "age/graphics/GraphicsEngine.h"
#include <age/windows/CrashHandler.h>

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


namespace Ag
{
	void EnsureScenePropertiesAreLoaded();
	void EnsureBasePropertiesAreLoaded();

	void EnsureStaticInitializedTypesAreLoaded()
	{
		EnsureScenePropertiesAreLoaded();
		EnsureBasePropertiesAreLoaded();
	}
}


// Set by Go from argv[1]; read by BenchConfig when BENCH_SCENE is unset.
std::string locStartupScene;

const std::string& StartupScene() { return locStartupScene; }

void Go(const char* aStartupScene)
{
	Ag::InstallCrashHandler();

	// The editor hands over a document path like "Scenes\TEST.tgs"; the scene
	// loader wants "Scenes/TEST". Normalise once, here, rather than teaching
	// the loader about editor path spelling.
	if (aStartupScene && *aStartupScene)
	{
		locStartupScene = aStartupScene;
		std::replace(locStartupScene.begin(), locStartupScene.end(), '\\', '/');
		if (locStartupScene.size() > 4 &&
			locStartupScene.compare(locStartupScene.size() - 4, 4, ".tgs") == 0)
			locStartupScene.resize(locStartupScene.size() - 4);
	}

	// DX12 is the engine's default backend. Only set it when a scripted/bench
	// run hasn't already picked one itself (BENCH_* env vars, CI, etc.) --
	// DX11 stays fully supported for backporting via an explicit AGE_RHI=dx11.
	if (!std::getenv("AGE_RHI"))
		_putenv_s("AGE_RHI", "dx12");

	Ag::EnsureStaticInitializedTypesAreLoaded();

	Ag::LoadSettings(AGE_PROJECT_SETTINGS_FILE);

	Ag::ApplicationConfiguration& cfg = Ag::Settings::GetApplicationConfiguration();

	cfg.winProcCallback = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {return WinProc(hWnd, message, wParam, lParam); };
#ifdef _DEBUG
	cfg.activateDebugSystems = Ag::DebugFeature::Fps | Ag::DebugFeature::Mem | Ag::DebugFeature::Cpu | Ag::DebugFeature::Drawcalls | Ag::DebugFeature::OptimizeWarnings | Ag::DebugFeature::Log;
#else
	cfg.activateDebugSystems = Ag::DebugFeature::None;
#endif

	// A timed benchmark run (BENCH_FRAMES>0) must not be vsync-locked.
	if (const char* bf = std::getenv("BENCH_FRAMES"); bf && std::atoi(bf) > 0)
		cfg.enableVSync = false;
	if (const char* nv = std::getenv("BENCH_NOVSYNC"); nv && std::atoi(nv) != 0)
		cfg.enableVSync = false;
	
	cfg.enableVSync = false; // Forced off for testing

	const auto startupBegin = std::chrono::steady_clock::now();
	bool started = false;
	{
		AG_CPU_SCOPE("Application start (window, device)");
		started = Ag::Application::Start();
	}
	if (started)
	{
		AG_CPU_SCOPE("Graphics engine start");
		started = Ag::GraphicsEngine::Start();
	}
	if (!started)
	{
		ERROR_PRINT("Fatal error! Engine could not start!");
		system("pause");
		return;
	}

	{
		GameWorld gameWorld;
		{
			AG_CPU_SCOPE("GameWorld init (scene load)");
			gameWorld.Init();
		}
		const double startupMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupBegin).count();
		std::printf("Startup took %.0f ms\n", startupMs);
		Ag::CpuProfiler::Get().PrintLoadReport(5.0);

		Ag::Application& application = *Ag::Application::GetInstance();
		Ag::GraphicsEngine& graphicsEngine = *Ag::GraphicsEngine::GetInstance();

		while (application.BeginFrame() && graphicsEngine.BeginFrame())
		{
			{
				AG_CPU_SCOPE("Game update");
				gameWorld.Update(application.GetDeltaTime());
			}
			{
				AG_CPU_SCOPE("Game render");
				gameWorld.Render();
			}
			{
				AG_CPU_SCOPE("Graphics end frame");
				graphicsEngine.EndFrame();
			}
			application.EndFrame();
		}
	}

	Ag::GraphicsEngine::GetInstance()->Shutdown();
	Ag::Application::GetInstance()->Shutdown();
}

