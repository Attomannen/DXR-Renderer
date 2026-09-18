#pragma once

#include <string>

#include <tge/editor/Commands/SceneCommandBase.h>
#include <tge/scene/Scene.h>

namespace Tga
{
	// The environment texture path (Scene::myEnvironmentTexturePath) is a
	// string, so it doesn't fit ChangeSceneLightingFieldCommand's float
	// shape -- otherwise the same idea, one small command for the field.
	class ChangeSceneEnvironmentTextureCommand : public SceneCommandBase
	{
	public:
		ChangeSceneEnvironmentTextureCommand(const std::string& aNewPath, const std::string& aOldPath);

		void Execute() override;
		void Undo() override;

		void GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const override;
		const char* GetName() const override { return "Change Environment Texture"; }

	private:
		std::string myNewPath;
		std::string myOldPath;
	};
}
