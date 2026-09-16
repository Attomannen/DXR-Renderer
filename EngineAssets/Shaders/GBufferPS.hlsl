// Deferred geometry pass. Same material decode as PbrModelShaderPS, but writes
// the G-buffer instead of shading. Pair with PbrModelShaderVS.
#include "Common.hlsli"

struct GBufferOutput
{
	float4 albedo   : SV_TARGET0; // rgb: linear base colour        a: 1
	float4 normal   : SV_TARGET1; // rgb: world-space pixel normal   a: 1
	float4 material : SV_TARGET2; // r: AO  g: roughness  b: metalness  a: emissive mask
	float4 emissive : SV_TARGET3; // rgb: emissive radiance (albedo * mask)
};

GBufferOutput main(ModelVertexToPixel input)
{
	GBufferOutput o;

	float2 uv = input.texCoord0;
	float4 albedo = albedoTexture.Sample(defaultSampler, uv).rgba;

	if (albedo.a <= AlphaTestThreshold)
		discard;

	// Tangent-space normal -> world space (matches the forward path's TBN).
	float3 nT = normalTexture.Sample(defaultSampler, uv).xyy;
	nT.xy = 2.0f * nT.xy - 1.0f;
	nT.z = sqrt(1.0f - saturate(nT.x * nT.x + nT.y * nT.y));
	nT = normalize(nT);

	float3x3 TBN = float3x3(
		normalize(input.tangent.xyz),
		normalize(-input.binormal.xyz),
		normalize(input.normal.xyz));
	TBN = transpose(TBN);
	float3 worldNormal = normalize(mul(TBN, nT));

	// TGA ORM pack: r = AO, g = roughness, b = metalness.
	float3 orm = materialTexture.Sample(defaultSampler, uv).rgb;

	// _FX: r = emissive mask, g = emissive strength / MAX_EMISSIVE_STRENGTH.
	float2 fx = fxTexture.Sample(defaultSampler, uv).rg;
	float emissiveMask = fx.r;
	float emissiveStrength = fx.g * MAX_EMISSIVE_STRENGTH;

	o.albedo   = float4(albedo.rgb, 1.0f);
	o.normal   = float4(worldNormal, 1.0f);
	o.material = float4(orm, emissiveMask);
	o.emissive = float4(albedo.rgb * emissiveMask * emissiveStrength, 1.0f);   // HDR
	return o;
}
