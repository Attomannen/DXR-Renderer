#pragma once

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	// Which light field a command instance edits. Point lights only expose
	// Color/Range/Radius; Inner/OuterAngle are spot-light-only in the
	// inspector but stored unconditionally on SceneObject either way.
	enum class LightField { Color, Range, Radius, InnerAngle, OuterAngle };

	// One command class for every scalar/HDR-color light field instead of one
	// per field: Execute/Undo just re-apply a captured 1- or 3-float value by
	// field tag, so there is nothing field-specific left to duplicate five times.
	class ChangeSceneObjectLightFieldCommand : public SceneCommandBase
	{
	public:
		// aCount: 1 for the scalar fields, 3 for Color. aNewValue/aOldValue must
		// point to at least aCount floats.
		ChangeSceneObjectLightFieldCommand(uint32_t aObjectId, LightField aField,
			const float* aNewValue, const float* aOldValue, int aCount);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;
		const char* GetName() const override { return "Edit Light"; }

	private:
		void Apply(const float* aValue);

		uint32_t mySceneObjectId;
		LightField myField;
		int myCount;
		float myNewValue[3];
		float myOldValue[3];
	};
}
