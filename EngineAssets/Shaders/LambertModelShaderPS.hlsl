#include "Common.hlsli"
#include "LambertFunctions.hlsli"

PixelOutput main(ModelVertexToPixel input)
{
	PixelOutput result;

	float2 scaledUV = input.texCoord0;
	
	float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);
    float4 albedo = albedoTexture.Sample(defaultSampler, scaledUV).rgba;

	if (albedo.a <= AlphaTestThreshold)
	{
		discard;
		result.color = float4(0.f, 0.f, 0.f, 0.f);
		return result;
	}

	float3 normal = normalTexture.Sample(defaultSampler, scaledUV).xyy;

	normal.xy = 2.0f * normal.xy - 1.0f;
	normal.z = sqrt(1 - saturate(normal.x * normal.x + normal.y * normal.y));
	normal = normalize(normal);

	float3x3 TBN = float3x3(
		normalize(input.tangent.xyz),
		normalize(-input.binormal.xyz),
		normalize(input.normal.xyz)
		);

	// Can save an instruction here by instead doing
	// normalize(mul(normal, TBN)); It works because
	// TBN is a 3x3 and therefore TBN^T is the same
	// as TBN^-1. However, it is considered good form
	// to do this.
	TBN = transpose(TBN);
	float3 pixelNormal = normalize(mul(TBN, normal));

	// TGA Channel Pack. ORM.
	// Metalness, Roughness, Emissive, Emissive Strength (opt).

    float3 fx = fxTexture.Sample(defaultSampler, scaledUV).rgb;

    float emissive = fx.r;
	


    float3 ambiance = AmbientLightColor.rgb * EvaluateAmbianceLambert(
		environmentTexture, pixelNormal, albedo.rgb
	);

	float3 directionalLight;


	if (DirectionalLightSoftness == 0.f)
	{
        directionalLight = EvaluateDirectionalLightLambert(
			albedo.rgb, pixelNormal,
			DirectionalLightColor.xyz, GetSunDirectionToLight());
    }
	
	else 
	{
        directionalLight = EvaluateSoftDirectionalLightLambert(
			albedo.rgb, pixelNormal, DirectionalLightSoftness,
			DirectionalLightColor.xyz, GetSunDirectionToLight());
	}

	
	float3 pointLights = 0; // <- The sum of all point lights.
	for(unsigned int p = 0; p < NumberOfLights; p++)
	{
		if (PointLights[p].radius == 0.f)
		{
            pointLights += EvaluatePointLightLambert(
				albedo.rgb, pixelNormal,
				PointLights[p].color.rgb, PointLights[p].range, PointLights[p].position.xyz,
				input.worldPosition.xyz);
		}
		else
		{
			pointLights += EvaluateSoftAreaLightLambert(
				albedo.rgb, pixelNormal,
				PointLights[p].color.rgb, PointLights[p].radius, PointLights[p].range, PointLights[p].position.xyz,
				input.worldPosition.xyz);
		}
    }
	
	float3 emissiveAlbedo = albedo.rgb * emissive;
	float3 radiance = directionalLight + ambiance + pointLights + emissiveAlbedo;

    result.color.rgb = (float3) radiance;
	result.color.a = albedo.a;
	return result;
}

