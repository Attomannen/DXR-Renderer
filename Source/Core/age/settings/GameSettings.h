#pragma once

#include <string>

namespace Ag
{
	// The project's game settings, like Unreal's Project Settings -> Maps & Modes. One file for the whole project,
	// Game.tgsettings, at the root of the game assets. The editor edits it; the game reads it at startup.
	struct GameSettings
	{
		std::string gameName = "My Game";
		// The level the game opens when nothing else picks one, e.g. "Scenes/Main" (a .tgs, without the extension).
		std::string defaultLevel;
		// The Game Mode used by every level that does not name its own: a .tgo with a Game Mode component,
		// e.g. "Game/MyGameMode.tgo".
		std::string defaultGameMode;

		static std::string FilePath();
		bool Load();   // false when there is no settings file yet; the defaults stay
		bool Save() const;
	};
}
