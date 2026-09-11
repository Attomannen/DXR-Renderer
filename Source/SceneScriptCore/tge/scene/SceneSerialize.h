#pragma once

namespace Tga
{
	enum class SceneFileChangeType
	{
		Add,
		Delete,
		Modify
	};

	class Scene;
	class SceneObject;
	class SceneObjectDefinitionManager;

	extern void SaveScene(const Scene& scene, void (*afileChangedCallback)(SceneFileChangeType, const char*) = nullptr);
	extern bool LoadScene(const char* filepath, Scene& scene);
}