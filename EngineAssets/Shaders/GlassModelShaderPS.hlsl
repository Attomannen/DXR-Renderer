#include "Common.hlsli"
#include "PBRFunctions.hlsli"
#include "MaterialSurface.hlsli"
#include "Exposure.hlsli"

// Thin glass for the forward transparent pass (SHADING_MODEL_GLASS).
// IOR, thickness, absorption and refraction strength come from the material.

// t9 is a copy of opaque HDR made before this forward pass.  Sampling it is
// legal on both D3D11 and D3D12; sampling the active HDR render target is not.
Texture2D opaqueSceneTexture : register(t9);
Texture2D<float> opaqueDepthTexture : register(t6);
Texture2D<float> glassPreviousEv100 : register(t7);   // bound when CustomShaderParameters.x = 1

PixelOutput main(ModelVertexToPixel input)
{
	PixelOutput result;
	const MaterialParams m = gMaterial;
	const MaterialSurface surface = SampleMaterialSurface(input);
	if (surface.coverage <= m.alphaCutoff) discard;
	const float opacity = saturate(surface.opacity);
	// The DXR renderer's HDR is pre-exposed; terms added here must match it.
	const float preExposure = CustomShaderParameters.x > 0.5f
		? PreExposureFromEv100(glassPreviousEv100.Load(int3(0, 0, 0))) : 1.0f;

	const float3 normal = surface.normal;
	const float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);
	const float roughness = surface.roughness;

	uint width, height;
	opaqueSceneTexture.GetDimensions(width, height);
	const float2 screenUv = (input.position.xy + 0.5f) / float2(width, height);
	const float ior = max(m.ior, 1.001f);
	const float3 refracted = refract(-toEye, normal, 1.0f / ior);
	// Screen-space approximation of thickness. Refraction weakens on rough
	// glass, where the background is no longer a reliable single ray.
	const float strength = m.refractionScale * (1.0f - roughness * 0.65f);
	float2 refractedUv = saturate(screenUv + refracted.xy * strength * 0.035f);

	// Do not pull a foreground opaque pixel through the glass.  This is the
	// standard-depth convention used by the deferred depth buffer (near = 0).
	const float refractedDepth = opaqueDepthTexture.SampleLevel(defaultSampler, refractedUv, 0);
	if (refractedDepth < input.position.z - 0.00025f)
		refractedUv = screenUv;

	// Rough glass: average a small disc around the refracted sample.
	float3 scene = 0.0f;
	const float blurRadius = roughness * roughness * 0.04f;
	static const float2 kDisc[8] = { float2(1, 0), float2(0.707, 0.707), float2(0, 1), float2(-0.707, 0.707),
		float2(-1, 0), float2(-0.707, -0.707), float2(0, -1), float2(0.707, -0.707) };
	if (blurRadius > 0.0005f)
	{
		scene = opaqueSceneTexture.SampleLevel(defaultSampler, refractedUv, 0).rgb;
		for (uint i = 0; i < 8; ++i)
		{
			const float ring = (i & 1u) ? 0.5f : 1.0f;
			scene += opaqueSceneTexture.SampleLevel(defaultSampler, saturate(refractedUv + kDisc[i] * blurRadius * ring * float2(float(height) / float(width), 1.0f)), 0).rgb;
		}
		scene /= 9.0f;
	}
	else
		scene = opaqueSceneTexture.SampleLevel(defaultSampler, refractedUv, 0).rgb;
	const float thicknessMetres = max(m.thicknessCm, 0.0f) * 0.01f;
	const float3 transmittance = exp(-max(m.absorption, 0.0f) * thicknessMetres
		* max(1.0f - surface.baseColor, 0.02f));
	const float f0 = pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
	const float fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(normal, toEye)), 5.0f);
	const float3 reflected = environmentTexture.SampleLevel(defaultSampler,
		reflect(-toEye, normal), roughness * max(0, GetNumMips(environmentTexture) - 1)).rgb
		* (CustomShaderParameters.x > 0.5f ? CustomShaderParameters.yzw : 1.0f);   // DXR environment tint

	// Standard alpha blending performs: source * opacity + opaque * (1-opacity).
	// Divide only the additive reflection by opacity so the final blend remains
	// physically ordered without switching the whole transparent pass to a
	// premultiplied blend state.
	result.color.rgb = scene * transmittance * (1.0f - fresnel)
		+ (reflected * fresnel + surface.emissive) * preExposure / max(opacity, 0.05f);
	result.color.a = opacity;
	return result;
}
