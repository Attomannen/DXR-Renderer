#pragma once

#include <functional>
#include <string>

namespace FileDialog {
	typedef std::function<void(const char*)> Callback;

	// Shows the system folder picker and calls back with the chosen folder (ending in a backslash).
	extern void OpenProjectFolder(Callback callback);
}
