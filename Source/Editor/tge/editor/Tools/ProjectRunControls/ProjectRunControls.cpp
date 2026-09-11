#include <tge/editor/Tools/ProjectRunControls/ProjectRunControls.h>

#include <tge/editor/Editor.h>
#include <tge/editor/Document/Document.h>

#include <tge/settings/settings.h>
#include <tge/scene/Scene.h>
#include <tge/scene/SceneSerialize.h>

#include <imgui.h>

#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX 
#include <Windows.h>

static STARTUPINFO si{};
static PROCESS_INFORMATION pi{};
static bool hasLaunchedGame = false;

void Tga::ProjectRunControls::ExecuteRun(Tga::Document &aDocument)
{
	if (hasLaunchedGame)
	{
		if (pi.hProcess && TerminateProcess(pi.hProcess, WM_CLOSE)) 
		{
			if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0)
			{
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
				ZeroMemory(&si, sizeof(si));
				ZeroMemory(&pi, sizeof(pi));
				hasLaunchedGame = false;
			}
		}
	}

	{
		aDocument.Save();

		int result = 0;
		//if (ImGui::GetIO().KeyShift) {
			//result = Editor::MSBuild("../Game.sln");
		//}
		if (result == 0) {
			ZeroMemory(&si, sizeof(si));
			ZeroMemory(&pi, sizeof(pi));
			si.cb = sizeof(si);
#ifdef _DEBUG
			const std::string exe = Editor::GetEditor()->GetEditorConfiguration().debugExeName;
			const wchar_t* exe_path = Editor::GetEditor()->GetEditorConfiguration().debugExePath;
#else
			const std::string exe = Editor::GetEditor()->GetEditorConfiguration().releaseExeName;
			const wchar_t* exe_path = Editor::GetEditor()->GetEditorConfiguration().releaseExePath;
#endif
			const std::string cmdline = exe + " " + aDocument.GetPath().data();
			const std::wstring cmdline_w = string_cast<std::wstring>(cmdline);

			// note: CreateProcess second argument is the FULL commandline, so to get argv[1] in GameMain to
			// correspond to the path to a scene we MUST see the second argument as the full argv in GameMain
			bool createResult = CreateProcess(exe_path, (LPWSTR)cmdline_w.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
			if (createResult)
			{
				hasLaunchedGame = true;
			}
		}
	}
}
