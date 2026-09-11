#include <tge/editor/Scene/EditorSceneManager.h>

#include <fstream>
#include <tge/settings/settings.h>
#include <tge/scene/SceneSerialize.h>

using namespace Tga;

EditorSceneManager::EditorSceneManager() {}

Scene* EditorSceneManager::Get(const std::filesystem::path& aPath)
{
	auto it = myScenes.find(aPath);
	if (it == myScenes.end())
	{
		FilePathStream resolvedTgsPath;
		resolvedTgsPath << Tga::Settings::GameAssetRoot() << "/" << aPath.string();
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