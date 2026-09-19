#pragma once

#include <age/math/Vector3.h>
#include <age/math/Color.h>

namespace Ag
{

struct PointLight 
{
	Vector3f position = {};
	Color color = { 1.f, 1.f, 1.f, 1.f };
	float range = 1000.0f; // max range the light is visible
	float radius = 0.f; // radius of the light itself
};

} // namespace Ag