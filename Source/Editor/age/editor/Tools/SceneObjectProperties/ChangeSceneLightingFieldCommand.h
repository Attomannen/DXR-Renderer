#pragma once

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	// Sun/ambient are plain scalar fields on Scene itself, not SceneObjects
	// (see Scene.h's mySunYaw etc.) -- same one-command-many-fields shape as
	// ChangeSceneObjectLightFieldCommand, but Apply() reaches the active
	// Scene directly instead of looking an object up by id.
	enum class SceneLightingField { SunYaw, SunPitch, SunColor, SunIntensity, AmbientColor };

	class ChangeSceneLightingFieldCommand : public SceneCommandBase
	{
	public:
		// aCount: 1 for the scalar fields, 3 for the two colors.
		ChangeSceneLightingFieldCommand(SceneLightingField aField, const float* aNewValue, const float* aOldValue, int aCount);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;
		const char* GetName() const override { return "Edit Sun / Ambient"; }

	private:
		void Apply(const float* aValue);

		SceneLightingField myField;
		int myCount;
		float myNewValue[3];
		float myOldValue[3];
	};
}
