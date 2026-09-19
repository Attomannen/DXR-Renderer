#pragma once

#include <vector>
#include <age/math/Vector.h>
#include <age/math/Matrix4x4.h>
#include <age/scene/ScenePropertyTypes.h>
#include <age/scene/SceneObjectDefinition.h>

struct ImDrawList;

namespace Ag
{
	class Camera;

	// Draws the collision an object will get at runtime as a wireframe over the viewport:
	// Collider primitives as boxes / spheres / capsules, and model-derived collision
	// (the Model "Collision" setting) as the model's real triangles when it is light
	// enough, otherwise as its bounding box.
	class CollisionOverlay
	{
	public:
		CollisionOverlay();
		~CollisionOverlay();

		void Begin(ImDrawList* drawList, const Camera& camera, const Vector2f& viewportPos, const Vector2f& viewportSize);
		// properties: the object's combined property set. worldTransform includes scale.
		void DrawObject(const std::vector<ScenePropertyDefinition>& properties, const Matrix4x4f& worldTransform);

	private:
		struct MeshLines
		{
			BoxSphereBounds bounds;
			std::vector<Vector3f> from, to; // model space; empty = too heavy, use the bounds box
		};
		const MeshLines* GetMeshLines(StringId modelPath);

		void Line(const Vector3f& a, const Vector3f& b, unsigned int color, const Matrix4x4f& world);
		void Box(const Vector3f& center, const Vector3f& half, unsigned int color, const Matrix4x4f& world);
		void Capsule(const Vector3f& center, float radius, float halfHeight, unsigned int color, const Matrix4x4f& world);
		void Circle(const Vector3f& center, const Vector3f& axisU, const Vector3f& axisV, unsigned int color, const Matrix4x4f& world, float arcStart = 0.f, float arcEnd = 6.2831853f);

		ImDrawList* myDraw = nullptr;
		Matrix4x4f myViewProj;
		Vector2f myPos, mySize;
		std::vector<std::pair<StringId, MeshLines>> myMeshCache;
	};
}
