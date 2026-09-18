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

	// Collision generated from the model's own geometry. Static objects (no Rigidbody)
	// use it as level geometry; with a Rigidbody, Auto/Box/ConvexHull make it a prop.
	enum class SceneModelCollision : int
	{
		None,
		Auto,         // static -> triangle mesh, dynamic -> convex hull
		Box,          // fitted to the model bounds
		ConvexHull,
		TriangleMesh, // static only
	};

	// What the editor needs to draw a model's collision: the union bounds of the whole
	// model and, when it has at most maxTriangles, its triangles (model space).
	struct SceneModelCollisionInfo
	{
		BoxSphereBounds bounds;
		bool hasGeometry = false;
		std::vector<float> positions; // x,y,z per vertex
		std::vector<uint32_t> indices;
	};

	using GetModelCollisionInfoFunction = bool(*)(StringId modelPath, size_t maxTriangles, SceneModelCollisionInfo& outInfo);

	void RegisterGetModelCollisionInfoFunction(GetModelCollisionInfoFunction aGetFunction);
	bool GetModelCollisionInfo(StringId modelPath, size_t maxTriangles, SceneModelCollisionInfo& outInfo);

	struct SceneModel
	{
		StringId path;
		// One authored .tgmat asset per model mesh. Texture-map paths belong to
		// the material asset, keeping model definitions small and reusable.
		StringId materials[MAX_MESHES_PER_MODEL] = {};
		SceneModelCollision collision = SceneModelCollision::None;
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
		bool isTrigger = false;                      // reports overlaps (On Trigger Enter) instead of blocking
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

	// A view the object carries: the game looks through it while it is the active camera.
	// It sits at offset (cm, turned with the object's yaw) and looks along the object's
	// heading; scripts add pitch.
	struct SceneCamera
	{
		Vector3f offset = { 0.f, 170.f, 0.f };  // eye height for a standing character
		float fov = 90.f;                       // horizontal, degrees
		bool activeOnStart = true;
	};

	// A walking character: a capsule that steps, slides and climbs slopes (not a rigid body).
	// The object's origin is its feet. Scripts drive it with Move and Jump.
	struct SceneCharacter
	{
		float radius = 35.f;          // cm
		float height = 170.f;         // cm, feet to top of head
		float stepHeight = 40.f;      // cm, the tallest step it walks up
		float maxSlope = 50.f;        // degrees; steeper ground is not walkable
		float mass = 80.f;            // kg, for pushing bodies
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
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCamera>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneCharacter>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<SceneReference>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<AnimationClipReference>)
	DECLARE_PROPERTY_TYPE(PoseAndMotion)
}
