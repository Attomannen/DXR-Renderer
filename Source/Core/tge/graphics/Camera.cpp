#include "stdafx.h"
#include "Camera.h"
#include <iostream>

using namespace Tga;

bool Tga::CheckFrustum(const Frustum& frustum, Vector3f center, float radius)
{
	Tga::Vector3f FrustumToPoint{};

	if (frustum.top.normal.Dot(center - frustum.top.pos) <= -radius)
	{
		return false;
	}
	if (frustum.right.normal.Dot(center - frustum.right.pos) <= -radius)
	{
		return false;
	}
	if (frustum.bottom.normal.Dot(center - frustum.bottom.pos) <= -radius)
	{
		return false;
	}
	if (frustum.left.normal.Dot(center - frustum.left.pos) <= -radius)
	{
		return false;
	}
	if (frustum.nearplane.normal.Dot(center - frustum.nearplane.pos) <= -radius)
	{
		return false;
	}
	if (frustum.farplane.normal.Dot(center - frustum.farplane.pos) <= -radius)
	{
		return false;
	}

	return true;
}

Frustum Tga::CalculateFrustum(const Camera& camera)
{
	const Tga::Matrix4x4f& projection = camera.GetProjection();
	const Tga::Matrix4x4f& cameraToWorld = camera.GetTransform();

	Tga::Vector3f tlc = Tga::Vector3f{
		(-1.0f) / projection(1,1),
		(1.0f) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f brc = Tga::Vector3f{
		(1.0f) / projection(1,1),
		(-1.0f) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f trc = Tga::Vector3f{
		(1.0f) / projection(1,1),
		(1.0f) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f blc = Tga::Vector3f{
		(-1.0f) / projection(1,1),
		(-1.0f) / projection(2,2),
		1.f
	}.GetNormalized();

	float nearPlane; float farPlane;
	camera.GetProjectionPlanes(nearPlane, farPlane);

	Tga::Vector3f tln = (nearPlane * tlc) * cameraToWorld;
	Tga::Vector3f brn = (nearPlane * brc) * cameraToWorld;
	Tga::Vector3f trn = (nearPlane * trc) * cameraToWorld;
	Tga::Vector3f bln = (nearPlane * blc) * cameraToWorld;

	Tga::Vector3f tlf = (farPlane * tlc) * cameraToWorld;
	Tga::Vector3f brf = (farPlane * brc) * cameraToWorld;
	Tga::Vector3f trf = (farPlane * trc) * cameraToWorld;
	Tga::Vector3f blf = (farPlane * blc) * cameraToWorld;

	Frustum frustum;

	{
		frustum.left.pos = { tln };
		frustum.left.normal = (tln - tlf).Cross(tln - bln).GetNormalized();
		frustum.right.pos = { trn };
		frustum.right.normal = (brn - trn).Cross(trf - trn).GetNormalized();
		frustum.top.pos = { tln };
		frustum.top.normal = (trn - tln).Cross(tlf - tln).GetNormalized();
		frustum.bottom.pos = { bln };
		frustum.bottom.normal = (brf - brn).Cross(brn - bln).GetNormalized();
		frustum.nearplane.pos = { bln };
		frustum.nearplane.normal = (brn - bln).Cross(trn - brn).GetNormalized();
		frustum.farplane.pos = { blf };
		frustum.farplane.normal = (trf - brf).Cross(brf - blf).GetNormalized();
	}


	return frustum;
}

Camera::Camera()
{}

Camera::~Camera()
{}

void Camera::SetOrtographicProjection(float aWidth, float aHeight, float aDepth)
{
	myProjection = {};

	myProjection(1, 1) = 2.f/aWidth;
	myProjection(2, 2) = 2.f/aHeight;
	myProjection(3, 3) = 1.f/aDepth;
}

void Camera::SetOrtographicProjection(float aLeft, float aRight, float aTop, float aBottom, float aNear, float aFar)
{
	myProjection = {};

	myProjection(1, 1) = 2.f / (aRight - aLeft);
	myProjection(2, 2) = 2.f / (aBottom - aTop);
	myProjection(3, 3) = 1.f / (aFar - aNear);

	myProjection(4, 1) = -(aRight + aLeft) / (aRight - aLeft);
	myProjection(4, 2) = -(aBottom + aTop) / (aBottom - aTop);
	myProjection(4, 3) = -(aNear) / (aFar - aNear);
}

void Camera::SetPerspectiveProjection(float aHorizontalFoV, Vector2f aResolution, float aNearPlane, float aFarPlane)
{
	myProjection = {};

	assert(aNearPlane < aFarPlane);
	assert(aNearPlane >= FMath::KindaSmallNumber);
	
    // aHorizontalFoV is in Degrees!
	// Convert to Radians
	const float hFoVRad = aHorizontalFoV * (FMath::Pi / 180);

	float xScale = 1 / std::tanf(hFoVRad * 0.5f);
	float yScale = aResolution.x / (aResolution.y * std::tanf(hFoVRad * 0.5f));
	float Q = aFarPlane / (aFarPlane - aNearPlane);

	myProjection(1, 1) = xScale;
	myProjection(2, 2) = yScale;
	myProjection(3, 3) = Q;
	myProjection(3, 4) = 1.0f;
	myProjection(4, 3) = -Q * aNearPlane;
	myProjection(4, 4) = 0.0f;
}

void Camera::SetTransform(Matrix4x4f someTransform)
{
	myTransform = someTransform;
}
