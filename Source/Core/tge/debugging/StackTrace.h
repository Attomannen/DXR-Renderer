#pragma once

#include <functional>
#include <array>
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

namespace Tga
{
    class StackTrace
    {
        static constexpr std::size_t STACK_TRACE_MAX_LINES = 32;

    public:
        StackTrace();

        static StackTrace CaptureStackTrace(int aSkipDepth);
        void Print() const;
        std::size_t ComputeHash() const;
        bool operator==(const StackTrace& aStackTrace) const noexcept;

    private:
        std::array<void*, STACK_TRACE_MAX_LINES> myLineAddresses = {}; // Fixed-size array for stack frame addresses
        std::size_t myLineCount = 0;  // Number of valid stack frames
        ULONG myBackTraceHash = 0;    // Hash value returned by CaptureStackBackTrace
    };
}

namespace std
{
    template<> struct hash<Tga::StackTrace>
    {
        std::size_t operator()(const Tga::StackTrace& aStackTrace) const noexcept
        {
            return aStackTrace.ComputeHash();
        }
    };
}