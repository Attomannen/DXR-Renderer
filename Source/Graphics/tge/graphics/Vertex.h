#pragma once

#include <tge/math/Vector.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

// GPU vertex of static (non-skinned) meshes: 40 bytes instead of Vertex's 180.
// Keeps what the static shaders and the ray tracer read: position, normal and
// tangent frame, UV0, UV1 and the first vertex colour. Skinned meshes keep the
// full Vertex because they need bones and weights.
//
// Layout (matches MeshVertexInput in Common.hlsli and the DXR decode in
// DxrCommon.hlsli):
//   0  float3 position
//   12 float  bitangent sign (+1 / -1)
//   16 snorm16 x4: octahedral normal (xy), octahedral tangent (zw)
//   20 float2 uv0
//   28 float2 uv1
//   36 unorm8 x4: colour 0
struct MeshVertex
{
	Vector3f position;
	float bitangentSign = 1.f;
	int16_t normalTangent[4] = {};
	Vector2f uv0;
	Vector2f uv1;
	uint8_t color0[4] = {};
};

namespace VertexPacking
{
	inline int16_t ToSnorm16(float v)
	{
		return (int16_t)std::lround(std::clamp(v, -1.f, 1.f) * 32767.f);
	}

	// Octahedral encoding of a unit vector into [-1, 1]^2.
	inline void OctEncode(Vector3f n, int16_t& outX, int16_t& outY)
	{
		const float len = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
		if (len < 1e-20f) { outX = 0; outY = ToSnorm16(1.f); return; }   // degenerate -> +Z
		float x = n.x / len, y = n.y / len;
		if (n.z < 0.f)
		{
			const float ox = (1.f - std::abs(y)) * (x >= 0.f ? 1.f : -1.f);
			const float oy = (1.f - std::abs(x)) * (y >= 0.f ? 1.f : -1.f);
			x = ox; y = oy;
		}
		outX = ToSnorm16(x);
		outY = ToSnorm16(y);
	}

	inline uint8_t ToUnorm8(float v)
	{
		return (uint8_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f);
	}
}

inline MeshVertex PackMeshVertex(const Vertex& v)
{
	MeshVertex o;
	o.position = { v.position.x, v.position.y, v.position.z };

	const Vector3f n = v.normal;
	Vector3f t = v.tangent;
	// Meshes without UVs have no tangent; any vector perpendicular to the
	// normal keeps the frame valid.
	if (t.LengthSqr() < 1e-12f)
		t = std::abs(n.y) < 0.99f ? Vector3f{ -n.z, 0.f, n.x } : Vector3f{ 1.f, 0.f, 0.f };
	const Vector3f crossNT{ n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x };
	const float handedness = crossNT.x * v.binormal.x + crossNT.y * v.binormal.y + crossNT.z * v.binormal.z;
	o.bitangentSign = handedness < 0.f ? -1.f : 1.f;

	VertexPacking::OctEncode(n, o.normalTangent[0], o.normalTangent[1]);
	VertexPacking::OctEncode(t, o.normalTangent[2], o.normalTangent[3]);
	o.uv0 = v.uvs[0];
	o.uv1 = v.uvs[1];
	const Vector4f& c = v.vertexColors[0];
	o.color0[0] = VertexPacking::ToUnorm8(c.x);
	o.color0[1] = VertexPacking::ToUnorm8(c.y);
	o.color0[2] = VertexPacking::ToUnorm8(c.z);
	o.color0[3] = VertexPacking::ToUnorm8(c.w);
	return o;
}

} // namespace Tga

