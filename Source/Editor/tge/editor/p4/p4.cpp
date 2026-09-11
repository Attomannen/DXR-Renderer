#include <tge/editor/p4/p4.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>
#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <vector>
#include <unordered_set>
#include <sstream>
#include <wincred.h>
#include <regex>
#include <mutex>
#include <chrono>

#include <tge/util/StringCast.h>
#include <tge/util/FixedStream.h>
#include <tge/settings/settings.h>
#include <tge/scene/Scene.h>
#include <tge/log/Log.h>

#include <imgui.h>
#include <deque>

#pragma comment(lib, "Advapi32.lib")
#pragma warning(push)
#pragma warning(disable : 4996)
#pragma warning(disable : 6031)

static constexpr char p4cmd[] = "p4 -C utf8";
static constexpr char p4port[] = "ssl:perforce.tga.learnet.se:1666";

static std::chrono::steady_clock::time_point locNextPollTime;

static std::regex uuidregex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
static std::regex verifiedResponseRegex("^//.*-\\s.*by\\s\\w*\\.\\w*@\\w*\\.\\w*\\S*$");
static std::regex newlineregex(R"([\r\n.*]+$)");

static char username[256] = {};
static char workspace[512] = {};
static char streamroot[512] = {};
static char streamGameAssetsRoot[512] = {};
static char localGameAssetRoot[128] = {};

static std::atomic<bool> locIsAuthenticated = false;
static std::atomic<P4::ErrorType> locErrorState = P4::ErrorType::None;

using UUIDchar = char[37];

enum class ThreadState { STOPPED, RUNNING, FAILED };

static std::atomic<ThreadState> locThreadState;
static std::unique_ptr<std::thread> locThread;

static std::mutex locInfoCacheMutex;
static std::unordered_map<std::string, P4::FileInfo> locInfoCache;

static std::mutex locPendingAsyncCommandsMutex;
static std::deque<std::string> locPendingAsyncCommands;

// Erronous responses we can get from running P4 commands
//static std::unordered_set<std::string_view> errorMessages = {
static std::unordered_map<std::string_view, P4::ErrorType> errorMessages = {
	{"Yoursessionhasexpired,pleaseloginagain.",			P4::ErrorType::LoginRequired},
	{"Youarenotloggedin.",								P4::ErrorType::LoginRequired},
	{"Perforcepassword(P4PASSWD)invalidorunset.",		P4::ErrorType::LoginRequired},
	{"Error:Connectionfailed.",							P4::ErrorType::Generic},
	{"Filenotfound.",									P4::ErrorType::FileNotFound},
	{"Perforceclienterror:",							P4::ErrorType::Workspace}
};

const char *P4::GetErrorString() {
	switch (locErrorState)
	{ 
		case P4::ErrorType::FileNotFound:	{ return "File not found!"; }
		case P4::ErrorType::Generic:		{ return "Generic error!"; }
		case P4::ErrorType::LoginRequired:	{ return "Login required!"; } 
		case P4::ErrorType::Password:		{ return "Password incorrect!"; } 
		case P4::ErrorType::Workspace:		{ return "Workspace missing!"; } 
		default: { return "Unknown Error!"; }
	}
}

static void RunCommand(const char*, std::vector<char>&, const size_t);

static bool HasCommandBufferErrors(const std::string_view buffer)
{
	//if (std::regex_match(std::regex_replace(buffer.data(), newlineregex, ""), verifiedResponseRegex))
	std::string response = buffer.data();
	response.erase(std::remove_if(response.begin(), response.end(), ::isspace), response.end());
	locErrorState = P4::ErrorType::None;

	if (errorMessages.find(response) != errorMessages.end())
	{
		auto status = errorMessages.find(response);
		locErrorState = status->second;
		return true;
	}
	return false;
}

