#include "stdafx.h"
#include <age/filewatcher/FileWatcher.h>
#define WIN32_LEAN_AND_MEAN 
#define NOMINMAX 
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <age/application.h>

using namespace Ag;
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
	// One syscall, not two: the error code covers "missing" as well as exists()
	// did, and this runs for every watched file on every sweep.
	std::error_code ec;
	const auto stamp = std::filesystem::last_write_time(aFilePath, ec);
	if (ec) return 0;
	return stamp.time_since_epoch().count();
}


void FileWatcher::UpdateChanges()
{
	while (!myShouldEndThread)
	{
		{
			// myMutex guards only the change queue that FlushChanges drains --
			// OnFileChange takes it for the push. Holding it across the whole
			// sweep meant the main thread's FlushChanges blocked for the length
			// of a stat of every watched file: measured at 4.8 ms per frame
			// average and 21 ms peak in the editor, for work that is almost
			// always "nothing changed".
			std::lock_guard<std::mutex> folderGuard(myAddNewFolderMutex);
			for (auto& iter : myThreadedFilesToWatch)
			{
				CheckFileChanges(iter.first, iter.second);
			}
		}

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
	std::lock_guard<std::mutex> guard(myMutex);
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

