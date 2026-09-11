#pragma once

#include <tge/script/Property.h>
#include <tge/math/Color.h>
#include <tge/math/Vector.h>
#include <tge/stringRegistry/StringRegistry.h>

namespace Tga
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