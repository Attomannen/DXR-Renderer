#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneLightingFieldCommand.h>

#include <cstring>

#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

ChangeSceneLightingFieldCommand::ChangeSceneLightingFieldCommand(SceneLightingField aField,
	const float* aNewValue, const float* aOldValue, int aCount)
	: myField(aField)
	, myCount(aCount)
{
	std::memcpy(myNewValue, aNewValue, sizeof(float) * aCount);
	std::memcpy(myOldValue, aOldValue, sizeof(float) * aCount);
}

void ChangeSceneLightingFieldCommand::Apply(const float* aValue)
{
	Scene* scene = GetActiveScene();
	if (!scene) return;

	switch (myField)
	{
	case SceneLightingField::SunYaw:       scene->SetSunYaw(aValue[0]); break;
	case SceneLightingField::SunPitch:     scene->SetSunPitch(aValue[0]); break;
	case SceneLightingField::SunColor:     std::memcpy(scene->GetSunColor(), aValue, sizeof(float) * 3); break;
	case SceneLightingField::SunIntensity: scene->SetSunIntensity(aValue[0]); break;
	case SceneLightingField::AmbientColor: std::memcpy(scene->GetAmbientColor(), aValue, sizeof(float) * 3); break;
	}
}

void ChangeSceneLightingFieldCommand::Execute()
{
	Apply(myNewValue);
}

void ChangeSceneLightingFieldCommand::Undo()
{
	Apply(myOldValue);
}

void ChangeSceneLightingFieldCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	// Sun/ambient live in the .tgs itself, not a per-object leveldata file --
	// the opposite of every other command in this directory, which touches
	// outModifiedObjects and leaves this false.
	(void)outModifiedObjects;
	outHasModifedSceneFile = true;
}
