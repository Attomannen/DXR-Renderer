// G-buffer visualiser. CustomShaderParameters.x selects the channel:
//   1 albedo  2 world normal  3 roughness  4 metalness  5 AO  6 emissive  7 depth  8 SSAO
#include "Common.hlsli"

struct FSInput
{
	float4 position : SV_POSITION;
	float2 uv       : UV;
};

Texture2D GBufferAlbedo   : register(t10);
Texture2D GBufferNormal   : register(t11);
Texture2D GBufferMaterial : register(t12);
Texture2D GBufferEmissive : register(t13);
Texture2D GBufferDepth    : register(t14);
Texture2D SsaoTexture     : register(t18);
Texture2D LocalShadowAtlas : register(t21);

SamplerState PointSampler : register(s1);

float4 main(FSInput input) : SV_TARGET
{
	float2 uv = input.uv;
	int mode = (int)(CustomShaderParameters.x + 0.5f);

	float3 albedo = GBufferAlbedo.Sample(PointSampler, uv).rgb;
	float3 n      = GBufferNormal.Sample(PointSampler, uv).xyz;
	float3 orm    = GBufferMaterial.Sample(PointSampler, uv).rgb;
	float3 emis   = GBufferEmissive.Sample(PointSampler, uv).rgb;
	float depth   = GBufferDepth.Sample(PointSampler, uv).r;

	float3 c;
	if      (mode == 2) c = n * 0.5f + 0.5f;
	else if (mode == 3) c = orm.g.xxx;
	else if (mode == 4) c = orm.b.xxx;
	else if (mode == 5) c = orm.r.xxx;
	else if (mode == 6) c = emis;
	else if (mode == 7) c = saturate((1.0f - depth) * 40.0f).xxx;
	else if (mode == 8) c = SsaoTexture.Sample(PointSampler, uv).r.xxx;
	else if (mode == 9) c = saturate((1.0f - LocalShadowAtlas.Sample(PointSampler, uv).r) * 40.0f).xxx;
	else                c = albedo;

	return float4(c, 1.0f);
}
