#pragma once

struct EditorConfiguration
{
	bool enableVisualScripts = false;
	const char* debugExeName = "GameMain_Debug.exe";
	const char* releaseExeName = "GameMain_Release.exe";
	const wchar_t* debugExePath = L"..\\Bin\\GameMain_Debug.exe";
	const wchar_t* releaseExePath = L"..\\Bin\\GameMain_Release.exe";

};

constexpr EditorConfiguration DefaultEditorConfiguration = {};