static bool ParseFileInfo(char* aBuffer)
{
	if (std::string(aBuffer).find("... - file(s) not opened") != std::string::npos)
	{
		return true;
	}

	std::vector<std::string> changeLines;
	bool dirty = false;
	{ // Split into a vector where each element is one change line from p4 if there are multiple lines to check
		std::lock_guard<std::mutex> lock(locInfoCacheMutex);
		std::string strBuffer(aBuffer);

		std::regex nlRegex("\r\n");
		std::sregex_token_iterator it(strBuffer.begin(), strBuffer.end(), nlRegex, -1);
		std::sregex_token_iterator end;
		while (it != end)
		{
			changeLines.push_back((*it).str());
			++it;
		}
	}

	//* Loop through and do a couple of things:
	// 1. Goes through lines to parse the specific p4-change
	// 2. caching them if not cached
	// 3. removing from cache if they are cached but no longer reported as a change
	for (std::string& l : changeLines)
	{
		char* line = l.data();
		if (HasCommandBufferErrors(line))
		{
			P4::FileInfo fileinfo{};
			fileinfo.action = P4::FileAction::Error;

			{
				std::lock_guard<std::mutex> lock(locInfoCacheMutex);
				locInfoCache["lasterror"] = std::move(fileinfo);
			}
			return true;
		}

		{
			size_t linehash = std::hash <std::string>{}(line);
			P4::FileInfo info{};

			char path[256];
			strcpy_s(path, strtok_s(line, "#", &line));

			{
				std::lock_guard<std::mutex> lock(locInfoCacheMutex);
				if (locInfoCache.contains(path))
				{
					if (locInfoCache[path].cachedLine == linehash)
					{
						locInfoCache[path].inUse = true;
						continue;
					}
				}
			}
			dirty = true;
			info.cachedLine = linehash;
			info.revision = std::atoi(strtok_s(line, " ", &line));

			strtok_s(line, " ", &line);
			char action[16];
			strcpy(action, strtok_s(line, " ", &line));
			if (strcmp(action, "add") == 0) { info.action = P4::FileAction::Add; }
			else if (strcmp(action, "edit") == 0) { info.action = P4::FileAction::Edit; }
			else if (strcmp(action, "delete") == 0) { info.action = P4::FileAction::Delete; }

			if (strstr(line, "default") != 0)
			{
				strcpy(info.changelist, strtok_s(line, " ", &line));
				strtok_s(line, " ", &line);
			}
			else
			{
				strtok_s(line, " ", &line);
				strcpy(info.changelist, strtok_s(line, " ", &line));
			}

			strcpy_s(info.fileType, strtok_s(&line[1], ")", &line));

			strtok_s(line, " ", &line);
			char* userAndClient = strtok_s(line, "\0", &line);
			if (userAndClient)
			{
				char* user = strtok(userAndClient, "@");
				char* client = strtok(nullptr, "\0");
				if (user)
				{
					strcpy_s(info.user, user);
				}
				if (client)
				{
					strcpy_s(info.client, client);
				}
			}

			{
				std::lock_guard<std::mutex> lock(locInfoCacheMutex);
				info.inUse = true;
				locInfoCache[path] = info;
			}
		}
	}
	
	// clean cache.
	// If info.inUse is true it means either that it was just added, or already in cache. 
	// Otherwise it should be safe to remove it from the cache
	{
		std::lock_guard<std::mutex> lock(locInfoCacheMutex);

		auto it = locInfoCache.begin();
		while (it != locInfoCache.end())
		{
			if (!(*it).second.inUse)
			{
				it = locInfoCache.erase(it);
			}
			else
			{
				(*it).second.inUse = false;
				it++;
			}
		}
	}

	return dirty;
}

static void RunCommand(const char* command, std::vector<char>& buffer, const size_t chunkSize = 1024)
{
	HANDLE stdoutRead, stdoutWrite;
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

	if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0))
	{
		ERROR_PRINT("CreatePipe failed!");
		CloseHandle(stdoutWrite);
		CloseHandle(stdoutRead);
		return;
	}

	if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0))
	{
		ERROR_PRINT("SetHandleInformation failed!");
		CloseHandle(stdoutWrite);
		CloseHandle(stdoutRead);
		return;
	}

	STARTUPINFO si = {};
	si.cb = sizeof(STARTUPINFO);
	si.hStdOutput = stdoutWrite;
	si.hStdError = stdoutWrite;
	si.dwFlags |= STARTF_USESTDHANDLES;

	std::string fullcmd = "cmd.exe /C ";
	{
		//std::lock_guard <std::mutex> lock(locPollMutex);
		fullcmd += command;
		//fullcmd += "\r\n";
	}

	PROCESS_INFORMATION pi = {};
	if (!CreateProcess(
		NULL,
		&string_cast<std::wstring>(fullcmd)[0],
		NULL, NULL, TRUE, 0, NULL, NULL,
		&si,
		&pi))
	{
		ERROR_PRINT("CreateProcess failed!");
		CloseHandle(stdoutWrite);
		CloseHandle(stdoutRead);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return;
	}

	CloseHandle(stdoutWrite);

	DWORD bytesRead;
	size_t totalRead = 0;
	{

		buffer.resize(chunkSize);
		while (ReadFile(stdoutRead, buffer.data() + totalRead, (DWORD)chunkSize, &bytesRead, NULL) && bytesRead > 0) {
			totalRead += bytesRead;
			if (totalRead + chunkSize > buffer.size()) {
				buffer.resize(buffer.size() + chunkSize);  // Expand buffer
			}
		}

		buffer.resize(totalRead);
		buffer.push_back('\0');
	}


	CloseHandle(stdoutRead);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

