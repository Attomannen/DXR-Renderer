#include "stdafx.h"
#include "StackTrace.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <array>
#include <string>

#pragma comment(lib, "Dbghelp.lib")

namespace Tga
{
    StackTrace::StackTrace() : myLineCount(0), myBackTraceHash(0) {}

    bool StackTrace::operator==(const StackTrace& aStackTrace) const noexcept
    {
        return myBackTraceHash == aStackTrace.myBackTraceHash;
    }

    StackTrace StackTrace::CaptureStackTrace(int aSkipDepth)
    {
        StackTrace stackTrace;
        void* buffer[STACK_TRACE_MAX_LINES] = { 0 };

        stackTrace.myLineCount = CaptureStackBackTrace(
            static_cast<ULONG>(aSkipDepth + 1),
            STACK_TRACE_MAX_LINES,
            buffer,
            &stackTrace.myBackTraceHash
        );

        std::copy(buffer, buffer + stackTrace.myLineCount, stackTrace.myLineAddresses.begin());

        return stackTrace;
    }

    void StackTrace::Print() const
    {
        if (myLineCount == 0)
        {
            OutputDebugStringA("Empty Stack Trace\n");
            return;
        }

        // Todo: should do this once for the process, not once per print!
        if (SymInitialize(GetCurrentProcess(), nullptr, TRUE) == FALSE)
        {
            OutputDebugStringA("Failed to initialize symbol resolution\n");
            return;
        }

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = { 0 };
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        for (std::size_t i = 0; i < myLineCount; ++i)
        {
            DWORD64 displacement = 0;
            if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(myLineAddresses[i]), &displacement, symbol))
            {
                OutputDebugStringA(symbol->Name);
                OutputDebugStringA("\n");
            }
            else
            {
                std::string output = "Unable to resolve symbol for address: " + std::to_string(reinterpret_cast<uintptr_t>(myLineAddresses[i])) + "\n";
                OutputDebugStringA(output.c_str());
            }
        }

        // Todo: should do this once for the process, not once per print!
        SymCleanup(GetCurrentProcess());
    }

    std::size_t StackTrace::ComputeHash() const
    {
        return static_cast<std::size_t>(myBackTraceHash);
    }
}