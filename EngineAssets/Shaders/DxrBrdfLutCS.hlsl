// Split-sum environment BRDF integration LUT. X = NdotV, Y = perceptual
// roughness; RG stores the A/B terms used as F0 * A + B.

RWTexture2D<float2> gBrdfLut : register(u0);

cbuffer BrdfLutConstants : register(b0)
{
	uint gLutSize;
	uint gSampleCount;
	uint2 _pad;
};

static const float PI = 3.14159265359f;

float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint count) { return float2(float(i) / float(count), RadicalInverseVdC(i)); }

float3 ImportanceSampleGGX(float2 xi, float roughness, float3 n)
{
	const float a = roughness * roughness;
	const float phi = 2.0f * PI * xi.x;
	const float cosTheta = sqrt((1.0f - xi.y) / max(1.0f + (a * a - 1.0f) * xi.y, 1e-5f));
	const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
	const float3 h = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	const float3 tangent = abs(n.z) < 0.999f ? normalize(cross(float3(0,0,1), n)) : float3(1,0,0);
	return normalize(tangent * h.x + cross(n, tangent) * h.y + n * h.z);
}

float GeometrySchlickGGX(float noV, float roughness)
{
	const float k = roughness * roughness * 0.5f;
	return noV / max(noV * (1.0f - k) + k, 1e-5f);
}

float GeometrySmith(float noV, float noL, float roughness)
{
	return GeometrySchlickGGX(noV, roughness) * GeometrySchlickGGX(noL, roughness);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= gLutSize || id.y >= gLutSize) return;
	const float noV = max((float(id.x) + 0.5f) / float(gLutSize), 1e-4f);
	const float roughness = (float(id.y) + 0.5f) / float(gLutSize);
	const float3 v = float3(sqrt(saturate(1.0f - noV * noV)), 0, noV);
	const float3 n = float3(0,0,1);
	float a = 0.0f, b = 0.0f;
	for (uint i = 0; i < gSampleCount; ++i)
	{
		const float3 h = ImportanceSampleGGX(Hammersley(i, gSampleCount), roughness, n);
		const float3 l = normalize(2.0f * dot(v, h) * h - v);
		const float noL = saturate(l.z), noH = saturate(h.z), voH = saturate(dot(v, h));
		if (noL > 0.0f)
		{
			const float g = GeometrySmith(noV, noL, roughness);
			const float gVis = (g * voH) / max(noH * noV, 1e-5f);
			const float fc = pow(1.0f - voH, 5.0f);
			a += (1.0f - fc) * gVis;
			b += fc * gVis;
		}
	}
	gBrdfLut[id.xy] = float2(a, b) / float(gSampleCount);
}
