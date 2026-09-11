#include "stdafx.h"
#include <tge/log/Log.h>
#include <tge/util/FixedStream.h>
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <cfloat>
using namespace Tga;


constexpr int LOG_COLORS[] = {
	10, // LogType::Log   -> Green
	12, // LogType::Error -> Red
	14  // LogType::Tip   -> Yellow
};
constexpr int CONSOLE_TEXT_COLOR_WHITE = 15;



Log* Log::ourInstance = nullptr;

Log::Log()
	:myErrorsReported(0)
{
	ourInstance = this;
}


Log::~Log()
{
	if (ourInstance == this)
{
		ourInstance = nullptr;
}
}

void Tga::Log::Create()
{
	if (!ourInstance)
	{
		ourInstance = new Log();
	}
}

void Tga::Log::Destroy()
{
	delete ourInstance;
	ourInstance = nullptr;
}

unsigned int Tga::Log::GetErrorsReported()
{
	if (ourInstance)
	{
		return ourInstance->myErrorsReported;
	}
	return 0;
	}

float Tga::Log::RegisterLog(std::string_view aMessage, LogType aType, unsigned int& outCount)
{
	auto currentTime = std::chrono::steady_clock::now();
	auto it = myLogLookup.find(aMessage);
	if (it != myLogLookup.end())
	{
		size_t index = it->second;
		LogEntry& entry = myLogEntries[index];
		entry.count++;
		outCount = entry.count;

		float elapsed = std::chrono::duration<float>(currentTime - entry.lastLogTime).count();
		if (elapsed >= 1.0f)
	{
			mySortedLogEntries.erase(&entry);
			entry.lastLogTime = currentTime;
			mySortedLogEntries.insert(&entry);
	}
		return elapsed;
}

	LogEntry entry;
	entry.message = aMessage;
	entry.type = aType;
	entry.count = 1;
	entry.lastLogTime = currentTime;
	outCount = 1;

	myLogEntries.push_back(std::move(entry));
	
	const std::string& stableMessage = myLogEntries.back().message;
	std::string_view stableView(stableMessage);

	myLogLookup[stableView] = myLogEntries.size() - 1;
	mySortedLogEntries.insert(&myLogEntries.back());

	if (aType == LogType::Error)
	{
		myErrorsReported++;
	}

	return FLT_MAX;
}

void Tga::Log::SetConsoleColor(int aColor)
{
	HANDLE  hConsole;
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!hConsole)
	{
		return;
	}
	SetConsoleTextAttribute(hConsole, static_cast<WORD>(aColor));
}

void Tga::Log::LogWrite(LogType aType, const char* aFile, int aLine, const char* aFormat, va_list aList)
{
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), aFormat, aList);
	
	unsigned int count = 0;
	float elapsedSinceLastLog = FLT_MAX;
	if (ourInstance)
	{
		elapsedSinceLastLog = ourInstance->RegisterLog(buffer, aType, count);
	}
	else
	{
		count = 1;
	}

	if (elapsedSinceLastLog < 1.0f)
	{
		return;
	}

	char printBuffer[4096 + 64];
	if (count > 1)
	{
		snprintf(printBuffer, sizeof(printBuffer), "%s (%u)", buffer, count);
	}
	else
	{
		snprintf(printBuffer, sizeof(printBuffer), "%s", buffer);
	}

	int color = LOG_COLORS[static_cast<int>(aType)];
	SetConsoleColor(color);

	if (aType == LogType::Error)
	{
		const char* file = nullptr;
		if (aFile)
		{
			file = strrchr(aFile, '\\');
			if (!file) file = strrchr(aFile, '/');
			file = file ? file + 1 : aFile;
}
		if (file)
			fprintf(stderr, "%s - in file %s at line:%i \n", printBuffer, file, aLine);
		else
			fprintf(stderr, "%s\n", printBuffer);
	}
	else
	{
		fprintf(stdout, "%s\n", printBuffer);
	}

	OutputDebugStringA(printBuffer);
	OutputDebugStringA("\n");
	SetConsoleColor(CONSOLE_TEXT_COLOR_WHITE);
}

void Tga::Log::LogError(const char* aFile, int aLine, const char* aFormat, ...)
{
	va_list argptr;
	va_start(argptr, aFormat);
	LogWrite(LogType::Error, aFile, aLine, aFormat, argptr);
	va_end(argptr);
}

void Tga::Log::LogInfo(const char* aFormat, ...)
	{
	va_list argptr;
	va_start(argptr, aFormat);
	LogWrite(LogType::Log, nullptr, 0, aFormat, argptr);
	va_end(argptr);
	}

void Tga::Log::LogTip(const char* aFormat, ...)
{
	va_list argptr;
	va_start(argptr, aFormat);
	LogWrite(LogType::Tip, nullptr, 0, aFormat, argptr);
	va_end(argptr);
}

void Tga::Log::LogMessage(LogType aType, const char* aFormat, ...)
{
	va_list argptr;
	va_start(argptr, aFormat);
	LogWrite(aType, nullptr, 0, aFormat, argptr);
	va_end(argptr);
}

void Tga::Log::LogMessage(LogType aType, const char* aFile, int aLine, const char* aFormat, ...)
{
	va_list argptr;
	va_start(argptr, aFormat);
	LogWrite(aType, aFile, aLine, aFormat, argptr);
	va_end(argptr);
}

size_t Tga::Log::GetLatestLogEntries(const LogEntry** aOutBuffer, size_t aMaxCount)
{
	if (!ourInstance || !aOutBuffer || aMaxCount == 0)
	{
		return 0;
	}

	size_t count = 0;
	for (const LogEntry* entry : ourInstance->mySortedLogEntries)
	{
		if (count >= aMaxCount)
		{
			break;
		}
		aOutBuffer[count++] = entry;
}

	for (size_t i = count; i < aMaxCount; ++i)
{
		aOutBuffer[i] = nullptr;
	}

	return count;
}

