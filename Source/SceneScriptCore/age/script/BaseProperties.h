#pragma once

#include <age/script/Property.h>
#include <age/math/Color.h>
#include <age/math/Vector.h>
#include <age/stringRegistry/StringRegistry.h>

namespace Ag
{
	DECLARE_PROPERTY_TYPE(bool)
	DECLARE_PROPERTY_TYPE(int)
	DECLARE_PROPERTY_TYPE(float)
	DECLARE_PROPERTY_TYPE(Vector2f)
	DECLARE_PROPERTY_TYPE(Vector3f)
	DECLARE_PROPERTY_TYPE(Vector4f)
	DECLARE_PROPERTY_TYPE(Color)
	DECLARE_PROPERTY_TYPE(StringId)
}