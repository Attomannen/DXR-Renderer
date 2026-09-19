#include <age/editor/Scene/EditorSceneManager.h>

#include <fstream>
#include <age/settings/settings.h>
#include <age/scene/SceneSerialize.h>

using namespace Ag;

EditorSceneManager::EditorSceneManager() {}

Scene* EditorSceneManager::Get(const std::filesystem::path& aPath)
{
	auto it = myScenes.find(aPath);
	if (it == myScenes.end())
	{
		FilePathStream resolvedTgsPath;
		resolvedTgsPath << Ag::Settings::GameAssetRoot() << "/" << aPath.string();
		resolvedTgsPath.NormalizePath();
		if (!fs::exists(resolvedTgsPath.GetData()))
			return nullptr;

		std::unique_ptr<Scene> scene = std::make_unique<Scene>();

		std::string pathString = aPath.string();
		LoadScene(pathString.c_str(), *scene);

		myScenes[aPath] = std::move(scene);
	}

	return myScenes[aPath].get();
}