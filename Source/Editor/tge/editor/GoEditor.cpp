#include "stdafx.h"
#include <tge/editor/GoEditor.h>

#include <tge/input/InputManager.h>

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/settings/settings.h>

#include <tge/editor/Editor.h>

#include <tge/Script/Nodes/CommonNodes.h>

#include "tge/Application.h"
#include <tge/log/Log.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

// Top-level SEH handler: without one, an unhandled exception here (or one
// that somehow doesn't reach Windows' default crash-dump collector -- e.g.
// a stack overflow, which can exhaust the stack before WER's own handler
// gets a chance to run) can terminate the process with NO diagnostic at
// all: no console output, no crash dialog, no dump in %LOCALAPPDATA%\
// CrashDumps (found 2026-09-12: DX12 GameEditor "just vanished" the moment
// a scene was opened, confirmed via this filter to be a genuine access
// violation with zero WER dump produced on this machine for this exe --
// this exists to make sure that can never happen silently again). Resolves
// the faulting address to a symbol + source line/file using DbgHelp against
// this Debug build's own PDB (no external tooling needed), logs it, then
// lets the default handler continue (so a real crash still produces
// whatever its normal dump/dialog would be, on top of this).
static LONG WINAPI GoEditorCrashFilter(EXCEPTION_POINTERS* aInfo)
{
	void* addr = aInfo->ExceptionRecord->ExceptionAddress;
	const unsigned code = (unsigned)aInfo->ExceptionRecord->ExceptionCode;
	HANDLE process = GetCurrentProcess();

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (SymInitialize(process, nullptr, TRUE))
	{
		alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		DWORD64 symDisplacement = 0;
		const bool gotSymbol = SymFromAddr(process, (DWORD64)addr, &symDisplacement, symbol);

		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(line);
		DWORD lineDisplacement = 0;
		const bool gotLine = SymGetLineFromAddr64(process, (DWORD64)addr, &lineDisplacement, &line);

		ERROR_PRINT("UNHANDLED EXCEPTION: code=0x%08X at %s+0x%llX (%s:%lu), address=%p",
			code, gotSymbol ? symbol->Name : "???", symDisplacement,
			gotLine ? line.FileName : "???", gotLine ? line.LineNumber : 0u, addr);

		SymCleanup(process);
	}
	else
	{
		ERROR_PRINT("UNHANDLED EXCEPTION: code=0x%08X at address=%p (SymInitialize failed 0x%08X)",
			code, addr, (unsigned)GetLastError());
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

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
	SetUnhandledExceptionFilter(GoEditorCrashFilter);

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

