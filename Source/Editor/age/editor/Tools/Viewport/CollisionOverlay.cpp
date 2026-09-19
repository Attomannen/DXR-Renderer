#include "stdafx.h"
#include "CollisionOverlay.h"

#include <cmath>
#include <imgui.h>
#include <age/graphics/Camera.h>

using namespace Ag;

namespace
{
	// Model-derived collision heavier than this is drawn as its bounding box.
	constexpr size_t kMaxMeshTriangles = 6000;

	const ImU32 kStaticColor = IM_COL32(70, 230, 100, 220);
	const ImU32 kDynamicColor = IM_COL32(255, 160, 50, 230);

	Vector3f Transform(const Vector3f& p, const Matrix4x4f& m)
	{
		return {
			p.x * m(1, 1) + p.y * m(2, 1) + p.z * m(3, 1) + m(4, 1),
			p.x * m(1, 2) + p.y * m(2, 2) + p.z * m(3, 2) + m(4, 2),
			p.x * m(1, 3) + p.y * m(2, 3) + p.z * m(3, 3) + m(4, 3) };
	}
}

CollisionOverlay::CollisionOverlay() = default;
CollisionOverlay::~CollisionOverlay() = default;

void CollisionOverlay::Begin(ImDrawList* drawList, const Camera& camera, const Vector2f& viewportPos, const Vector2f& viewportSize)
{
	myDraw = drawList;
	myViewProj = Matrix4x4f::GetFastInverse(camera.GetTransform()) * camera.GetProjection();
	myPos = viewportPos;
	mySize = viewportSize;
}

void CollisionOverlay::Line(const Vector3f& a, const Vector3f& b, unsigned int color, const Matrix4x4f& world)
{
	const Vector3f wa = Transform(a, world), wb = Transform(b, world);
	Vector4f ca = Vector4f(wa.x, wa.y, wa.z, 1.f) * myViewProj;
	Vector4f cb = Vector4f(wb.x, wb.y, wb.z, 1.f) * myViewProj;

	// Clip against the near plane so lines that cross behind the camera still draw.
	constexpr float kNear = 0.1f;
	if (ca.w <= kNear && cb.w <= kNear)
		return;
	if (ca.w <= kNear) { const float t = (kNear - ca.w) / (cb.w - ca.w); ca = ca + (cb - ca) * t; }
	else if (cb.w <= kNear) { const float t = (kNear - cb.w) / (ca.w - cb.w); cb = cb + (ca - cb) * t; }

	const ImVec2 sa(myPos.x + (ca.x / ca.w * 0.5f + 0.5f) * mySize.x, myPos.y + (1.f - (ca.y / ca.w * 0.5f + 0.5f)) * mySize.y);
	const ImVec2 sb(myPos.x + (cb.x / cb.w * 0.5f + 0.5f) * mySize.x, myPos.y + (1.f - (cb.y / cb.w * 0.5f + 0.5f)) * mySize.y);
	myDraw->AddLine(sa, sb, color, 1.f);
}

void CollisionOverlay::Box(const Vector3f& c, const Vector3f& h, unsigned int color, const Matrix4x4f& world)
{
	Vector3f p[8];
	for (int i = 0; i < 8; ++i)
		p[i] = { c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z) };
	for (int i = 0; i < 8; ++i)
		for (int axis = 0; axis < 3; ++axis)
			if (!(i & (1 << axis)))
				Line(p[i], p[i | (1 << axis)], color, world);
}

void CollisionOverlay::Circle(const Vector3f& c, const Vector3f& u, const Vector3f& v, unsigned int color, const Matrix4x4f& world, float arcStart, float arcEnd)
{
	constexpr int kSegments = 24;
	Vector3f previous = c + u * std::cos(arcStart) + v * std::sin(arcStart);
	for (int i = 1; i <= kSegments; ++i)
	{
		const float a = arcStart + (arcEnd - arcStart) * i / kSegments;
		const Vector3f next = c + u * std::cos(a) + v * std::sin(a);
		Line(previous, next, color, world);
		previous = next;
	}
}

const CollisionOverlay::MeshLines* CollisionOverlay::GetMeshLines(StringId modelPath)
{
	for (auto& entry : myMeshCache)
		if (entry.first == modelPath)
			return &entry.second;

	// Not loaded yet (the editor imports asynchronously): ask again next frame.
	SceneModelCollisionInfo info;
	if (!GetModelCollisionInfo(modelPath, kMaxMeshTriangles, info))
		return nullptr;

	MeshLines lines;
	lines.bounds = info.bounds;
	if (info.hasGeometry)
	{
		auto position = [&](uint32_t index) { return Vector3f(info.positions[index * 3], info.positions[index * 3 + 1], info.positions[index * 3 + 2]); };
		for (size_t i = 0; i + 2 < info.indices.size(); i += 3)
			for (int e = 0; e < 3; ++e)
			{
				lines.from.push_back(position(info.indices[i + e]));
				lines.to.push_back(position(info.indices[i + (e + 1) % 3]));
			}
	}
	myMeshCache.emplace_back(modelPath, std::move(lines));
	return &myMeshCache.back().second;
}

