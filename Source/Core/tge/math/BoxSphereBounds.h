#pragma once

#include <tge/math/Vector3.h>

namespace Tga
{

struct BoxSphereBounds
{
	// The radius of the Sphere
	float radius;
	// The extents of the Box
	Vector3f boxExtents;
	// The local-space center of the shape.
	Vector3f center;
};

}