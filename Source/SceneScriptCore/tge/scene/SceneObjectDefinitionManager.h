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

		// Re-reads a definition from disk after something outside the editor
		// rewrote its file (the FBX converter regenerating a .tgo with its material
		// list). CreateOrGet() deliberately hands back the in-memory copy whenever
		// one exists, so without this a finished conversion is invisible until the
		// editor restarts -- and saving that stale copy would silently overwrite
		// the freshly generated file. Loads it fresh if it was never registered.
		// Returns nullptr if the file can't be read.
		SceneObjectDefinition* Reload(const std::filesystem::path& aPath);

	private:
		std::unordered_map<StringId, std::unique_ptr<SceneObjectDefinition>> mySceneObjectDefinitions;
	};
}