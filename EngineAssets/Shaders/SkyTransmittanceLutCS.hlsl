#include "SkyAtmosphereCommon.hlsli"

// Transmittance LUT: for each (view height, view-zenith angle) texel, the
// fraction of light that survives a straight path out to the top of the
// atmosphere -- pure extinction integral, no scattering. Every other pass
// (multi-scatter, sky-view, the ground-truth sun-visibility test) samples
// this instead of re-marching extinction from scratch.

RWTexture2D<float4> TransmittanceLutOut : register(u0);

static const uint kSteps = 40;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint w, h;
	TransmittanceLutOut.GetDimensions(w, h);
	if (id.x >= w || id.y >= h) return;

	float2 uv = (float2(id.xy) + 0.5f) / float2(w, h);
	float viewHeight, viewZenithCosAngle;
	UvToLutTransmittanceParams(uv, viewHeight, viewZenithCosAngle);

	float3 origin = float3(0.0f, viewHeight, 0.0f);
	float3 dir = float3(sqrt(saturate(1.0f - viewZenithCosAngle * viewZenithCosAngle)), viewZenithCosAngle, 0.0f);

	float tMax = AtmosphereMarchDistance(origin, 0.0f, dir);

	float3 opticalDepth = 0.0f;
	float dt = tMax / kSteps;
	for (uint s = 0; s < kSteps; ++s)
	{
		float3 samplePos = origin + dir * ((s + 0.5f) * dt);
		float sampleHeight = length(samplePos) - gBottomRadius;
		float3 rayleighScatter, mieScatter, extinction;
		SampleAtmosphereMedium(sampleHeight, rayleighScatter, mieScatter, extinction);
		opticalDepth += extinction * dt;
	}

	TransmittanceLutOut[id.xy] = float4(exp(-opticalDepth), 1.0f);
}
