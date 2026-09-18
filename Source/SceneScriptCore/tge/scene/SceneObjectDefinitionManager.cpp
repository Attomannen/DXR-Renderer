#include <stdafx.h>
#include "SceneObjectDefinitionManager.h"
#include <algorithm>

#include <fstream>
#include <filesystem>
#include <iostream>

#include <tge/settings/Settings.h>
#include <tge/log/Log.h>

using namespace Tga;

// Deleted assets are moved into "<asset root>/.trash" (so a delete can be undone). They
// are not part of the project and must not be found by a scan of it.
static bool IsInAssetTrash(const std::filesystem::path& path)
{
	for (const auto& part : path)
		if (part == ".trash") return true;
	return false;
}

SceneObjectDefinitionManager::SceneObjectDefinitionManager() {}

void SceneObjectDefinitionManager::Init(std::string_view aProjectPath)
{
	for (const auto& entry : std::filesystem::recursive_directory_iterator(aProjectPath))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".tgo" && !IsInAssetTrash(entry.path())) 
		{
			std::unique_ptr<SceneObjectDefinition> definition = std::make_unique<SceneObjectDefinition>();

			std::string path = std::filesystem::relative(entry.path(), Tga::Settings::GameAssetRoot()).string();
			definition->Load(path.c_str());

			if (mySceneObjectDefinitions.find(definition->GetName()) != mySceneObjectDefinitions.end())
			{
				ERROR_PRINT("Multiple tgo files exist with same name. That is not allowed: %s", definition->GetName().GetString());
			}
			else
			{
				mySceneObjectDefinitions[definition->GetName()] = std::move(definition);
			}
		}
	}
}

SceneObjectDefinition* SceneObjectDefinitionManager::CreateOrGet(const std::filesystem::path& aPath)
{
	std::unique_ptr<SceneObjectDefinition> objectDefinition = std::make_unique<SceneObjectDefinition>();

	std::string name = aPath.stem().string();
	StringId nameId = StringRegistry::RegisterOrGetString(name.c_str());

	auto it = mySceneObjectDefinitions.find(nameId);
	if (it != mySceneObjectDefinitions.end())
		return it->second.get();

	std::string pathString = aPath.string().c_str();
	// A cooker can generate a TGO after the initial project scan.  Do not
	// replace that authored/generated file with an empty definition when it is
	// first opened by the editor; load it into the registry instead.
	std::filesystem::path fullPath = std::filesystem::path(Tga::Settings::GameAssetRoot()) / aPath;
	if (std::filesystem::exists(fullPath))
	{
		try
		{
			objectDefinition->Load(pathString.c_str());
			mySceneObjectDefinitions[nameId] = std::move(objectDefinition);
			return mySceneObjectDefinitions[nameId].get();
		}
		catch (const std::exception& e)
		{
			ERROR_PRINT("Could not load object definition '%s': %s", pathString.c_str(), e.what());
			return nullptr;
		}
	}

	objectDefinition->SetName(nameId);
	objectDefinition->SetPath(pathString.c_str());
	objectDefinition->Save();

	mySceneObjectDefinitions[nameId] = std::move(objectDefinition);
	return mySceneObjectDefinitions[nameId].get();
}

SceneObjectDefinition* SceneObjectDefinitionManager::Reload(const std::filesystem::path& aPath)
{
	const std::filesystem::path fullPath = std::filesystem::path(Tga::Settings::GameAssetRoot()) / aPath;
	if (!std::filesystem::exists(fullPath))
		return nullptr;

	const StringId nameId = StringRegistry::RegisterOrGetString(aPath.stem().string().c_str());
	const std::string pathString = aPath.string();

	try
	{
		// Load() appends to the property list rather than replacing it, so read
		// into a scratch definition first (a malformed file must not leave the
		// registered one half-cleared) and then swap the result in.
		auto fresh = std::make_unique<SceneObjectDefinition>();
		fresh->Load(pathString.c_str());

		auto it = mySceneObjectDefinitions.find(nameId);
		if (it == mySceneObjectDefinitions.end())
		{
			mySceneObjectDefinitions[nameId] = std::move(fresh);
			return mySceneObjectDefinitions[nameId].get();
		}

		// Keep the existing object's address: scene objects and open documents
		// hold pointers to it.
		it->second->EditProperties() = std::move(fresh->EditProperties());
		return it->second.get();
	}
	catch (const std::exception& e)
	{
		ERROR_PRINT("Could not reload object definition '%s': %s", pathString.c_str(), e.what());
		return nullptr;
	}
}

SceneObjectDefinition* SceneObjectDefinitionManager::Get(StringId name)
{
	auto it = mySceneObjectDefinitions.find(name);
	if (it == mySceneObjectDefinitions.end())
		return nullptr;

	return it->second.get();
}

std::vector<SceneObjectDefinition*> SceneObjectDefinitionManager::GetAll() const
{
	std::vector<SceneObjectDefinition*> all;
	for (const auto& entry : mySceneObjectDefinitions)
		all.push_back(entry.second.get());
	std::sort(all.begin(), all.end(), [](const SceneObjectDefinition* a, const SceneObjectDefinition* b)
	{
		return std::string_view(a->GetName().GetString()) < std::string_view(b->GetName().GetString());
	});
	return all;
}