const char* P4::MyClient()
{
	std::lock_guard<std::mutex> lock(locInfoCacheMutex);
	if (workspace[0] == '\0')
		return "";
	return workspace;
}
const char* P4::MyUser()
{
	std::lock_guard<std::mutex> lock(locInfoCacheMutex);
	if (username[0] == '\0')
		return "";

	/*
	TODO; this should not run blocking, has to run in the background
	{
		char buffer[256];
		RunCommand("p4 login -s", buffer, 256);

		char* context;
		strtok_s(buffer, " ", &context);
		strcpy(myUser, strtok(context, " "));
	}*/
	return username;
}

void P4::CheckoutFile(std::string_view aPath)
{
	Tga::FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << aPath;
	fullPath.NormalizePath();

	char command[512];
	sprintf(command, "%s edit %s", p4cmd, fullPath.GetData());

	std::lock_guard guard{ locPendingAsyncCommandsMutex };

	locPendingAsyncCommands.push_back(command);
}

void P4::MarkFileForAdd(std::string_view aPath)
{
	// for now we just reconcile regardless, since we don't know the files previous status
	Tga::FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << aPath;
	fullPath.NormalizePath();

	char command[512];
	sprintf(command, "%s reconcile %s", p4cmd, fullPath.GetData());

	std::lock_guard guard{ locPendingAsyncCommandsMutex };

	locPendingAsyncCommands.push_back(command);
}

void P4::MarkFileForDelete(std::string_view aPath)
{
	Tga::FilePathStream fullPath;
	fullPath << Tga::Settings::GameAssetRoot() << "/" << aPath;
	fullPath.NormalizePath();

	char command[512];
	sprintf(command, "%s reconcile %s", p4cmd, fullPath.GetData());

	std::lock_guard guard{ locPendingAsyncCommandsMutex };

	locPendingAsyncCommands.push_back(command);
}

bool P4::QueryHasFileInfo(std::string_view aPath)
{
	// normalisera alla paths!
	fs::path root = streamGameAssetsRoot;
	bool result = false;
	{
		std::lock_guard<std::mutex> lock(locInfoCacheMutex);
		fs::path assetpath = root / aPath;
		result = locInfoCache.contains(assetpath.generic_string());
	}
	return result;
}

const P4::FileInfo P4::GetFileInfo(std::string_view aPath)
{
	P4::FileInfo info;
	fs::path assetpath = streamGameAssetsRoot;
	assetpath = assetpath / aPath;

	std::lock_guard<std::mutex> lock(locInfoCacheMutex);

	auto it = locInfoCache.find(assetpath.generic_string());
	if (it != locInfoCache.end())
	{
		const auto& cache = it->second;
		strcpy(info.depotPath, cache.depotPath);
		info.revision = cache.revision;
		info.action = cache.action;
		strcpy(info.changelist, cache.changelist);
		strcpy(info.fileType, cache.fileType);
		strcpy(info.user, cache.user);
		strcpy(info.client, cache.client);
		strcpy(info.client, cache.client);
		info.cachedLine = cache.cachedLine;
	}

	return (info);
}

void P4::StopPolling()
{
	if (locThreadState != ThreadState::STOPPED)
	{
		locThreadState = ThreadState::STOPPED;
		if (locThread && locThread->joinable())
		{
			locThread->join();
		}

		std::lock_guard<std::mutex> lock(locInfoCacheMutex);
		locInfoCache.clear();

	}
}

