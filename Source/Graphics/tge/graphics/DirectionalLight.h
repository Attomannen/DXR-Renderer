#pragma once
#include <tge/math/Color.h>
#include <tge/math/Matrix.h>

namespace Tga
{

struct DirectionalLight 
{
	Matrix4x4f transform = {};
	Color color = {};
	float softness = 0.f; // 0.f: pure directional light. 1.f: omnidirectional ambient light
};

} // namespace Tga

