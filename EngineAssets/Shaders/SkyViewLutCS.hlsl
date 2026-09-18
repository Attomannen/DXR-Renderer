#include "SkyAtmosphereCommon.hlsli"

// Sky-view LUT: the actual visible sky radiance per view direction from the
// camera's current height, for the current sun direction. Ray-marches
// single scattering (Rayleigh + Mie, correctly phase-weighted by the angle
// to the sun -- this is what produces the red/orange sunset gradient, not
// just a color ramp) and adds in the multi-scatter LUT's isotropic term.
// Cheap enough (single march per texel, small LUT) to recompute every time
// the sun moves.

RWTexture2D<float4> SkyViewLutOut : register(u0);
Texture2D<float4> TransmittanceLut : register(t0);
Texture2D<float4> MultiScatterLut : register(t1);
SamplerState LutSampler : register(s0);

static const uint kSteps = 32;

float3 SampleMultiScatterLut(float viewHeight, float sunCosZenith)
{
	float2 uv = float2(sunCosZenith * 0.5f + 0.5f, saturate((viewHeight - gBottomRadius) / (gTopRadius - gBottomRadius)));
	return MultiScatterLut.SampleLevel(LutSampler, uv, 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= gSkyViewLutWidth || id.y >= gSkyViewLutHeight) return;

	float2 uv = (float2(id.xy) + 0.5f) / float2(gSkyViewLutWidth, gSkyViewLutHeight);

	// Azimuth is stored relative to the sun so the LUT doesn't need to be
	// regenerated on yaw alone when the sun hasn't moved in that frame --
	// SkyViewUvToDir gives a world direction once rotated by the sun's own
	// azimuth at sample time (done here, once, per texel).
	float3 localDir = SkyViewUvToDir(uv);
	float sunAzimuth = atan2(gSunDirToLight.z, gSunDirToLight.x);
	float cosA = cos(sunAzimuth), sinA = sin(sunAzimuth);
	float3 dir = float3(localDir.x * cosA - localDir.z * sinA, localDir.y, localDir.x * sinA + localDir.z * cosA);

	float3 origin = float3(0.0f, gBottomRadius + gCameraHeight, 0.0f);
	bool groundHit = HitsGround(origin, dir);
	float tMax = AtmosphereMarchDistance(origin, 0.0f, dir);

	float cosSunView = dot(dir, gSunDirToLight);
	float rayleighPhase = RayleighPhase(cosSunView);
	float miePhase = MiePhase(gMiePhaseG, cosSunView);

	float3 throughput = 1.0f;
	float3 radiance = 0.0f;
	float dt = tMax / kSteps;
	for (uint s = 0; s < kSteps; ++s)
	{
		float3 samplePos = origin + dir * ((s + 0.5f) * dt);
		float sampleHeight = length(samplePos) - gBottomRadius;
		float3 rayleighScatter, mieScatter, extinction;
		SampleAtmosphereMedium(sampleHeight, rayleighScatter, mieScatter, extinction);
		float3 segTransmittance = exp(-extinction * dt);

		float sunCosZenithHere = dot(samplePos / max(length(samplePos), 1.0f), gSunDirToLight);
		float3 sunT = SunTransmittance(TransmittanceLut, LutSampler, samplePos, gSunDirToLight);

		// Single scattering, correctly phase-weighted toward the sun.
		float3 singleScatter = sunT * (rayleighScatter * rayleighPhase + mieScatter * miePhase);
		// Multi-scattering: isotropic, so no phase weighting, but still needs
		// the LUT's own sun-visibility-independent estimate at this height.
		float3 msScatter = SampleMultiScatterLut(length(samplePos), sunCosZenithHere) * (rayleighScatter + mieScatter);

		radiance += throughput * (singleScatter + msScatter) * dt;
		throughput *= segTransmittance;
	}

	if (groundHit)
	{
		float3 groundPos = origin + dir * tMax;
		float3 up = normalize(groundPos);
		float sunCosAtGround = saturate(dot(up, gSunDirToLight));
		float3 sunT = SunTransmittance(TransmittanceLut, LutSampler, groundPos, gSunDirToLight);
		radiance += throughput * sunT * sunCosAtGround * (gGroundAlbedo / kSkyPi);
	}

	SkyViewLutOut[id.xy] = float4(radiance * gSunIlluminance, 1.0f);
}
