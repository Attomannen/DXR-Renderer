#pragma once

namespace Ag
{
	// Installs a top-level SEH handler (SetUnhandledExceptionFilter) that
	// resolves the faulting address to a symbol + source file/line using
	// DbgHelp against this Debug build's own PDB (no external debugger
	// needed), logs it via ERROR_PRINT, then lets the default handler
	// continue -- a real crash still produces whatever dump/dialog it
	// normally would, on top of this. Call once, early in main()/Go().
	//
	// Exists because an unhandled exception can otherwise terminate a
	// process with NO diagnostic at all: no console output, no crash
	// dialog, no dump in %LOCALAPPDATA%\CrashDumps (confirmed happening for
	// GameEditor_Debug.exe on this machine, 2026-09-12 -- see p5g3-dx12-port
	// memory/RENDERING_ROADMAP.md for the investigation that found this).
	void InstallCrashHandler();

	// Logs the current call stack (symbol + file:line) via ERROR_PRINT, without
	// an exception. For diagnosing a fault that abort()s rather than raising --
	// a failed assert, say -- where the top-level filter never runs and the
	// process dies with the assert text and nothing about who called.
	void PrintStackTrace(const char* aWhy);
}
