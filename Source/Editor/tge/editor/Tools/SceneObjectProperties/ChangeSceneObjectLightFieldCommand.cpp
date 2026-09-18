#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectLightFieldCommand.h>

#include <cstring>

#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

ChangeSceneObjectLightFieldCommand::ChangeSceneObjectLightFieldCommand(uint32_t aObjectId, LightField aField,
	const float* aNewValue, const float* aOldValue, int aCount)
	: mySceneObjectId(aObjectId)
	, myField(aField)
	, myCount(aCount)
{
	std::memcpy(myNewValue, aNewValue, sizeof(float) * aCount);
	std::memcpy(myOldValue, aOldValue, sizeof(float) * aCount);
}

void ChangeSceneObjectLightFieldCommand::Apply(const float* aValue)
{
	SceneObject* object = GetActiveScene()->GetSceneObject(mySceneObjectId);
	if (!object) return;

	switch (myField)
	{
	case LightField::Color:      std::memcpy(object->GetLightColor(), aValue, sizeof(float) * 3); break;
	case LightField::Range:      object->GetLightRange() = aValue[0]; break;
	case LightField::Radius:     object->GetLightRadius() = aValue[0]; break;
	case LightField::InnerAngle: object->GetLightInnerAngle() = aValue[0]; break;
	case LightField::OuterAngle: object->GetLightOuterAngle() = aValue[0]; break;
	}
}

void ChangeSceneObjectLightFieldCommand::Execute()
{
	Apply(myNewValue);
}

void ChangeSceneObjectLightFieldCommand::Undo()
{
	Apply(myOldValue);
}

void ChangeSceneObjectLightFieldCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	outModifiedObjects.push_back(mySceneObjectId);
	outHasModifedSceneFile = false;
}
