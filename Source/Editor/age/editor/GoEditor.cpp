#include "stdafx.h"
#include <age/debugging/CpuProfiler.h>
#include <age/editor/PlaySession.h>
#include <age/editor/GoEditor.h>

#include <age/input/InputManager.h>

#include <age/script/ScriptNodeTypeRegistry.h>
#include <age/settings/settings.h>

#include <age/editor/Editor.h>

#include <age/Script/Nodes/CommonNodes.h>

#include "age/Application.h"
#include <age/log/Log.h>
#include <age/windows/CrashHandler.h>
#include <cstdlib>

static const char* locSettingsPath;

Ag::InputManager* SInputManager;

LRESULT WinProc(HWND /*hWnd*/, UINT message, WPARAM wParam, LPARAM lParam)
{
	// A running in-viewport play session needs the same messages: its own
	// InputManager is what the game's camera and character nodes read. Sent
	// before the editor's own handling so the game sees a complete stream.
	Ag::PlaySession::WinProc((unsigned int)message, (unsigned long long)wParam, (long long)lParam);

	if (SInputManager->UpdateEvents(message, wParam, lParam)) {
		return 0;
	}

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

void GoEditor(const char* aSettingsPath, const EditorConfiguration& aEditorConfiguration, std::unique_ptr<Ag::EditorGraphicsBase>&& graphics)
{
	Ag::InstallCrashHandler();

	// DX12 is the engine's default backend -- see Go.cpp's identical guard.
	// _dupenv_s rather than std::getenv: this project builds with /WX and
	// without _CRT_SECURE_NO_WARNINGS (unlike Go.cpp's), so plain getenv is a
	// hard error here (C4996). DX11 stays fully supported for backporting via
	// an explicit AGE_RHI=dx11.
	{
		char* rhiEnv = nullptr;
		size_t rhiEnvLen = 0;
		const bool hasRhiEnv = (_dupenv_s(&rhiEnv, &rhiEnvLen, "AGE_RHI") == 0 && rhiEnv != nullptr);
		if (rhiEnv) free(rhiEnv);
		if (!hasRhiEnv) _putenv_s("AGE_RHI", "dx12");
	}

	locSettingsPath = aSettingsPath;

	Ag::LoadSettings(locSettingsPath);
	Ag::ApplicationConfiguration &cfg = Ag::Settings::GetApplicationConfiguration();

	cfg.winProcCallback = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {return WinProc(hWnd, message, wParam, lParam); };
	cfg.activateDebugSystems = Ag::DebugFeature::Filewatcher;

	if (!Ag::Application::Start())
	{
		ERROR_PRINT("Fatal error! Engine could not start!");
		system("pause");
		return;
	}
	
	{
		Ag::Application& application = *Ag::Application::GetInstance();

		Ag::InputManager inputManager(*application.GetHWND());
		SInputManager = &inputManager;

		Ag::Editor editor;
		editor.Init(aEditorConfiguration, std::move(graphics));

		// AGE_EDITOR_PROFILE=<n>: every n frames, dump the rolling per-scope
		// averages to stderr. Unbuffered, so the tail survives a crash.
		size_t profileLength = 0;
		char profileEvery[16] = {};
		getenv_s(&profileLength, profileEvery, sizeof(profileEvery), "AGE_EDITOR_PROFILE");
		const int profileInterval = profileLength ? atoi(profileEvery) : 0;
		uint64_t frameIndex = 0;
		int slowFrames = 0;
		double worstFrame = 0.0;

		while (application.BeginFrame()) 
		{
			inputManager.Update();
			editor.Update(application.GetDeltaTime(), inputManager);
			application.EndFrame();

			// A rolling max cannot tell a single stall from a recurring one, and
			// "it hitches" is a statement about frequency. Measured from the
			// profiler, not GetDeltaTime: StepTimer clamps its delta to 1/10 s,
			// so every real stall reads as exactly 100 ms through that.
			const double frameMs = Ag::CpuProfiler::Get().GetFrameMs();
			if (frameMs > 33.0) ++slowFrames;
			if (frameMs > worstFrame) worstFrame = frameMs;

			if (profileInterval > 0 && ++frameIndex % (uint64_t)profileInterval == 0)
			{
				Ag::CpuProfiler& profiler = Ag::CpuProfiler::Get();
				ERROR_PRINT("---- editor frame profile (frame %llu, %.2f ms; %d/%d frames over 33 ms, worst %.1f ms) ----",
					(unsigned long long)frameIndex, profiler.GetFrameMs(),
					slowFrames, profileInterval, worstFrame);
				slowFrames = 0;
				worstFrame = 0.0;
				for (const Ag::CpuProfiler::ScopeStats* st : profiler.GetStats())
					ERROR_PRINT("%*s%-34s avg %7.3f ms  max %7.3f ms",
						st->depth * 2, "", st->name, st->Average(), st->Max());
			}
		}
	}

	Ag::Application::GetInstance()->Shutdown();
}

