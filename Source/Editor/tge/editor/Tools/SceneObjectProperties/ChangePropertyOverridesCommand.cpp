#include <tge/editor/Tools/SceneObjectProperties/ChangePropertyOverridesCommand.h>

#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

ChangePropertyOverridesCommand::ChangePropertyOverridesCommand(uint32_t aObjectId, const SceneProperty& aNewPropertyDefinition, const SceneProperty& aOldPropertyDefinition)
	: mySceneObjectId(aObjectId)
	, myNewValue(aNewPropertyDefinition)
	, myOldValue(aOldPropertyDefinition)
{

}

void ChangePropertyOverridesCommand::Execute()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	std::vector<SceneProperty>& properties = object->EditPropertyOverrides();
	
	if (myNewValue.name.IsEmpty())
	{
		for (int i = 0; i < properties.size(); i++)
		{
			if (properties[i].name == myOldValue.name)
			{
				properties.erase(properties.begin() + i);
				return;
			}
		}	
	}

	for (SceneProperty& property : properties)
	{
		if (property.name == myNewValue.name)
		{
			property = myNewValue;
			return;
		}
	}

	properties.push_back(myNewValue);
}

void ChangePropertyOverridesCommand::Undo()
{
	Tga::SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);

	std::vector<SceneProperty>& properties = object->EditPropertyOverrides();

	if (myOldValue.name.IsEmpty())
	{
		for (int i = 0; i < properties.size(); i++)
		{
			if (properties[i].name == myNewValue.name)
			{
				properties.erase(properties.begin() + i);
				return;
			}
		}
	}

	for (SceneProperty& property : properties)
	{
		if (property.name == myOldValue.name)
		{
			property = myNewValue;
			return;
		}
	}

	properties.push_back(myOldValue);
}

void ChangePropertyOverridesCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}