void CollisionOverlay::Capsule(const Vector3f& o, float r, float h, unsigned int color, const Matrix4x4f& world)
{
	const Vector3f top = o + Vector3f{ 0, h, 0 }, bottom = o - Vector3f{ 0, h, 0 };
	Circle(top, { r, 0, 0 }, { 0, 0, r }, color, world);
	Circle(bottom, { r, 0, 0 }, { 0, 0, r }, color, world);
	for (const Vector3f& side : { Vector3f{ r, 0, 0 }, Vector3f{ -r, 0, 0 }, Vector3f{ 0, 0, r }, Vector3f{ 0, 0, -r } })
		Line(top + side, bottom + side, color, world);
	constexpr float kPi = 3.14159265f;
	Circle(top, { r, 0, 0 }, { 0, r, 0 }, color, world, 0.f, kPi);
	Circle(top, { 0, 0, r }, { 0, r, 0 }, color, world, 0.f, kPi);
	Circle(bottom, { r, 0, 0 }, { 0, -r, 0 }, color, world, 0.f, kPi);
	Circle(bottom, { 0, 0, r }, { 0, -r, 0 }, color, world, 0.f, kPi);
}

void CollisionOverlay::DrawObject(const std::vector<ScenePropertyDefinition>& properties, const Matrix4x4f& world)
{
	if (!myDraw)
		return;

	const SceneModel* model = nullptr;
	const SceneCollider* collider = nullptr;
	const SceneRigidBody* body = nullptr;
	const SceneCharacter* character = nullptr;
	for (const ScenePropertyDefinition& property : properties)
	{
		if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
			model = &property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get();
		else if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneCollider>>())
			collider = &property.value.Get<CopyOnWriteWrapper<SceneCollider>>()->Get();
		else if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneCharacter>>())
			character = &property.value.Get<CopyOnWriteWrapper<SceneCharacter>>()->Get();
		else if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneRigidBody>>())
			body = &property.value.Get<CopyOnWriteWrapper<SceneRigidBody>>()->Get();
	}

	const bool isStatic = !body || body->motion == SceneBodyMotion::Static;
	const ImU32 color = isStatic ? kStaticColor : kDynamicColor;

	// A character is a capsule standing on the object's origin.
	if (character)
	{
		const float halfHeight = std::max(character->height * 0.5f - character->radius, 0.f);
		Capsule({ 0.f, character->height * 0.5f, 0.f }, character->radius, halfHeight, IM_COL32(255, 220, 60, 230), world);
	}

	// Explicit primitive on a Collider component.
	if (collider && collider->shape != SceneColliderShape::Auto && collider->shape != SceneColliderShape::ConvexHull && collider->shape != SceneColliderShape::TriangleMesh)
	{
		const Vector3f& o = collider->offset;
		switch (collider->shape)
		{
		case SceneColliderShape::Box:
			Box(o, collider->halfExtents, color, world);
			break;
		case SceneColliderShape::Sphere:
		{
			const float r = collider->radius;
			Circle(o, { r, 0, 0 }, { 0, r, 0 }, color, world);
			Circle(o, { r, 0, 0 }, { 0, 0, r }, color, world);
			Circle(o, { 0, r, 0 }, { 0, 0, r }, color, world);
			break;
		}
		case SceneColliderShape::Capsule:
			Capsule(o, collider->radius, collider->halfHeight, color, world);
			break;
		default:
			break;
		}
		return;
	}

	// Collision generated from the model.
	if (!model || model->path.IsEmpty())
		return;
	const bool fromCollider = collider != nullptr; // Auto / ConvexHull / TriangleMesh on a Collider
	if (model->collision == SceneModelCollision::None && !fromCollider)
		return;

	const MeshLines* lines = GetMeshLines(model->path);
	if (!lines)
		return;

	if (model->collision == SceneModelCollision::Box || lines->from.empty())
	{
		// Box, or too heavy to draw every triangle: the whole model's bounding box.
		Box(lines->bounds.center, lines->bounds.boxExtents, color, world);
		return;
	}
	for (size_t i = 0; i < lines->from.size(); ++i)
		Line(lines->from[i], lines->to[i], color, world);
}
