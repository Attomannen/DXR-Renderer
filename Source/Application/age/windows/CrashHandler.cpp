#include "stdafx.h"
#include <age/windows/CrashHandler.h>
#include <age/log/Log.h>
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

			// The faulting frame alone is often a CRT or driver routine (memset,
			// memcpy) that says nothing about who called it. Walk the rest.
			CONTEXT context = *aInfo->ContextRecord;
			STACKFRAME64 frame = {};
			frame.AddrPC.Offset = context.Rip;    frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
			HANDLE thread = GetCurrentThread();
			for (int depth = 0; depth < 32; ++depth)
			{
				if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
					SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0)
					break;
				const DWORD64 pc = frame.AddrPC.Offset;
				DWORD64 frameDisplacement = 0;
				const bool frameSymbol = SymFromAddr(process, pc, &frameDisplacement, symbol);
				IMAGEHLP_LINE64 frameLine = {};
				frameLine.SizeOfStruct = sizeof(frameLine);
				DWORD frameLineDisplacement = 0;
				const bool frameHasLine = SymGetLineFromAddr64(process, pc, &frameLineDisplacement, &frameLine);
				char moduleName[MAX_PATH] = "?";
				if (HMODULE module = reinterpret_cast<HMODULE>(SymGetModuleBase64(process, pc)))
				{
					char modulePath[MAX_PATH] = {};
					if (GetModuleFileNameA(module, modulePath, MAX_PATH))
					{
						const char* slash = strrchr(modulePath, '\\');
						strncpy_s(moduleName, slash ? slash + 1 : modulePath, _TRUNCATE);
					}
				}
				ERROR_PRINT("  #%02d %s!%s+0x%llX (%s:%lu)", depth, moduleName,
					frameSymbol ? symbol->Name : "???", frameDisplacement,
					frameHasLine ? frameLine.FileName : "???", frameHasLine ? frameLine.LineNumber : 0u);
			}

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

namespace Ag
{
	void InstallCrashHandler()
	{
		SetUnhandledExceptionFilter(CrashHandlerFilter);
	}

	void PrintStackTrace(const char* aWhy)
	{
		HANDLE process = GetCurrentProcess();
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		// Idempotent in practice: a second SymInitialize on an already
		// initialised process simply fails, and the symbols stay loaded.
		SymInitialize(process, nullptr, TRUE);

		void* frames[32] = {};
		const USHORT captured = CaptureStackBackTrace(1, 32, frames, nullptr);
		ERROR_PRINT("STACK TRACE (%s):", aWhy ? aWhy : "");

		alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		for (USHORT i = 0; i < captured; ++i)
		{
			const DWORD64 pc = reinterpret_cast<DWORD64>(frames[i]);
			DWORD64 displacement = 0;
			const bool gotSymbol = SymFromAddr(process, pc, &displacement, symbol);
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(line);
			DWORD lineDisplacement = 0;
			const bool gotLine = SymGetLineFromAddr64(process, pc, &lineDisplacement, &line);
			ERROR_PRINT("  #%02d %s+0x%llX (%s:%lu)", (int)i,
				gotSymbol ? symbol->Name : "???", displacement,
				gotLine ? line.FileName : "???", gotLine ? line.LineNumber : 0u);
		}
	}
}
