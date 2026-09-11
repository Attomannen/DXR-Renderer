#include <stdafx.h>
#include <memory>
#include "Scene.h"
#include <filesystem>
#include <tge/script/Script.h>
#include <tge/script/ScriptManager.h>
#include <tge/script/ScriptRuntimeInstance.h>
#include <tge/script/Contexts/ScriptUpdateContext.h>

using namespace Tga;

void Scene::DeleteSceneObject(uint32_t anId)
{
	auto it = myFolderCounts.find(myObjects.find(anId)->second->GetPath());
	it->second--;
	if (it->second == 0)
		myFolderCounts.erase(it);

	myObjects.erase(anId);
}

void Scene::AddSceneObject(uint32_t anId, std::shared_ptr<SceneObject> anObject)
{
	myFolderCounts[anObject->GetPath()]++;
	myObjects.insert({ anId, anObject });
}

Scene::Scene() : myObjects() 
{
	myName = "untitled";
}

Scene::~Scene() 
{
}

void Scene::UpdateFolderCounts(StringId aOldFolder, StringId aNewFolder)
{
	myFolderCounts[aNewFolder]++;

	auto it = myFolderCounts.find(aOldFolder);
	it->second--;
	if (it->second == 0)
		myFolderCounts.erase(it);
}

void Scene::GetAllFolderNames(std::vector<StringId>& outFolderNames)
{
	for (const auto& pair : myFolderCounts)
	{
		outFolderNames.push_back(pair.first);
	}
}

StringId Scene::GetObjectFilePath(uint32_t anId) const
{
	auto it = myObjectPathCache.find(anId);
	if (it != myObjectPathCache.end())
		return it->second;

	const char* uuid = UUIDManager::GetUUIDStringFromID(anId);
	std::filesystem::path path = GetPath();
	path = path.replace_extension(".leveldata") / uuid;

	UUIDManager::DeallocateUUIDString(uuid);

	myObjectPathCache[anId] = StringRegistry::RegisterOrGetString(path.string().c_str());

	return myObjectPathCache[anId];
}
