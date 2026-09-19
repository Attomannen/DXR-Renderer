#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>

#include <age/editor/CommandManager/CommandManager.h>

#include <age/editor/Scene/ActiveScene.h>

using namespace Ag;

ChangeSceneObjectNameCommand::ChangeSceneObjectNameCommand(uint32_t aObjectId, const std::string_view& aNewName, const std::string_view& aOldName)
	: mySceneObjectId(aObjectId)
	, myNewName(aNewName)
	, myOldName(aOldName)
{

}

void ChangeSceneObjectNameCommand::Execute()
{
	Ag::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetName(myNewName.c_str());
}

void ChangeSceneObjectNameCommand::Undo()
{
	Ag::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetName(myOldName.c_str());
}

void ChangeSceneObjectNameCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}