#include "SkyAtmosphereCommon.hlsli"

// Renders one face of the base sky cubemap by sampling the sky-view LUT
// along each pixel's view ray, adding the sun disk, and blending in the
// authored "night sky" cubemap (stars) as the sun drops below the horizon.
// This cubemap is the ONLY new thing fed into the engine's existing IBL
// pipeline -- CubemapPrefilter::GeneratePrefilteredCubemap and everything
// downstream of it (raster ambient, DXR environment, GI environment tint,
// EnvironmentAverageCS) already work unmodified once this is bound as the
// source cubemap, exactly the way the reflection probe capture is today.

cbuffer SkyCubemapFaceCb : register(b12)
{
	uint gFaceIndex;
	float3 _cubeFacePad;
};

Texture2D<float4> SkyViewLut : register(t2);
TextureCube<float4> NightSkyCube : register(t3);
SamplerState LutSampler : register(s0);
SamplerState CubeSampler : register(s1);

struct FsIn { float4 position : SV_POSITION; float2 uv : UV; };

// Standard D3D cubemap face texel -> direction mapping.
float3 FaceDirection(uint face, float2 uv)
{
	float sc = uv.x * 2.0f - 1.0f;
	float tc = uv.y * 2.0f - 1.0f;
	switch (face)
	{
		case 0: return float3(1.0f, -tc, -sc);   // +X
		case 1: return float3(-1.0f, -tc, sc);   // -X
		case 2: return float3(sc, 1.0f, tc);     // +Y
		case 3: return float3(sc, -1.0f, -tc);   // -Y
		case 4: return float3(sc, -tc, 1.0f);    // +Z
		default: return float3(-sc, -tc, -1.0f); // -Z
	}
}

float4 main(FsIn input) : SV_TARGET
{
	float3 dir = normalize(FaceDirection(gFaceIndex, input.uv));

	float2 lutUv = SkyViewDirToUv(dir);
	float3 sky = SkyViewLut.SampleLevel(LutSampler, lutUv, 0).rgb;

	// Analytic sun disk, same angular size/intensity the volumetric composite
	// draws (AtmosphereCommon.hlsli / AtmosphereCompositePS.hlsl) so the disk
	// looks identical whether you're looking at bare sky or through fog.
	float cosSun = dot(dir, gSunDirToLight);
	float cosDiskEdge = cos(gSunAngularRadius);
	float3 originHere = float3(0.0f, gBottomRadius + gCameraHeight, 0.0f);
	if (cosSun > cosDiskEdge && !HitsGround(originHere, dir))
		sky += gSunIlluminance * gSunDiskIntensity;

	// Night sky: fades in as the sun drops toward and below the horizon, so
	// stars never pop against a still-lit blue sky.
	float nightBlend = saturate(-gSunDirToLight.y * 4.0f + 0.15f);
	if (nightBlend > 0.0f)
		sky += NightSkyCube.SampleLevel(CubeSampler, dir, 0).rgb * nightBlend;

	return float4(max(sky, 0.0f), 1.0f);
}
