#pragma once

#include <unordered_map>
#include <filesystem>

#include <age/stringRegistry/StringRegistry.h>
#include <age/scene/Scene.h>

namespace Ag
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