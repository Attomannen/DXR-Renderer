// Deferred geometry pass. Same material decode as PbrModelShaderPS
// (MaterialSurface.hlsli), but writes the G-buffer instead of shading.
// Pair with PbrModelShaderVS.
#include "Common.hlsli"
#include "MaterialSurface.hlsli"

struct GBufferOutput
{
	float4 albedo   : SV_TARGET0; // rgb: linear base colour        a: 1
	float4 normal   : SV_TARGET1; // rgb: world-space pixel normal   a: 1
	float4 material : SV_TARGET2; // r: AO  g: roughness  b: metalness  a: emissive mask
	float4 emissive : SV_TARGET3; // rgb: emissive radiance, HDR
};

GBufferOutput main(ModelVertexToPixel input)
{
	const MaterialSurface surface = SampleMaterialSurface(input);
	if (surface.coverage <= gMaterial.alphaCutoff)
		discard;

	GBufferOutput o;
	const float emissiveMask = saturate(max(surface.emissive.r, max(surface.emissive.g, surface.emissive.b)));
	o.albedo   = float4(surface.baseColor, 1.0f);
	o.normal   = float4(surface.normal, 1.0f);
	o.material = float4(surface.ao, surface.roughness, surface.metalness, emissiveMask);
	o.emissive = float4(surface.emissive, 1.0f);
	return o;
}
