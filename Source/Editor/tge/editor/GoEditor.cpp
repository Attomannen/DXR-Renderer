#include "stdafx.h"
#include <tge/editor/GoEditor.h>

#include <tge/input/InputManager.h>

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/settings/settings.h>

#include <tge/editor/Editor.h>

#include <tge/Script/Nodes/CommonNodes.h>

#include "tge/Application.h"

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

