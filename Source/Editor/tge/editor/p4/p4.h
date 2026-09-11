#pragma once

#include <tge/stringRegistry/StringRegistry.h>

namespace Tga {
	class Scene;
}
namespace P4 {

	enum class ErrorType { None, Generic, LoginRequired, Password, FileNotFound, Workspace };
	enum class FileAction { None, Add, Edit, Delete, Error };

	constexpr std::string_view FileActionString(FileAction value) {
		switch (value) {
		case FileAction::Add: return "add";
		case FileAction::Edit: return "edit";
		case FileAction::Delete: return "delete";
		default: return "Unknown";
		}
	}

	struct FileInfo
	{
		char depotPath[256];
		char client[128];
		char user[64];
		char hasError[32] = "";
		char changelist[32];
		char fileType[16];
		FileAction action = FileAction::None;
		int revision;
		size_t cachedLine;
		bool inUse = false;
		//size_t timestamp;
		// @tood: cache timestamp interesting?
	};

	typedef void (*AuthenticationCallback)();
	typedef void (*FileInfoCallback)(const FileInfo);

	ErrorType QueryErrorState();
	const char *GetErrorString();

	bool TrySetClient();
	void CheckoutFile(std::string_view aPath);
	void MarkFileForAdd(std::string_view aPath);
	void MarkFileForDelete(std::string_view aPath);

	bool QueryHasFileInfo(std::string_view path);
	const FileInfo GetFileInfo(std::string_view path);

	const char* MyUser();
	const char* MyClient();

	void StartPolling(const char* aFolder, const unsigned int someUpdateIntervalSeconds = 1, AuthenticationCallback requestAuthCallback = nullptr);
	void StopPolling();


}