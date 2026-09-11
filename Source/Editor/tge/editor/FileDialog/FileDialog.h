#pragma once

#include <functional>
#include <string>

namespace FileDialog {
	typedef std::function<void(const char*)> Callback;

	enum class FileType
	{
		na,
		tgs,
		tgo,
		tgac,
		tgmat,
		Count
	};

	extern void OpenFile(Callback callback);
	extern void SaveFile(FileType aFileType = FileType::na, Callback callback = [](const char*) {});
	extern void OpenProjectFolder(Callback callback);

}