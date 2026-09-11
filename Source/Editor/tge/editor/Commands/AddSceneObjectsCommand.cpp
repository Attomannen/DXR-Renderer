#include <tge/editor/Commands/AddSceneObjectsCommand.h>

#include <tge/Math/Matrix4x4.h>
#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/editor/Scene/ActiveScene.h>
#include <tge/editor/Scene/SceneSelection.h>

using namespace Tga;

void AddSceneObjectsCommand::AddObjects(std::span<std::shared_ptr<SceneObject>> someObjects)
{
	for (int i = 0; i < someObjects.size(); i++)
	{
		myObjects.push_back(std::pair<uint32_t, std::shared_ptr<SceneObject>>(0, someObjects[i]));
	}
}

void AddSceneObjectsCommand::Execute()
{
	for (auto& p : myObjects)
	{
		if (p.first == 0)
			p.first = UUIDManager::CreateUUID();

		GetActiveScene()->AddSceneObject(p.first, p.second);
	}
}

void AddSceneObjectsCommand::Undo()
{
	for (auto& p : myObjects)
	{
		GetActiveScene()->DeleteSceneObject(p.first);
	}
}

std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>> AddSceneObjectsCommand::GetObjects() const
{
	return myObjects;
}

void AddSceneObjectsCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	for (auto& p : myObjects)
	{
		outModifiedObjects.push_back(p.first);
	}

	outHasModifedSceneFile = false;
}

