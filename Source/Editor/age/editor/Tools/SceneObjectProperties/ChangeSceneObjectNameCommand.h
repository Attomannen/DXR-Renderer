#pragma once

#include <span>

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	class ChangeSceneObjectNameCommand : public SceneCommandBase
	{
	public:
		ChangeSceneObjectNameCommand(uint32_t aObjectId, const std::string_view& aNewName, const std::string_view& aOldName);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;

	private:
		uint32_t mySceneObjectId;
		std::string myOldName;
		std::string myNewName;
	};

}
