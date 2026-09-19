#include "stdafx.h"
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

		while (application.BeginFrame()) 
		{
			inputManager.Update();
			editor.Update(application.GetDeltaTime(), inputManager);
			application.EndFrame();
		}
	}

	Ag::Application::GetInstance()->Shutdown();
}

