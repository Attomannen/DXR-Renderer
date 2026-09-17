#ifndef MATERIAL_SURFACE_HLSLI
#define MATERIAL_SURFACE_HLSLI

// One material decode for every raster model shader (G-buffer, forward PBR,
// glass, editor preview). The DXR equivalent is DecodeHit in DxrCommon.hlsli;
// keep the two in step. Include after Common.hlsli.
#include "MaterialParams.hlsli"

struct MaterialSurface
{
	float3 baseColor;
	float  coverage;    // base colour alpha, tested against alphaCutoff
	float  opacity;     // coverage x material opacity: the blend weight
	float3 normal;      // world space
	float  ao;
	float  roughness;
	float  metalness;
	float3 emissive;    // HDR, scene units
};

MaterialSurface SampleMaterialSurface(ModelVertexToPixel input)
{
	const MaterialParams m = gMaterial;
	MaterialSurface s;
	const float2 uv = input.texCoord0;
	const float3 geometricNormal = normalize(input.normal.xyz);

	if ((m.flags & MATERIAL_FLAG_USE_TEXTURES) == 0u)
	{
		s.baseColor = m.baseColorFactor;
		s.coverage = 1.0f;
		s.opacity = m.opacity;
		s.normal = geometricNormal;
		s.ao = 1.0f;
		s.roughness = saturate(m.roughnessFactor);
		s.metalness = saturate(m.metalnessFactor);
		s.emissive = MaterialEmissive(m, s.baseColor, 0.0f);
		return s;
	}

	const float4 baseColor = albedoTexture.Sample(defaultSampler, uv);
	s.baseColor = baseColor.rgb * m.baseColorFactor;
	s.coverage = baseColor.a;
	s.opacity = baseColor.a * m.opacity;

	// Tangent-space normal -> world. The engine's TBN negates the authored
	// binormal, which makes +green point down the texture: DirectX / Unreal.
	const float3 tangentNormal = DecodeMaterialNormal(normalTexture.Sample(defaultSampler, uv).xy, m);
	const float3x3 tbn = float3x3(
		normalize(input.tangent.xyz),
		normalize(-input.binormal.xyz),
		geometricNormal);
	s.normal = normalize(mul(tangentNormal, tbn));

	// ORM: r AO, g roughness, b metalness (Unreal order).
	const float3 orm = materialTexture.Sample(defaultSampler, uv).rgb;
	s.ao = lerp(1.0f, orm.r, m.aoStrength);
	s.roughness = saturate(orm.g * m.roughnessFactor);
	s.metalness = saturate(orm.b * m.metalnessFactor);

	s.emissive = MaterialEmissive(m, s.baseColor, fxTexture.Sample(defaultSampler, uv));
	return s;
}

#endif
