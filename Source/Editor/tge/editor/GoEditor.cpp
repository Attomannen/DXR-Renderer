#include "stdafx.h"
#include <tge/editor/GoEditor.h>

#include <tge/input/InputManager.h>

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/settings/settings.h>

#include <tge/editor/Editor.h>

#include <tge/Script/Nodes/CommonNodes.h>

#include "tge/Application.h"
#include <tge/log/Log.h>
#include <tge/windows/BackendChooser.h>
#include <tge/windows/CrashHandler.h>
#include <cstdlib>

static const char* locSettingsPath;

Tga::InputManager* SInputManager;

LRESULT WinProc(HWND /*hWnd*/, UINT message, WPARAM wParam, LPARAM lParam)
{
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

void GoEditor(const char* aSettingsPath, const EditorConfiguration& aEditorConfiguration, std::unique_ptr<Tga::EditorGraphicsBase>&& graphics)
{
	Tga::InstallCrashHandler();

	// Only for a plain interactive launch -- see Go.cpp's identical guard.
	// _dupenv_s rather than std::getenv: this project builds with /WX and
	// without _CRT_SECURE_NO_WARNINGS (unlike Go.cpp's), so plain getenv is a
	// hard error here (C4996).
	{
		char* rhiEnv = nullptr;
		size_t rhiEnvLen = 0;
		const bool hasRhiEnv = (_dupenv_s(&rhiEnv, &rhiEnvLen, "TGE_RHI") == 0 && rhiEnv != nullptr);
		if (rhiEnv) free(rhiEnv);
		if (!hasRhiEnv && Tga::ShowBackendChooser(L"TGE Editor - Choose Backend") == Tga::RhiBackendChoice::Cancelled)
			return;
	}

	locSettingsPath = aSettingsPath;

	Tga::LoadSettings(locSettingsPath);
	Tga::ApplicationConfiguration &cfg = Tga::Settings::GetApplicationConfiguration();

	cfg.winProcCallback = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {return WinProc(hWnd, message, wParam, lParam); };
	cfg.activateDebugSystems = Tga::DebugFeature::Filewatcher;

	if (!Tga::Application::Start())
	{
		ERROR_PRINT("Fatal error! Engine could not start!");
		system("pause");
		return;
	}
	
	{
		Tga::Application& application = *Tga::Application::GetInstance();

		Tga::InputManager inputManager(*application.GetHWND());
		SInputManager = &inputManager;

		Tga::Editor editor;
		editor.Init(aEditorConfiguration, std::move(graphics));

		while (application.BeginFrame()) 
		{
			inputManager.Update();
			editor.Update(application.GetDeltaTime(), inputManager);
			application.EndFrame();
		}
	}

	Tga::Application::GetInstance()->Shutdown();
}

