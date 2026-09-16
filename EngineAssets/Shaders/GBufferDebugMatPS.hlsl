// Material-preview G-buffer shader: ignores textures, writes constant PBR values
// from the DebugMat cbuffer. Pair with PbrModelShaderVS. Drives a debug sphere so
// base colour / roughness / metalness / AO / emissive can be dialled live in ImGui.
#include "Common.hlsli"

struct GBufferOutput
{
	float4 albedo   : SV_TARGET0;
	float4 normal   : SV_TARGET1;
	float4 material : SV_TARGET2; // r AO  g rough  b metal  a emissive mask
	float4 emissive : SV_TARGET3;
};

cbuffer DebugMat : register(b11)
{
	float4 gDbgBaseColor;      // rgb linear base colour
	float4 gDbgParams;         // x roughness  y metalness  z ao  w emissive strength
	float4 gDbgEmissiveColor;  // rgb emissive tint
};

GBufferOutput main(ModelVertexToPixel input)
{
	GBufferOutput o;

	float3 n = normalize(input.normal.xyz);
	float emMask = saturate(max(gDbgEmissiveColor.r, max(gDbgEmissiveColor.g, gDbgEmissiveColor.b)));

	o.albedo   = float4(gDbgBaseColor.rgb, 1.0f);
	o.normal   = float4(n, 1.0f);
	o.material = float4(saturate(gDbgParams.z), saturate(gDbgParams.x), saturate(gDbgParams.y), emMask);
	o.emissive = float4(gDbgEmissiveColor.rgb * gDbgParams.w, 1.0f);
	return o;
}
