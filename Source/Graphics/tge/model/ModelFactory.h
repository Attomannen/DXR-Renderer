#pragma once
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
	#include <mutex>
	#include <future>
#include <tge/animation/animation.h>
#include <tge/graphics/Vertex.h>
#include <tge/model/model.h>
#include <tge/model/AnimatedModelInstance.h>

#include <tge/stringRegistry/StringRegistry.h>

struct BoxSphereBounds;
namespace Tga
{

class Texture;
class AnimatedModel;

class ModelInstance;
class Model;

// Positions and triangle indices of every static mesh in a model, merged, in model
// space. The model itself drops its CPU geometry after the GPU upload.
struct CollisionGeometry
{
	std::vector<float> positions;     // x,y,z per vertex
	std::vector<uint32_t> indices;    // triangle list
};

class ModelFactory
{
	bool InitUnitCube();
	bool InitUnitPlane();
	bool InitUnitCone();
	bool InitUnitTorus();
	bool InitUnitSphere();
	bool InitUnitCylinder();
	bool InitPrimitives();
private:
	ModelFactory();
	~ModelFactory();
public:

	static ModelFactory& GetInstance() { if (!ourInstance) { ourInstance = new ModelFactory(); } return *ourInstance; }
	static void DestroyInstance() { if (ourInstance) { delete ourInstance; ourInstance = nullptr; } }

	std::shared_ptr<Model> GetModel(StringId aFilePath);

	// Reads the geometry back from the model's mesh cache (written when the model was
	// imported), so nothing has to be kept in memory for models that need no collision.
	// Call after GetModel. False for skinned models, which are not cached.
	bool GetCollisionGeometry(StringId aFilePath, CollisionGeometry& outGeometry);
	std::shared_ptr<Model> GetModel(std::string_view aFilePath);

	// Editor-only asynchronous preload. CPU FBX parsing is performed away from
	// the UI thread; PumpAsyncImports is called from the render thread to adopt
	// finished work safely. GetModel remains the synchronous runtime API.
	void RequestAsyncImport(StringId aFilePath);
	void PumpAsyncImports();
	bool IsAsyncImportPending(StringId aFilePath) const;
	// Never starts I/O or parsing; used by the editor while an async job is pending.
	std::shared_ptr<Model> GetLoadedModel(StringId aFilePath) const;

	AnimatedModelInstance GetAnimatedModelInstance(StringId aFilePath);
	AnimatedModelInstance GetAnimatedModelInstance(std::string_view aFilePath);

	ModelInstance GetModelInstance(StringId aFilePath);
	ModelInstance GetModelInstance(std::string_view aFilePath);

	std::shared_ptr<const Animation> GetAnimation(StringId aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton);
	std::shared_ptr<const Animation> GetAnimation(std::string_view aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton);

	AnimationPlayer GetAnimationPlayer(StringId aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton);
	AnimationPlayer GetAnimationPlayer(std::string_view aFilePath, const std::shared_ptr<const Skeleton>& aSkeleton);

	ModelInstance GetUnitCube();
	ModelInstance GetUnitPlane();

protected:
	
	std::shared_ptr<Model> LoadModel(StringId aFilePath);
	Tga::BoxSphereBounds CalculateBoxSphereBounds(std::vector<Tga::Vertex> somePositions);
private:	
	void OnModelChanged(StringId aUnresolvedPath);

	struct AnimationIdentifer
	{
		StringId path;
		std::shared_ptr<const Skeleton> skeleton;

		bool operator==(const AnimationIdentifer& other) const
		{
			return path == other.path && skeleton == other.skeleton;
		};
	};

	struct AnimationIdentiferHash
	{
		std::size_t operator()(const AnimationIdentifer& identifier) const
		{
			return (std::hash<StringId>()(identifier.path)) * 31 + std::hash<std::shared_ptr<const Skeleton>>()(identifier.skeleton);
		}
	};

	std::unordered_set<StringId> myWatchedPaths;
	std::unordered_map<StringId, std::shared_ptr<Model>> myLoadedModels;	
	mutable std::mutex myAsyncImportMutex;
	struct AsyncImportJob;
	std::unordered_map<StringId, std::shared_ptr<AsyncImportJob>> myAsyncImportJobs;
	std::unordered_map<AnimationIdentifer, std::shared_ptr<Animation>, AnimationIdentiferHash> myLoadedAnimations;

	static ModelFactory* ourInstance;
};

} // namespace Tga
