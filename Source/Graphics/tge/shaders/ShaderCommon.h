#pragma once
#include <tge/math/vector2.h>
#include <tge/math/color.h>
#include <tge/math/matrix4x4.h>

#define SPRITE_BATCH_COUNT 1024
namespace Tga
{
	enum class ConstantBufferSlot
	{
		Frame = 0,
		Camera = 1,
		Light = 2,
		ShaderSettings = 3,
		Object = 4,
		Bones = 5,
		Count,
	};

	struct SpriteShaderInstanceData
	{
		Matrix4x4f transform;
		Vector4f color;
		Vector4f uv;
		Vector4f uvRect;
	};

}