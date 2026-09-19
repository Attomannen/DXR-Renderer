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
			pawn->name = "Player";
			pawn->isPlayerPawn = true;
			entries.push_back(std::move(*pawn));
		}
	}
	mode->name = "GameMode";
	entries.push_back(std::move(*mode));
}

size_t GameWorld::Impl::AddSceneInstance(const SceneEntry& entry, int modelIndex, const Matrix4x4f& transform)
{
	SceneInstance instance;
	instance.model = modelIndex;
	instance.transform = transform;
	instance.name = entry.name;
	instance.definition = fs::path(entry.tgoPath).filename().string();
	sceneInstances.push_back(std::move(instance));
	const size_t index = sceneInstances.size() - 1;
	if (entry.isPlayerPawn)
		playerPawn = (int)index;
	return index;
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

int GameWorld::Impl::RequestSpawn(const std::string& definition, const Matrix4x4f& transform, const std::string& name)
{
	if (definition.empty())
		return -1;
	std::string relative = definition;
	std::replace(relative.begin(), relative.end(), '\\', '/');
	if (fs::path(relative).extension() != ".tgo")
		relative += ".tgo";

	std::optional<SceneEntry> entry = LoadTgo(fs::path(Settings::GameAssetRoot()) / relative);
	if (!entry)
	{
		ERROR_PRINT("spawn: cannot read '%s'", relative.c_str());
		return -1;
	}
	entry->transform = transform;
	entry->name = name;

	// Every request adds exactly one object, so its handle is known before it exists.
	const int handle = (int)(sceneInstances.size() + pendingSpawns.size());
	pendingSpawns.push_back(std::move(*entry));
	return handle;
}

void GameWorld::Impl::ProcessSpawnRequests()
{
	if (!pendingSpawns.empty())
	{
		std::vector<SceneEntry> spawns = std::move(pendingSpawns);
		pendingSpawns.clear();
		for (SceneEntry& entry : spawns)
		{
			const size_t before = sceneInstances.size();
			if (!InstantiateEntry(entry, false))
			{
				// Keep the handle the script was given: the object exists, without its mesh.
				entry.fbx.clear();
				InstantiateEntry(entry, false);
			}
			if (sceneInstances.size() > before)
				INFO_PRINT("spawn: '%s' is object %zu", entry.tgoPath.c_str(), before);
		}
		if (physics.IsInitialized())
			physics.OptimizeBroadPhase();
	}

	if (!pendingDestroys.empty())
	{
		std::vector<size_t> destroys = std::move(pendingDestroys);
		pendingDestroys.clear();
		for (const size_t index : destroys)
			DestroyInstance(index);
	}
}

void GameWorld::Impl::DestroyInstance(size_t index)
{
	if (index >= sceneInstances.size() || !sceneInstances[index].alive)
		return;
	SceneInstance& instance = sceneInstances[index];
	instance.alive = false;
	instance.name.clear();
	instance.definition.clear();
	if (playerPawn == (int)index)
		playerPawn = -1;

	std::erase_if(sceneScripts, [index](const SceneScriptObject& object) { return object.instance == index; });
	std::erase_if(sceneParticles, [index](const SceneParticleObject& object) { return object.instance == index; });

	for (size_t i = 0; i < scenePhysicsObjects.size();)
	{
		if (scenePhysicsObjects[i].instance != index)
		{
			++i;
			continue;
		}
		if (scenePhysicsObjects[i].body.IsValid())
			physics.DestroyBody(scenePhysicsObjects[i].body);
		if (!scenePhysicsObjects[i].dynamic)
			--scenePhysicsStaticCount;
		scenePhysicsObjects.erase(scenePhysicsObjects.begin() + (ptrdiff_t)i);
	}
	for (size_t i = 0; i < sceneCharacters.size();)
	{
		if (sceneCharacters[i].instance != index)
		{
			++i;
			continue;
		}
		if (sceneCharacters[i].id.IsValid())
			physics.DestroyCharacter(sceneCharacters[i].id);
		sceneCharacters.erase(sceneCharacters.begin() + (ptrdiff_t)i);
	}
	for (int i = 0; i < (int)sceneCameras.size();)
	{
		if (sceneCameras[(size_t)i].instance != index)
		{
			++i;
			continue;
		}
		if (activeSceneCamera == i)
			SetSceneCameraActive(-1);
		else if (activeSceneCamera > i)
			--activeSceneCamera;
		sceneCameras.erase(sceneCameras.begin() + i);
	}

	// The mesh goes; the meshes after it move up one place.
	if (instance.model >= 0)
	{
		const size_t slot = (size_t)instance.model;
		models.erase(models.begin() + (ptrdiff_t)slot);
		opaqueMeshes.erase(opaqueMeshes.begin() + (ptrdiff_t)slot);
		transparentMeshes.erase(transparentMeshes.begin() + (ptrdiff_t)slot);
		if (slot < instanceOffsets.size())
			instanceOffsets.erase(instanceOffsets.begin() + (ptrdiff_t)slot);
		for (SceneInstance& other : sceneInstances)
			if (other.model > (int)slot)
				--other.model;
		instance.model = -1;
	}
}
