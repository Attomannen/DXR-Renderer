#pragma once
#include <tge/math/Vector2.h>

namespace Tga
{
	enum class BlendState
	{
		Disabled,
		AlphaBlend,
		AdditiveBlend,
		Count
	};

	enum class SamplerFilter
	{
		Point,
		Bilinear,
		Trilinear,
		Count
	};

	enum class SamplerAddressMode
	{
		Clamp,
		Wrap,
		Mirror,
		Count
	};

	enum class DepthStencilState
	{
		WriteLess,
		WriteLessOrEqual,
		ReadOnlyLess,
		ReadOnlyLessOrEqual,
		Count
	};

	enum class RasterizerState
	{
		BackfaceCulling,
		FrontFaceCulling,
		NoFaceCulling,
		Wireframe,
		WireframeNoCulling,
		Count,
	};

	enum ShaderMap
	{
		NORMAL_MAP,
		MATERIAL_MAP,
		FX_MAP,
		MAP_MAX,
	};

#pragma warning( push )
#pragma warning( disable : 26495)

#pragma warning( pop )

	struct VertexInstanced
	{
		float x, y, z, w;      // position
		unsigned int vertexIndex, unused1, unused2, unused3;
	};

	struct SimpleVertex
	{
		float x, y, z;      // position
		float colorR, colorG, colorB, colorA;
		float u, v;
	};

	struct TextureRext
	{
		float startX;
		float startY;
		float endX;
		float endY;
	};
}