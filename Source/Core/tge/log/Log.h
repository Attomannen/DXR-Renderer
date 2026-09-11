#pragma once
#include <unordered_map>
#include <deque>
#include <ostream>
#include <functional>
#include <string_view>
#include <string>
#include <chrono>
#include <set>

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

namespace Tga
{
	enum class LogType
	{
		Log,
		Error,
		Tip
	};

	struct LogEntry
	{
		LogType type;
		std::string message;
		unsigned int count = 0;
		std::chrono::steady_clock::time_point lastLogTime;
	};

#ifndef _RETAIL
	#define ERROR_PRINT(aFormat, ...) Tga::Log::LogError(__FILE__, __LINE__, aFormat, ##__VA_ARGS__)
	#define INFO_PRINT(aFormat, ...) Tga::Log::LogInfo(aFormat, ##__VA_ARGS__)
	#define INFO_TIP(aFormat, ...) Tga::Log::LogTip(aFormat, ##__VA_ARGS__)

#define ASSERT(condition, message, ...) \
		do { \
		if(!condition) \
		{ \
				Tga::Log::LogError(__FILE__, __LINE__, "Assertion '" #condition "' failed in %s! " #message, __FUNCTION__, ##__VA_ARGS__); \
			std::terminate(); \
		} \
		} while (0)
#else
	#define ERROR_PRINT(aFormat, ...) do { } while (0)
	#define INFO_PRINT(aFormat, ...) do { } while (0)
	#define INFO_TIP(aFormat, ...) do { } while (0)

	#define ASSERT(condition, message, ...) do { } while (0)
#endif
    class Log
    {
    public:
		static void Create();
		static void Destroy();

		static void LogError(const char* aFile, int aLine, const char* aFormat, ...);
		static void LogInfo(const char* aFormat, ...);
		static void LogTip(const char* aFormat, ...);

		static void LogMessage(LogType aType, const char* aFormat, ...);
		static void LogMessage(LogType aType, const char* aFile, int aLine, const char* aFormat, ...);

		static size_t GetLatestLogEntries(const LogEntry** aOutBuffer, size_t aMaxCount);

		static unsigned int GetErrorsReported();
		static void SetConsoleColor(int aColor);

    private:
        Log();
        ~Log();

        float RegisterLog(std::string_view aMessage, LogType aType, unsigned int& outCount);
		static void LogWrite(LogType aType, const char* aFile, int aLine, const char* aFormat, va_list aList);

		struct LogEntryCompare
		{
			bool operator()(const LogEntry* aLeft, const LogEntry* aRight) const
			{
				if (aLeft->lastLogTime != aRight->lastLogTime)
				{
					return aLeft->lastLogTime > aRight->lastLogTime;
				}
				return aLeft > aRight;
			}
		};
		std::deque<LogEntry> myLogEntries;
		std::unordered_map<std::string_view, size_t> myLogLookup;
		std::set<const LogEntry*, LogEntryCompare> mySortedLogEntries;
		unsigned int myErrorsReported;

		static Log* ourInstance;
    };
}