#pragma once

#include <string>

#include <age/editor/Commands/SceneCommandBase.h>
#include <age/scene/Scene.h>

namespace Ag
{
	// The level's Game Mode (Scene::myGameMode): one path, one small command, like the environment texture.
	class ChangeSceneGameModeCommand : public SceneCommandBase
	{
	public:
		ChangeSceneGameModeCommand(const std::string& aNewPath, const std::string& aOldPath);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;
		const char* GetName() const override { return "Change Game Mode"; }

	private:
		std::string myNewPath;
		std::string myOldPath;
	};
}
