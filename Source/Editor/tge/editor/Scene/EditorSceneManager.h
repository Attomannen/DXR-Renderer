#pragma once

#include <unordered_map>
#include <filesystem>

#include <tge/stringRegistry/StringRegistry.h>
#include <tge/scene/Scene.h>

namespace Tga
{
	class EditorSceneManager
	{
	public:
		EditorSceneManager();

		Scene* Get(const std::filesystem::path& aPath);

	private:
		std::unordered_map<std::filesystem::path, std::unique_ptr<Scene>> myScenes;
	};
}