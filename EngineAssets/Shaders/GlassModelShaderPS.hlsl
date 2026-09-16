#include "Common.hlsli"
#include "PBRFunctions.hlsli"

// t5 is a copy of opaque HDR made before this forward pass.  Sampling it is
// legal on both D3D11 and D3D12; sampling the active HDR render target is not.
Texture2D opaqueSceneTexture : register(t5);
Texture2D<float> opaqueDepthTexture : register(t6);

PixelOutput main(ModelVertexToPixel input)
{
	PixelOutput result;
	const float2 uv = input.texCoord0;
	const float4 albedo = albedoTexture.Sample(defaultSampler, uv);
	const float opacity = saturate(albedo.a);
	if (opacity <= AlphaTestThreshold) discard;

	float3 tangentNormal = normalTexture.Sample(defaultSampler, uv).xyy;
	tangentNormal.xy = tangentNormal.xy * 2.0f - 1.0f;
	tangentNormal.z = sqrt(1.0f - saturate(dot(tangentNormal.xy, tangentNormal.xy)));
	float3x3 tbn = float3x3(normalize(input.tangent.xyz),
		normalize(-input.binormal.xyz), normalize(input.normal.xyz));
	const float3 normal = normalize(mul(transpose(tbn), tangentNormal));
	const float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);
	const float roughness = saturate(materialTexture.Sample(defaultSampler, uv).g);

	uint width, height;
	opaqueSceneTexture.GetDimensions(width, height);
	const float2 screenUv = (input.position.xy + 0.5f) / float2(width, height);
	const float ior = max(CustomShaderParameters.x, 1.001f);
	const float eta = 1.0f / ior;
	const float3 refracted = refract(-toEye, normal, eta);
	// This is a screen-space approximation of thickness.  Refraction weakens on
	// rough glass, where the background is no longer a reliable single ray.
	const float strength = CustomShaderParameters.y * (1.0f - roughness * 0.65f);
	float2 refractedUv = saturate(screenUv + refracted.xy * strength * 0.035f);

	// Do not pull a foreground opaque pixel through the glass.  This is the
	// standard-depth convention used by the deferred depth buffer (near = 0).
	const float refractedDepth = opaqueDepthTexture.SampleLevel(defaultSampler, refractedUv, 0);
	if (refractedDepth < input.position.z - 0.00025f)
		refractedUv = screenUv;

	const float3 scene = opaqueSceneTexture.SampleLevel(defaultSampler, refractedUv, 0).rgb;
	const float thicknessMetres = max(CustomShaderParameters.z, 0.0f) * 0.01f;
	const float absorption = max(CustomShaderParameters.w, 0.0f);
	const float3 transmittance = exp(-absorption * thicknessMetres * max(1.0f - albedo.rgb, 0.02f));
	const float f0 = pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
	const float fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(normal, toEye)), 5.0f);
	const float3 reflected = environmentTexture.SampleLevel(defaultSampler,
		reflect(-toEye, normal), roughness * max(0, GetNumMips(environmentTexture) - 1)).rgb;

	// Standard alpha blending performs: source * opacity + opaque * (1-opacity).
	// Divide only the additive reflection by opacity so the final blend remains
	// physically ordered without switching the whole transparent pass to a
	// premultiplied blend state.
	result.color.rgb = scene * transmittance * (1.0f - fresnel) + reflected * fresnel / max(opacity, 0.05f);
	result.color.a = opacity;
	return result;
}
