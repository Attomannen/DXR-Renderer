#pragma once

#include <unordered_map>
#include <chrono>
#include <memory>
#include <age/stringRegistry/StringRegistry.h>
#include <age/math/Matrix4x4.h>

#include "age/animation/Pose.h"
#include <vector>
#include <age/rhi/Descs.h>

#include "age/texture/Texture.h"

namespace Ag
{
	class Model;
	class EditorViewport;

	class Scene;
	class SceneObject;
	struct ScenePropertyDefinition;
	class ModelShader;
	class Camera;
	struct Frustum;



	struct MaterialAsset;

	class SceneCache
	{
		std::unordered_map<StringId, Texture*> myTextureCache;
		std::unordered_map<StringId, std::shared_ptr<Model>> myModelCache;
		std::unordered_map<StringId, Scene*> mySceneCache;
		// Materials are re-read for every mesh of every instance each frame;
		// without this the editor does one file read per mesh per frame.
		std::unordered_map<StringId, std::shared_ptr<MaterialAsset>> myMaterialCache;

	public:
		Texture* GetTextureUsingCache(StringId path, TextureSrgbMode srgbMode);
		std::shared_ptr<Model> GetModelUsingCache(StringId path);
		Scene* GetSceneUsingCache(StringId path);
		// Null when the asset cannot be loaded. Cached until the next clear.
		const MaterialAsset* GetMaterialUsingCache(StringId path);

		void ClearCache();
		// Drops the caches at most every aMinIntervalSeconds, so edited assets
		// still appear while the editor is running without re-reading every
		// model, texture and material from disk every frame.
		void ClearCacheThrottled(float aMinIntervalSeconds = 0.5f);

	private:
		std::chrono::steady_clock::time_point myLastClear{};
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

		// Which sub-meshes a static model draws: everything, only opaque /
		// masked meshes (G-buffer, shadows) or only transparent ones (glass).
		enum class MeshPass { All, Opaque, Transparent };
		MeshPass meshPass = MeshPass::All;
		// Locators and selection bounds belong to the editor overlay, not to
		// the lit scene's extra passes.
		bool drawHelpers = true;

		// When set, static models are collected as ray-tracing instances for
		// the TLAS instead of being drawn. One traversal, no draw calls.
		std::vector<rhi::RaytracingInstanceDesc>* rayInstances = nullptr;
	};

	void SetupIdPass();
	void DrawOutlines(const EditorViewport& viewport);
	void SetObjectAndSelectionId(uint32_t anObjectId, uint32_t aSelectionId);

	bool DrawSceneProperty(const ScenePropertyDefinition& property, float maxScale, DrawParameters& drawParameters);
	void DrawSceneObject(const SceneObject& sceneObject, DrawParameters& drawParameters);
	void DrawScene(const Scene& scene, DrawParameters& drawParameters);

	bool CheckBounds(const Frustum& frustum, Ag::Matrix4x4f matrix, float maxScale, Model& model);

}