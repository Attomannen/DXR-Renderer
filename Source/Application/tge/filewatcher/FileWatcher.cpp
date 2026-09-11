#include "stdafx.h"
#include <tge/filewatcher/FileWatcher.h>
#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX 
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <tge/application.h>

using namespace Tga;
namespace fs = std::filesystem;
FileWatcher::FileWatcher()
	: myThread(nullptr)
	, myShouldEndThread(false)
	, myThreadIsDone(false)

{
}


FileWatcher::~FileWatcher()
{
	myShouldEndThread = true;
	if (myThread)
	{
		while (!myThreadIsDone)
		{
			std::this_thread::sleep_for(std::chrono::nanoseconds(1));
		}
		myThread->join();
		delete myThread;
	}

}


void FileWatcher::FlushChanges()
{
	if (!Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Filewatcher) || !myThread)
	{
		return;
	}
	std::lock_guard<std::mutex> guard(myMutex);

	myFileChanged.swap(myFileChangedThreaded);

	for (fs::path& theString : myFileChanged)
	{
		std::string comparrableStringFromPath = theString.string();
		std::replace(comparrableStringFromPath.begin(), comparrableStringFromPath.end(), '\\', '#');
		std::replace(comparrableStringFromPath.begin(), comparrableStringFromPath.end(), '/', '#');
		std::vector<callback_function_file> callbacks = myCallbacks[comparrableStringFromPath];
		for (unsigned int i = 0; i < callbacks.size(); i++)
		{
			if (callbacks[i])
			{
				callbacks[i]();
			}
		}
	}

	myFileChanged.clear();
}

long long GetFileTimeStamp(const fs::path& aFilePath)
{
	if (std::filesystem::exists(aFilePath) == false)
	{
		return 0;
	}
	return std::filesystem::last_write_time(aFilePath).time_since_epoch().count();
}


void FileWatcher::UpdateChanges()
{
	while (!myShouldEndThread)
	{
		myMutex.lock();
		myAddNewFolderMutex.lock();

		for (auto& iter : myThreadedFilesToWatch)
		{
			CheckFileChanges(iter.first, iter.second);
		}
		
		myMutex.unlock();
		myAddNewFolderMutex.unlock();
	
		std::this_thread::sleep_for(std::chrono::milliseconds(32));
	}
	myThreadIsDone = true;
}

void FileWatcher::CheckFileChanges(const fs::path& aFile, long long aTimeStampLastChanged)
{
	long long latestTimeStamp = GetFileTimeStamp(aFile);
	if (latestTimeStamp != aTimeStampLastChanged)
	{
		OnFileChange(aFile);
		myThreadedFilesToWatch[aFile] = latestTimeStamp;
	}
}

void FileWatcher::OnFileChange(const fs::path& aFile)
{
	for (unsigned int i = 0; i < myFileChangedThreaded.size(); i++)
	{
		if (myFileChangedThreaded[i].compare(aFile) == 0)
		{
			return;
		}
	}

	myFileChangedThreaded.push_back(aFile);

}
bool CheckIsFileExits(const char* aFilePath)
{
	return std::filesystem::exists(aFilePath);
}


bool FileWatcher::WatchFileChange(std::string_view aFile, callback_function_file aFunctionToCallOnChange)
{
	if (!Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Filewatcher))
	{
		return false;
	}

	if (!CheckIsFileExits(aFile.data()))
	{
		return false;
	}

	std::string comparableStringFromPath = std::string(aFile);
	std::replace(comparableStringFromPath.begin(), comparableStringFromPath.end(), '\\', '#');
	std::replace(comparableStringFromPath.begin(), comparableStringFromPath.end(), '/', '#');

	myCallbacks[comparableStringFromPath].push_back(aFunctionToCallOnChange);

	long long timeStampChanged = GetFileTimeStamp(aFile.data());
	if (myThread)
	{
		myAddNewFolderMutex.lock();
		myThreadedFilesToWatch[aFile] = timeStampChanged;
		myAddNewFolderMutex.unlock();
	}
	else
	{
		myThreadedFilesToWatch[aFile] = timeStampChanged;
		myThread = new std::thread(&FileWatcher::UpdateChanges, this);
	}

	return true;
}

