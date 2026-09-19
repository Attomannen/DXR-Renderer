#pragma once

#include <age/Math/Matrix4x4.h>

namespace Ag
{

struct Plane
{
	Ag::Vector3f pos;
	Ag::Vector3f normal;
};
struct PlaneRect
{
	Ag::Vector3f tl, tr, br, bl;
};
struct Frustum
{
	Ag::Plane top, right, bottom, left, nearplane, farplane;
	Ag::PlaneRect nearrect, farrect;
};

class Camera
{
private:

	Matrix4x4f myTransform{};
	
	Matrix4x4f myProjection{};
	float myNearPlane = 1.0f;
	float myFarPlane = 10000.0f;
	
public:
	Camera();
	~Camera();
	
	void SetOrtographicProjection(float aWidth, float aHeight, float aDepth);
	void SetOrtographicProjection(float aLeft, float aRight, float aTop, float aBottom, float aNear, float aFar);
	void SetPerspectiveProjection(float aHorizontalFoV, Vector2f aResolution, float aNearPlane, float aFarPlane);

	void SetTransform(Matrix4x4f someTransform);

	Matrix4x4f& GetTransform() { return myTransform; }
	const Matrix4x4f& GetTransform() const { return myTransform; }
	const Matrix4x4f& GetProjection() const { return myProjection; }
	void GetProjectionPlanes(float& aNearPlane, float& aFarPlane) const { aNearPlane = myNearPlane; aFarPlane = myFarPlane; }
};

Frustum CalculateFrustum(const Camera& camera);
bool CheckFrustum(const Frustum& frustum, Vector3f center, float radius);


} // namespace Ag
