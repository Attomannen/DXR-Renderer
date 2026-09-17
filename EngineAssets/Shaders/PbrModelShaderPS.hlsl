#include "Common.hlsli"
#include "PBRFunctions.hlsli"
#include "MaterialSurface.hlsli"

PixelOutput main(ModelVertexToPixel input)
{
	PixelOutput result;

	float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);
	const MaterialSurface surface = SampleMaterialSurface(input);
	if (surface.coverage <= gMaterial.alphaCutoff)
		discard;

	const float3 albedo = surface.baseColor;
	const float3 pixelNormal = surface.normal;
	const float ambientOcclusion = surface.ao;
	const float roughness = surface.roughness;
	const float metalness = surface.metalness;

	float3 specularColor = lerp((float3) 0.04f, albedo, metalness);
	float3 diffuseColor = lerp((float3) 0.00f, albedo, 1 - metalness);

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

	
	float3 pointLights = 0; // <- The sum of all point lights.
	for(unsigned int p = 0; p < NumberOfLights; p++)
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
	
	float3 radiance = directionalLight + ambiance + pointLights + surface.emissive;

    result.color.rgb = (float3) radiance;
	result.color.a = surface.opacity;
	return result;
}

