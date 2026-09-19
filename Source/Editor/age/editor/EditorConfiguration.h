#pragma once

struct EditorConfiguration
{
	const char* debugExeName = "GameMain_Debug.exe";
	const char* releaseExeName = "GameMain_Release.exe";
	// The game sits beside the editor, so these resolve against the working
	// directory rather than being walked to. They used to read
	// "..\Bin\GameMain_*.exe", which only worked because the editor ran from
	// Bin and ".." came straight back to it. Once the build output moved to
	// Run/ that pointed at a folder which no longer exists, CreateProcess
	// returned false and Play silently did nothing.
	const wchar_t* debugExePath = L"GameMain_Debug.exe";
	const wchar_t* releaseExePath = L"GameMain_Release.exe";

};

constexpr EditorConfiguration DefaultEditorConfiguration = {};
