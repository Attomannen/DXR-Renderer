#include <stdafx.h>
#include "SceneObjectDefinitionManager.h"

#include <fstream>
#include <filesystem>
#include <iostream>

#include <tge/settings/Settings.h>
#include <tge/log/Log.h>

using namespace Tga;

SceneObjectDefinitionManager::SceneObjectDefinitionManager() {}

void SceneObjectDefinitionManager::Init(std::string_view aProjectPath)
{
	for (const auto& entry : std::filesystem::recursive_directory_iterator(aProjectPath))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".tgo") 
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

SceneObjectDefinition* SceneObjectDefinitionManager::Get(StringId name)
{
	auto it = mySceneObjectDefinitions.find(name);
	if (it == mySceneObjectDefinitions.end())
		return nullptr;

	return it->second.get();
}
