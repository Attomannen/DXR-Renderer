#include <age/editor/Commands/RemoveSceneObjectsCommand.h>

#include <age/Math/Matrix4x4.h>
#include <age/editor/CommandManager/CommandManager.h>

#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Scene/ActiveScene.h>

using namespace Ag;

void RemoveSceneObjectsCommand::AddObjects(std::span<const uint32_t> someObjectIds)
{
	for (int i = 0; i < someObjectIds.size(); i++)
	{
		myObjects.push_back(std::pair<uint32_t, std::shared_ptr<SceneObject>>(someObjectIds[i], nullptr));
	}
}

void RemoveSceneObjectsCommand::Execute()
{
	for (auto& p : myObjects)
	{
		if (p.second == nullptr)
		{
			p.second = GetActiveScene()->GetSceneObjectSharedPtr(p.first);
		}
		if (p.second)
			GetActiveScene()->DeleteSceneObject(p.first);
	}

	// CommandManager snapshots selection after Execute(). Clearing here keeps
	// subsequent hierarchy/properties frames from using deleted IDs; undo
	// restores the selection state that existed before this command.
	if (SceneSelection::GetActiveSceneSelection())
		SceneSelection::GetActiveSceneSelection()->ClearSelection();
}

void RemoveSceneObjectsCommand::Undo()
{
	for (auto& p : myObjects)
	{
		if (p.second)
			GetActiveScene()->AddSceneObject(p.first, p.second);
	}
}

std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>> RemoveSceneObjectsCommand::GetObjects() const
{
	return myObjects;
}

void RemoveSceneObjectsCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	for (auto& p : myObjects)
	{
		outModifiedObjects.push_back(p.first);
	}

	outHasModifedSceneFile = false;
}
