#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>

#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

ChangeSceneObjectFolderCommand::ChangeSceneObjectFolderCommand(uint32_t aObjectId, const StringId& aNewName, const StringId& aOldName)
	: mySceneObjectId(aObjectId)
	, myNewName(aNewName)
	, myOldName(aOldName)
{

}

void ChangeSceneObjectFolderCommand::Execute()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetPath(GetActiveScene(), myNewName);
}

void ChangeSceneObjectFolderCommand::Undo()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetPath(GetActiveScene(), myOldName);
}

void ChangeSceneObjectFolderCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}