bool P4::TrySetClient()
{
	std::lock_guard<std::mutex> lock(locInfoCacheMutex);
	char command[512];
	std::vector<char> response;

	////////////////////////////
	// Set P4PORT to: ssl:perforce.tga.learnet.se : 1666
	{
		sprintf(command, "%s set P4PORT=%s", p4cmd, p4port);
		RunCommand(command, response);
		if (HasCommandBufferErrors(response.data()))
		{
			return false;
		}
	}

	////////////////////////////
	// Find p4 username
	{
		sprintf(command, "%s info | findstr \"User\"", p4cmd);
		RunCommand(command, response);
		if (HasCommandBufferErrors(response.data()))
		{
			return false;
		}
		if (response.size() == 0)
		{
			strcpy(command, "echo %USERNAME%");
			RunCommand(command, response);
			if (HasCommandBufferErrors(response.data()))
			{
				return false;
			}
		}

		response.erase(std::remove_if(response.begin(), response.end(), ::isspace),
			response.end());
		std::string r = response.data();

		size_t from = strlen("Username:");
		strcpy(username, r.substr(from, r.length() - from).c_str());
	}

	////////////////////////////
	// Find p4 workspace
	{
		sprintf(command, "%s clients -u %s", p4cmd, username);
		RunCommand(command, response);
		if (HasCommandBufferErrors(response.data()))
		{
			return false;
		}

		fs::path projectpath = fs::absolute(Tga::Settings::EngineAssetRoot()).parent_path().parent_path();
		std::string workspaces = response.data();
		size_t from = workspaces.find(projectpath.string().c_str());
		workspaces = workspaces.substr(0, from);
		from = workspaces.find_last_of('\n');
		workspaces = workspaces.substr(from, workspaces.length() - from);
		from = workspaces.find_first_of(' ') + 1;
		size_t to = workspaces.find_first_of(' ', from);

		strcpy(workspace, workspaces.substr(from, to - from).c_str());

		sprintf(command, "%s set P4CLIENT=\"%s\"", p4cmd, workspace);
		RunCommand(command, response);
		if (HasCommandBufferErrors(response.data()))
		{
			return false;
		}
	}

	///////////////////////////////
	// Find depot path
	{
		sprintf(command, "%s info", p4cmd);
		RunCommand(command, response);
		if (HasCommandBufferErrors(response.data()))
		{
			return false;
		}
		std::string response_str = response.data();
		size_t from = response_str.find("Client stream: ");
		from += strlen("Client stream: ");
		size_t to = response_str.find_first_of("\r\n", from);
		response_str = response_str.substr(from, to-from);
		strcpy(streamroot, response_str.c_str());
		sprintf_s(streamGameAssetsRoot, "%s/Source/Game/data/", streamroot);
	}

	locIsAuthenticated = true;
	return true;
}

P4::ErrorType P4::QueryErrorState()
{
	return locErrorState;
}

void P4::StartPolling(const char* aFolder, const unsigned int someUpdateIntervalSeconds, AuthenticationCallback requestAuthCallback)
{
	locErrorState = P4::ErrorType::None;
	if (locThreadState == ThreadState::RUNNING)
	{
		// Already running so just return
		return;
	}
	locThreadState = ThreadState::RUNNING;

	locThread = std::make_unique<std::thread>([folder = std::string(aFolder), someUpdateIntervalSeconds, requestAuthCallback]() {
		locIsAuthenticated = P4::TrySetClient();
		strcpy(localGameAssetRoot, Tga::Settings::ResolveAssetPath(folder.c_str()).data());

		locNextPollTime = std::chrono::steady_clock::now();

		while (true)
		{
			if (locThreadState == ThreadState::STOPPED)
			{
				//break;
				return;
			}

			auto now = std::chrono::steady_clock::now();
			static std::vector<char> response;
			char command[512];

			if (now > locNextPollTime)
			{
				if (locIsAuthenticated == false)
				{
					locIsAuthenticated = P4::TrySetClient();
					locNextPollTime = now + std::chrono::seconds(60);
					continue;
				}

 				sprintf_s(command, "%s opened -a \"%s...\"", p4cmd, streamGameAssetsRoot);
				RunCommand(command, response);

				if (HasCommandBufferErrors(response.data()) == false)
				{
					ParseFileInfo(response.data());
				}
				response.clear();

				locNextPollTime = now + std::chrono::seconds(60);
				// TODO : As it is now, it never removes from the cache, so if I checkout a file, it will be marked in the cache, if I revert it it will still be marked in cache.
			}

			bool updateNow = !locPendingAsyncCommands.empty();
			while (!locPendingAsyncCommands.empty())
			{
				RunCommand(locPendingAsyncCommands.front().c_str(), response);
				locPendingAsyncCommands.pop_front();

				// Todo: report results somehow!

				response.clear();
			}

			if (updateNow)
			{
 				sprintf_s(command, "%s opened -a \"%s...\"", p4cmd, streamGameAssetsRoot);
				RunCommand(command, response);

				if (HasCommandBufferErrors(response.data()) == false)
				{
					ParseFileInfo(response.data());
				}
				response.clear();
			}
			std::this_thread::sleep_for(std::chrono::seconds(someUpdateIntervalSeconds));
		}
	});
}

#pragma warning(pop)