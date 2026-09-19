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
		//
		// A material miss actually parses the .tgmat off disk, unlike a texture
		// or model miss (those fall through to TextureManager / ModelFactory,
		// which cache for themselves). Dropping this cache wholesale on the
		// throttle therefore re-read every material twice a second -- 132 files
		// for one Bistro, which measured as a 100-155 ms hitch roughly every
		// 0.4 s. So entries carry the timestamp they were read at, and the
		// throttle drops only the ones whose file actually changed.
		struct CachedMaterial
		{
			std::shared_ptr<MaterialAsset> asset;
			std::string file;
			long long stamp = 0;
		};
		std::unordered_map<StringId, CachedMaterial> myMaterialCache;

	public:
		// What ApplyModelMaterials derives for one (model, material assignment)
		// pair. Deriving it is all string work -- matching each mesh's material
		// name against the assigned assets' filename stems, building the
		// ray-tracing material record's name, interning it -- and the answer
		// only changes when the assignment changes or an asset is edited. Both
		// are covered by dropping this alongside the other caches, so the
		// derivation runs about twice a second instead of every frame.
		//
		// Only the string-derived half is kept: the asset each mesh resolves to
		// and its ray-tracing material index. Textures are looked up through the
		// ordinary caches every frame, so a cleared cache costs a hash miss
		// rather than redoing this. Deliberately NOT dropped by ClearCache --
		// it holds StringIds, which stay valid, and rebuilding it in the one
		// frame after a clear is what turned a steady cost into a twice-a-second
		// 130 ms hitch.
		struct ResolvedMaterials
		{
			struct Mesh
			{
				StringId materialPath;
				StringId texturePaths[4];
				TextureSrgbMode textureModes[4] = {};
				uint32_t materialIndex = 0;
				// Re-read the maps out of the asset when it is reloaded, so an
				// edited material still repoints its textures live.
				const MaterialAsset* lastMaterial = nullptr;
				bool hasMaterial = false;
			};
			std::vector<Mesh> meshes;
			bool built = false;
		};
		// aKey identifies the model and its material assignment together, so two
		// objects sharing a model with different materials get separate entries.
		ResolvedMaterials& GetResolvedMaterials(uint64_t aKey)
		{
			// Nothing evicts these, and every change of a material assignment
			// mints a new key, so bound it. Dropping the lot costs one rebuild.
			if (myResolvedMaterials.size() > 256 && !myResolvedMaterials.contains(aKey))
				myResolvedMaterials.clear();
			return myResolvedMaterials[aKey];
		}

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
		// Evicts cached materials whose file changed on disk since it was read.
		void DropChangedMaterials();

	public:

	private:
		std::unordered_map<uint64_t, ResolvedMaterials> myResolvedMaterials;
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