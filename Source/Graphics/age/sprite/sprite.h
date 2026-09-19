/*
Use this class to create and show a sprite
*/

#pragma once
#include <age/render/RenderCommon.h>
#include <age/math/Color.h>
#include <age/math/Matrix4x4.h>

namespace Ag
{
	class TextureResource;
	class SpriteShader;

	struct SpriteSharedData
	{
		const TextureResource* texture = nullptr;
		const TextureResource* maps[MAP_MAX] = { nullptr };
		const SpriteShader* customShader = nullptr;
	};

	struct Sprite2DInstanceData
	{
		Vector2f position = { 0.0f, 0.0f };
		Vector2f pivot = { 0.5f, 0.5f };
		Vector2f size = { 1.0f, 1.0f };
		Vector2f sizeMultiplier = { 1.0f, 1.0f };
		Vector2f uv = { 0.0f, 0.0f };
		Vector2f uvScale = { 1.0f, 1.0f };
		Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
		TextureRext textureRect = { 0.0f, 0.0f, 1.0f, 1.0f };
		float rotation = 0.f;
		bool isHidden = false;
	};

	struct Sprite3DInstanceData
	{
		Matrix4x4f transform = {};
		Vector2f uv = { 0.0f, 0.0f };
		Vector2f uvScale = { 1.0f, 1.0f };
		Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
		TextureRext textureRect = { 0.0f, 0.0f, 1.0f, 1.0f };
		bool isHidden = false;
	};
}