#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>

#include <age/editor/CommandManager/CommandManager.h>

#include <age/editor/Scene/ActiveScene.h>

using namespace Ag;

ChangeSceneObjectFolderCommand::ChangeSceneObjectFolderCommand(uint32_t aObjectId, const StringId& aNewName, const StringId& aOldName)
	: mySceneObjectId(aObjectId)
	, myNewName(aNewName)
	, myOldName(aOldName)
{

}

void ChangeSceneObjectFolderCommand::Execute()
{
	Ag::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetPath(GetActiveScene(), myNewName);
}

void ChangeSceneObjectFolderCommand::Undo()
{
	Ag::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetPath(GetActiveScene(), myOldName);
}

void ChangeSceneObjectFolderCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}