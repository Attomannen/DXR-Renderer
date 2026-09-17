#define _CRT_SECURE_NO_WARNINGS

#include "GameWorld.h"
#include <tge/debugging/CpuProfiler.h>
#include <chrono>
#include <cstdio>

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

	const auto startupBegin = std::chrono::steady_clock::now();
	bool started = false;
	{
		TGA_CPU_SCOPE("Application start (window, device)");
		started = Tga::Application::Start();
	}
	if (started)
	{
		TGA_CPU_SCOPE("Graphics engine start");
		started = Tga::GraphicsEngine::Start();
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
			TGA_CPU_SCOPE("GameWorld init (scene load)");
			gameWorld.Init();
		}
		const double startupMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupBegin).count();
		std::printf("Startup took %.0f ms\n", startupMs);
		Tga::CpuProfiler::Get().PrintLoadReport(5.0);

		Tga::Application& application = *Tga::Application::GetInstance();
		Tga::GraphicsEngine& graphicsEngine = *Tga::GraphicsEngine::GetInstance();

		while (application.BeginFrame() && graphicsEngine.BeginFrame())
		{
			{
				TGA_CPU_SCOPE("Game update");
				gameWorld.Update(application.GetDeltaTime());
			}
			{
				TGA_CPU_SCOPE("Game render");
				gameWorld.Render();
			}
			{
				TGA_CPU_SCOPE("Graphics end frame");
				graphicsEngine.EndFrame();
			}
			application.EndFrame();
		}
	}

	Tga::GraphicsEngine::GetInstance()->Shutdown();
	Tga::Application::GetInstance()->Shutdown();
}

