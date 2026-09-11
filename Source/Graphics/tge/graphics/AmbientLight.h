#pragma once
#include <tge/math/color.h>

namespace Tga
{
class TextureResource;

enum class AmbientLightType
{
	Uniform,
	UniformAboveHorizon,
	Custom
};

struct AmbientLight
{
	Color color = {1.f, 1.f, 1.f, 1.f};
	AmbientLightType type = AmbientLightType::Uniform;
	TextureResource* cubemap = nullptr;

};

} // namespace Tga
