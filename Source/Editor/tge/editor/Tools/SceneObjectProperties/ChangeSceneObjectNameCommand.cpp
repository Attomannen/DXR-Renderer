#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>

#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

ChangeSceneObjectNameCommand::ChangeSceneObjectNameCommand(uint32_t aObjectId, const std::string_view& aNewName, const std::string_view& aOldName)
	: mySceneObjectId(aObjectId)
	, myNewName(aNewName)
	, myOldName(aOldName)
{

}

void ChangeSceneObjectNameCommand::Execute()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetName(myNewName.c_str());
}

void ChangeSceneObjectNameCommand::Undo()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	object->SetName(myOldName.c_str());
}

void ChangeSceneObjectNameCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}