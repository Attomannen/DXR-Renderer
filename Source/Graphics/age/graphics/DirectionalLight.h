#pragma once
#include <age/math/Color.h>
#include <age/math/Matrix.h>

namespace Ag
{

struct DirectionalLight 
{
	Matrix4x4f transform = {}; // forward points along sunlight travel, not toward the sun
	Color color = {};
	float softness = 0.f; // 0.f: pure directional light. 1.f: omnidirectional ambient light
};

} // namespace Ag

