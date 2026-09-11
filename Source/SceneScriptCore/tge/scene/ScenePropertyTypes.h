#pragma once

#include <tge/script/Property.h>
#include <tge/math/Vector.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/CopyOnWriteWrapper.h>
#include <tge/EngineDefines.h>

#include <tge/animation/PoseGenerator.h>
#include <tge/math/BoxSphereBounds.h>

namespace Tga
{
	// todo: move and rename as assetProperties or something, potentially restructure asset callback so itcan be used in more places

	void RegisterAssetBrowserGetSelectionFunction(StringId(*aGetFunction)());

	struct SceneModelMeshInfo
	{
		int meshCount = 0;
		StringId meshNames[MAX_MESHES_PER_MODEL];
		BoxSphereBounds bounds;
	};

	using GetModelMeshInfoFunction = bool(*)(StringId modelPath, SceneModelMeshInfo& outMeshInfo);

	void RegisterGetModelMeshInfoFunction(GetModelMeshInfoFunction aGetFunction);
	bool GetModelMeshInfo(StringId modelPath, SceneModelMeshInfo& outMeshInfo);

	struct SceneModel
	{
		StringId path;
		StringId textures[MAX_MESHES_PER_MODEL][4] = {};
	};

	struct SceneSprite
	{
		StringId textures[4];
		Vector2f size = { 100.f, 100.f };
		Vector2f pivot = { 0.5f, 0.5f };
	};

	struct SceneReference
	{
		StringId path;
	};

	struct AnimationClipReference
	{
		StringId path;
	};

	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneModel>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneSprite>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneReference>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<AnimationClipReference>)
	DECLARE_PROPERTY_TYPE(PoseAndMotion)
}