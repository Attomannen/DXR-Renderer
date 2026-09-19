#pragma once

namespace Ag
{
	// The engine's log, in an ImGui window, toggled with the grave key (`).
	//
	// This exists because the executables no longer allocate a Win32 console.
	// A second OS window that mirrors stdout is noise on the desktop for the
	// overwhelming majority of runs, but the log itself still has to be
	// reachable -- so it moved inside the application, where it can be opened
	// when wanted and stays out of the way when not.
	//
	// The log's own retained buffer is the backing store; this draws it and
	// owns no copy of its own.
	class ConsolePanel
	{
	public:
		// Draws the window if it is open, and handles the toggle key. Called
		// once per frame from ImGuiInterface::Render.
		static void Draw();

		static bool IsOpen();
		static void SetOpen(bool aOpen);
	};
}
