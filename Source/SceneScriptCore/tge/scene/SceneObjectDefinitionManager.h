#pragma once

#include <filesystem>

#include <tge/stringRegistry/StringRegistry.h>
#include <tge/scene/SceneObjectDefinition.h>

namespace Tga
{
	class SceneObjectDefinitionManager
	{
	public:
		SceneObjectDefinitionManager();
		void Init(std::string_view aProjectPath);

		SceneObjectDefinition* CreateOrGet(const std::filesystem::path& aPath);
		SceneObjectDefinition* Get(StringId name);

	private:
		std::unordered_map<StringId, std::unique_ptr<SceneObjectDefinition>> mySceneObjectDefinitions;
	};
}