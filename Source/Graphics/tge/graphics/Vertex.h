#pragma once

#include <tge/math/Vector.h>

namespace Tga
{

struct Vertex
{
	// Debug layout
	// float4 float4 float4 float4 float4 float2 float2 float2 float2 float3 float3 float3
	
	Vector4f position = {0,0,0,0 };
	Vector4f vertexColors[4]
	{
		{0, 0, 0, 0},
		{0, 0, 0, 0},
		{0, 0, 0, 0},
		{0, 0, 0, 0},
	};

	Vector2f uvs[4]
	{
		{0, 0},
		{0, 0},
		{0, 0},
		{0, 0}
	};

	Vector3f normal = {0, 0, 0};
	Vector3f tangent = { 0, 0, 0 };
	Vector3f binormal = { 0, 0, 0 };
	Vector4f bones = { 0, 0, 0, 0 };
	Vector4f weights = { 0, 0, 0, 0 };

	Vertex() = default;

	Vertex(float X, float Y, float Z, float R, float G, float B, float A, float U, float V)
	{
		position = { X, Y, Z, 1 };
		vertexColors[0] = { R, G, B, A };
		uvs[0] = { U, V };
	}

	Vertex(float X, float Y, float Z, float nX, float nY, float nZ, float tX, float tY, float tZ, float bX, float bY, float bZ, float R, float G, float B, float A, float U, float V)
	{
		position = { X, Y, Z , 1 };
		vertexColors[0] = { R, G, B, A };
		uvs[0] = { U, V };
		normal = { nX, nY, nZ };
		tangent = { tX, tY, tZ };
		binormal = { bX, bY, bZ };
	}
};

} // namespace Tga
