#pragma once

#include <span>

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	class ChangeSceneObjectFolderCommand : public SceneCommandBase
	{
	public:
		ChangeSceneObjectFolderCommand(uint32_t aObjectId, const StringId& aNewName, const StringId& aOldName);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;

	private:
		uint32_t mySceneObjectId;
		StringId myOldName;
		StringId myNewName;
	};
}
