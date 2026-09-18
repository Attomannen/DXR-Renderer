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
		// The FBX material each slot renders with, exactly as authored (Blender's
		// ".001" duplicate suffixes included). This is what the material-slot list
		// should show -- meshNames are the source *node* names ("Mesh.123"), which
		// say nothing about which material a slot is.
		StringId materialNames[MAX_MESHES_PER_MODEL];
		BoxSphereBounds bounds;
	};

	using GetModelMeshInfoFunction = bool(*)(StringId modelPath, SceneModelMeshInfo& outMeshInfo);

	void RegisterGetModelMeshInfoFunction(GetModelMeshInfoFunction aGetFunction);
	bool GetModelMeshInfo(StringId modelPath, SceneModelMeshInfo& outMeshInfo);

	struct SceneModel
	{
		StringId path;
		// One authored .tgmat asset per model mesh. Texture-map paths belong to
		// the material asset, keeping model definitions small and reusable.
		StringId materials[MAX_MESHES_PER_MODEL] = {};
	};

	// Physics components. Collider decides the shape; Rigidbody decides how the object
	// moves. A Collider without a Rigidbody is static level geometry.
	enum class SceneColliderShape : int
	{
		Auto,         // built from the Mesh: static -> triangle mesh, dynamic -> convex hull
		Box,
		Sphere,
		Capsule,
		ConvexHull,   // from the Mesh
		TriangleMesh, // from the Mesh, static only
	};

	struct SceneCollider
	{
		SceneColliderShape shape = SceneColliderShape::Auto;
		Vector3f halfExtents = { 50.f, 50.f, 50.f }; // Box, cm
		float radius = 50.f;                         // Sphere / Capsule, cm
		float halfHeight = 50.f;                     // Capsule cylinder half height, cm
		Vector3f offset = { 0.f, 0.f, 0.f };         // from the object origin, cm
	};

	enum class SceneBodyMotion : int
	{
		Static,
		Kinematic,
		Dynamic,
	};

	struct SceneRigidBody
	{
		SceneBodyMotion motion = SceneBodyMotion::Dynamic;
		float mass = 0.f; // kg, 0 = from the collider volume
		float friction = 0.5f;
		float restitution = 0.f;
		float gravityFactor = 1.f;
		float linearDamping = 0.05f;
		float angularDamping = 0.05f;
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
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCollider>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneRigidBody>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneReference>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<AnimationClipReference>)
	DECLARE_PROPERTY_TYPE(PoseAndMotion)
}
