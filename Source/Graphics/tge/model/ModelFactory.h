#pragma once
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <tge/animation/animation.h>
#include <tge/graphics/Vertex.h>
#include <tge/model/model.h>
#include <tge/model/AnimatedModelInstance.h>

#include <tge/stringRegistry/StringRegistry.h>

struct ID3D11Device;
struct BoxSphereBounds;
namespace Tga
{

class Texture;
class AnimatedModel;

class ModelInstance;
class Model;

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
	std::shared_ptr<Model> GetModel(std::string_view aFilePath);

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
	std::unordered_map<AnimationIdentifer, std::shared_ptr<Animation>, AnimationIdentiferHash> myLoadedAnimations;

	static ModelFactory* ourInstance;
};

} // namespace Tga
