#pragma once

#include <unordered_map>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/math/Matrix4x4.h>

#include "tge/animation/Pose.h"

#include "tge/texture/Texture.h"

namespace P4
{
	struct FileInfo;
}

namespace Tga
{
	class Model;
	class EditorViewport;

	class Scene;
	class SceneObject;
	struct ScenePropertyDefinition;
	class ModelShader;
	class Camera;
	struct Frustum;



	class SceneCache
	{
		std::unordered_map<StringId, Texture*> myTextureCache;
		std::unordered_map<StringId, std::shared_ptr<Model>> myModelCache;
		std::unordered_map<StringId, Scene*> mySceneCache;

	public:
		Texture* GetTextureUsingCache(StringId path, TextureSrgbMode srgbMode);
		std::shared_ptr<Model> GetModelUsingCache(StringId path);
		Scene* GetSceneUsingCache(StringId path);

		void ClearCache();
	};

	struct DrawParameters
	{
		bool useIdShader;
		bool drawBounds;
		Vector3f boundsColor;
		SceneCache& cache;
		Frustum& frustum;
		EditorViewport& viewport;
		ModelShader* overrideModelShader;

		std::unordered_map<StringId, ModelSpacePose>* previewPoses;
	};

	void SetupIdPass();
	void DrawOutlines(const EditorViewport& viewport);
	void SetObjectAndSelectionId(uint32_t anObjectId, uint32_t aSelectionId, const P4::FileInfo& someInfo);

	bool DrawSceneProperty(const ScenePropertyDefinition& property, float maxScale, DrawParameters& drawParameters);
	void DrawSceneObject(const SceneObject& sceneObject, DrawParameters& drawParameters);
	void DrawScene(const Scene& scene, DrawParameters& drawParameters);

	bool CheckBounds(const Frustum& frustum, Tga::Matrix4x4f matrix, float maxScale, Model& model);

}