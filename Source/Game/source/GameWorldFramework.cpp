#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// The game framework: which Game Mode is in charge, which Player Start the player begins at, and opening levels.

bool GameWorld::Impl::FindPlayerStart(const std::string& tag, Matrix4x4f& transform) const
{
	for (const PlayerStartInfo& start : playerStarts)
	{
		if (tag.empty() || start.tag == tag)
		{
			transform = start.transform;
			return true;
		}
	}
	return false;
}

void GameWorld::Impl::SpawnGameModeEntries(std::vector<SceneEntry>& entries)
{
	playerStarts.clear();
	for (const SceneEntry& entry : entries)
		if (entry.playerStart.has)
			playerStarts.push_back({ entry.playerStart.tag, entry.transform });

	gameSettings = GameSettings();
	gameSettings.Load();

	// The level's own Game Mode wins over the project's.
	const std::string modePath = !levelGameMode.empty() ? levelGameMode : gameSettings.defaultGameMode;
	if (modePath.empty())
		return;

	const fs::path root = Settings::GameAssetRoot();
	std::optional<SceneEntry> mode = LoadTgo(root / modePath);
	if (!mode || !mode->gameMode.has)
	{
		ERROR_PRINT("game mode: '%s' is not a Game Mode (it needs a Game Mode component)", modePath.c_str());
		return;
	}
	INFO_PRINT("game mode: %s", modePath.c_str());

	if (mode->gameMode.spawnPlayer && !mode->gameMode.defaultPawn.empty())
	{
		std::optional<SceneEntry> pawn = LoadTgo(root / mode->gameMode.defaultPawn);
		if (!pawn)
		{
			ERROR_PRINT("game mode: cannot load the default pawn '%s'", mode->gameMode.defaultPawn.c_str());
		}
		else
		{
			Matrix4x4f start;
			if (FindPlayerStart(mode->gameMode.playerStartTag, start))
				pawn->transform = start;
			else
				ERROR_PRINT("game mode: no Player Start%s%s in this level; the pawn starts at the origin",
					mode->gameMode.playerStartTag.empty() ? "" : " tagged ", mode->gameMode.playerStartTag.c_str());
			entries.push_back(std::move(*pawn));
		}
	}
	entries.push_back(std::move(*mode));
}

void GameWorld::Impl::ProcessLevelRequest()
{
	if (pendingLevel.empty() && !restartRequested)
		return;

	std::string level = restartRequested ? currentScene : pendingLevel;
	pendingLevel.clear();
	restartRequested = false;
	std::replace(level.begin(), level.end(), '\\', '/');
	if (level.size() > 4 && level.compare(level.size() - 4, 4, ".tgs") == 0)
		level.resize(level.size() - 4);

	INFO_PRINT("opening level '%s'", level.c_str());
	LoadSceneContent(level, false);
	frame = 0;
}
