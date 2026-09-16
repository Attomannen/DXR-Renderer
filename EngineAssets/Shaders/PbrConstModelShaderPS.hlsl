// Forward PBR shader driven by a fixed-parameter material cbuffer (b11) instead
// of textures. Pairs with PbrModelShaderVs. Used by the GameEditor Material
// Editor to preview a .tgmat that has no texture maps assigned.
#include "Common.hlsli"
#include "PBRFunctions.hlsli"
#include "ConstMaterial.hlsli"

PixelOutput main(ModelVertexToPixel input)
{
	PixelOutput result;

	float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);

	float3 albedo    = gCMBaseColor.rgb;
	float  roughness = saturate(gCMParams.x);
	float  metalness = saturate(gCMParams.y);
	float  ambientOcclusion = saturate(gCMParams.z);

	// Geometric normal only (no normal map in the flat material).
	float3 pixelNormal = normalize(input.normal.xyz);

	float3 specularColor = lerp((float3) 0.04f, albedo, metalness);
	float3 diffuseColor  = lerp((float3) 0.00f, albedo, 1 - metalness);

	float3 _ambSpecUnused;
	float3 ambiance = AmbientLightColor.rgb * EvaluateAmbiance(
		environmentTexture, pixelNormal, input.normal.xyz,
		toEye, roughness,
		ambientOcclusion, diffuseColor, specularColor, input.worldPosition.xyz, _ambSpecUnused
	);

	float3 directionalLight;
	if (DirectionalLightSoftness == 0.f)
	{
		directionalLight = EvaluateDirectionalLight(
			diffuseColor, specularColor, pixelNormal, roughness,
			DirectionalLightColor.xyz, GetSunDirectionToLight(), toEye.xyz);
	}
	else
	{
		directionalLight = EvaluateSoftDirectionalLight(
			diffuseColor, specularColor, pixelNormal, roughness, DirectionalLightSoftness,
			DirectionalLightColor.xyz, GetSunDirectionToLight(), toEye.xyz);
	}

	float3 pointLights = 0;
	for (unsigned int p = 0; p < NumberOfLights; p++)
	{
		if (PointLights[p].radius == 0.f)
		{
			pointLights += EvaluatePointLight(
				diffuseColor, specularColor, pixelNormal, roughness,
				PointLights[p].color.rgb, PointLights[p].range, PointLights[p].position.xyz,
				toEye.xyz, input.worldPosition.xyz);
		}
		else
		{
			pointLights += EvaluateSoftAreaLight(
				diffuseColor, specularColor, pixelNormal, roughness,
				PointLights[p].color.rgb, PointLights[p].radius, PointLights[p].range, PointLights[p].position.xyz,
				toEye.xyz, input.worldPosition.xyz);
		}
	}

	// Emissive strength alone drives the term; a black tint falls back to white so
	// the strength slider always does something.
	float3 emissiveTint = (max(gCMEmissiveColor.r, max(gCMEmissiveColor.g, gCMEmissiveColor.b)) < 0.001f)
		? float3(1.f, 1.f, 1.f) : gCMEmissiveColor.rgb;
	float3 emissive = emissiveTint * gCMParams.w;
	float3 radiance = directionalLight + ambiance + pointLights + emissive;

	result.color.rgb = radiance;
	result.color.a = 1.0f;
	return result;
}
