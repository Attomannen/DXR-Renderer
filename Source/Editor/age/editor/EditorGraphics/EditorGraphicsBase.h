#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <imgui/imgui.h>
#include <age/animation/Pose.h>
#include <age/math/Color.h>
#include <age/math/Vector.h>
#include <age/editor/Material/Graph/MaterialGraph.h>
#include <unordered_map>
#include <age/stringRegistry/StringRegistry.h>
namespace Ag
{
struct AnimationClip;
class EditorViewport;
class Scene;
class SceneSelection;
class SceneObjectDefinition;
struct LivePreviewData;

struct ObjectDefinitionDrawParameters
{
	EditorViewport* viewport;
	LivePreviewData* livePreviewData;
	SceneObjectDefinition* objectDefinition;
	StringId selectedProperty;
};

class ObjectDefinitionEditorGraphicsBase
{
public:

	virtual void Draw(ObjectDefinitionDrawParameters& parameters) = 0;
	virtual void DrawVisualPreviewSettings() {}

};

struct SceneDrawParameters
{
	EditorViewport* viewport;
	Scene* scene;
	SceneSelection* sceneSelection;
};

class SceneEditorGraphicsBase
{
public:
	virtual void Draw(const SceneDrawParameters& parameters) = 0;

};

struct AnimationClipDrawParameters
{
	EditorViewport* viewport;
	AnimationClip* clip;
	float currentTime;
	int selectedSkeletonNodeIndex;
};

class AnimationClipEditorGraphicsBase
{
public:
	virtual void Draw(const AnimationClipDrawParameters& parameters) = 0;

};

struct MaterialAsset;

struct MaterialEditorDrawParameters
{
	EditorViewport* viewport;
	MaterialAsset* material;
};

// A node-graph "Bake" request: evaluate `graph`'s connected Output sockets
// into real _C/_N/_M/_FX.dds files (written into the same folder as
// `absoluteMatPath`, named from `gameRootRelativeStem`) and update `material`
// to reference them, then save it. Implemented in EditorDefaultGraphics
// (needs DirectXTex-backed pixel I/O via Ag::TextureCpu, which only that
// project links) -- see MaterialGraphBake.cpp there.
struct MaterialGraphBakeRequest
{
	const MaterialGraphNS::MaterialGraph* graph = nullptr;
	MaterialAsset* material = nullptr;
	std::string absoluteMatPath;        // e.g. C:/.../Source/Game/data/Sponza/Arches.tgmat
	std::string gameRootRelativeStem;   // e.g. "Sponza/Arches" (no extension) -- what mat.maps[] stores
	int width = 1024;
	int height = 1024;
};

class MaterialEditorGraphicsBase
{
public:
	virtual ~MaterialEditorGraphicsBase() = default;
	virtual void Draw(const MaterialEditorDrawParameters& parameters) = 0;
	virtual void DrawPreviewSettings() {}
	// Returns false (default: unimplemented backend) or on any bake failure --
	// the caller should treat that as "nothing was written", not a partial bake.
	virtual bool BakeMaterialGraph(const MaterialGraphBakeRequest&) { return false; }
};


namespace Particles { class SystemInstance; }

struct ParticleEditorDrawParameters
{
	EditorViewport* viewport;
	const Particles::SystemInstance* system;
};

class ParticleEditorGraphicsBase
{
public:
	virtual ~ParticleEditorGraphicsBase() = default;
	virtual void Draw(const ParticleEditorDrawParameters& parameters) = 0;
};


class EditorGraphicsBase
{
public:
	virtual std::unique_ptr<ObjectDefinitionEditorGraphicsBase> CreateObjectDefinitionGraphicsInterface() const = 0;
	virtual std::unique_ptr<SceneEditorGraphicsBase> CreateSceneGraphicsInterface() const = 0;
	virtual std::unique_ptr<AnimationClipEditorGraphicsBase> CreateAnimationClipGraphicsInterface() const = 0;
	virtual std::unique_ptr<MaterialEditorGraphicsBase> CreateMaterialGraphicsInterface() const = 0;
	// Null when the backend has no particle preview.
	virtual std::unique_ptr<ParticleEditorGraphicsBase> CreateParticleGraphicsInterface() const { return nullptr; }

	virtual ImTextureID GetTextureID(std::string_view /*aTexturePath*/) const { return 0; }
	virtual void DrawLines(const Color* /*someColors*/, const Vector3f* /*someFromPositions*/, const Vector3f* /*someToPositions*/, unsigned int /*aCount*/) const {}
};

}
