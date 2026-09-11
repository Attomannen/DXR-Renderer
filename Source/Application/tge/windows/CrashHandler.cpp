#include "stdafx.h"
#include <tge/windows/CrashHandler.h>
#include <tge/log/Log.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace
{
	LONG WINAPI CrashHandlerFilter(EXCEPTION_POINTERS* aInfo)
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
}

namespace Tga
{
	void InstallCrashHandler()
	{
		SetUnhandledExceptionFilter(CrashHandlerFilter);
	}
}
