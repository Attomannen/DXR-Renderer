#include "SkyAtmosphereCommon.hlsli"

// Multi-scattering LUT: an isotropic estimate of second-and-beyond-order
// scattered light per (view height, sun-zenith angle) texel, following
// Hillaire 2020 section 5.1. For a stratified set of directions over the
// sphere, ray-march single scattering received at each point (`L`) and the
// phase-independent scattered fraction (`f`), then close the infinite
// scattering-order series analytically: total = L + f*L + f^2*L + ... =
// L / (1 - f). This LUT is tiny and only recomputed when the sun direction
// or an atmosphere tunable changes, not every frame.

RWTexture2D<float4> MultiScatterLutOut : register(u0);
Texture2D<float4> TransmittanceLut : register(t0);
SamplerState LutSampler : register(s0);

static const uint kDirSteps = 8;      // 8x8 = 64 stratified directions over the sphere
static const uint kMarchSteps = 20;

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	float2 uv = (float2(id.xy) + 0.5f) / float2(gMultiScatterLutRes, gMultiScatterLutRes);
	float sunCosZenith = uv.x * 2.0f - 1.0f;
	float3 sunDir = float3(sqrt(saturate(1.0f - sunCosZenith * sunCosZenith)), sunCosZenith, 0.0f);
	float viewHeight = lerp(gBottomRadius, gTopRadius, uv.y);
	float3 origin = float3(0.0f, viewHeight, 0.0f);

	float3 inScattered = 0.0f;   // L: single scattering received here from every direction
	float3 scatteredFraction = 0.0f; // f: what fraction of incoming light one bounce redirects

	for (uint i = 0; i < kDirSteps; ++i)
	for (uint j = 0; j < kDirSteps; ++j)
	{
		float theta = acos(1.0f - 2.0f * (i + 0.5f) / kDirSteps);
		float phi = 2.0f * kSkyPi * (j + 0.5f) / kDirSteps;
		float sinTheta = sin(theta);
		float3 rayDir = float3(sinTheta * cos(phi), cos(theta), sinTheta * sin(phi));

		float tMax = AtmosphereMarchDistance(origin, 0.0f, rayDir);
		bool groundHit = HitsGround(origin, rayDir);

		float3 throughput = 1.0f;
		float3 L = 0.0f, f = 0.0f;
		float dt = tMax / kMarchSteps;
		for (uint s = 0; s < kMarchSteps; ++s)
		{
			float3 samplePos = origin + rayDir * ((s + 0.5f) * dt);
			float sampleHeight = length(samplePos) - gBottomRadius;
			float3 rayleighScatter, mieScatter, extinction;
			SampleAtmosphereMedium(sampleHeight, rayleighScatter, mieScatter, extinction);
			float3 scatterHere = rayleighScatter + mieScatter;   // isotropic phase folded in below
			float3 segTransmittance = exp(-extinction * dt);

			float3 sunT = SunTransmittance(TransmittanceLut, LutSampler, samplePos, sunDir);
			L += throughput * sunT * scatterHere * dt;
			f += throughput * scatterHere * dt;

			throughput *= segTransmittance;
		}

		if (groundHit)
		{
			float3 groundPos = origin + rayDir * tMax;
			float3 up = normalize(groundPos);
			float sunCosAtGround = saturate(dot(up, sunDir));
			float3 sunT = SunTransmittance(TransmittanceLut, LutSampler, groundPos, sunDir);
			L += throughput * sunT * sunCosAtGround * (gGroundAlbedo / kSkyPi);
		}

		inScattered += L;
		scatteredFraction += f;
	}

	float invSamples = 1.0f / float(kDirSteps * kDirSteps);
	float isotropicPhase = 1.0f / (4.0f * kSkyPi);
	inScattered *= invSamples * isotropicPhase;
	scatteredFraction *= invSamples * isotropicPhase;

	float3 psiMs = inScattered / max(1.0f - scatteredFraction, 0.01f);
	MultiScatterLutOut[id.xy] = float4(psiMs, 1.0f);
}
