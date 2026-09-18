// Depth of field, pass 2 of 3: the bokeh gather, at half resolution.
//
// Gather rather than scatter. A scatter (splat each pixel's disc into the
// target) is the physically direct formulation but needs blending and ordering;
// gathering asks instead "which pixels could have spread onto me", which is the
// same integral read backwards and fits a pixel shader.
#include "PostFxCommon.hlsli"

Texture2D CocColor : register(t0);

// Golden-angle spiral: 48 samples whose positions never form rings or spokes,
// unlike a polar grid, and which fill the disc evenly at any count. A hexagonal
// or bladed aperture would go here too -- the shape of this kernel IS the shape
// of the bokeh.
static const int kSamples = 48;
static const float kGoldenAngle = 2.39996323f;

float4 main(FsIn i) : SV_TARGET
{
	const float4 center = CocColor.SampleLevel(LinearClamp, i.uv, 0);
	// CoC is in full-resolution pixels; this pass runs at half.
	const float centerRadius = abs(center.a) * 0.5f;

	// Below a pixel there is nothing to gather: the sample would land inside
	// the texel it started from.
	if (centerRadius < 0.75f) return float4(center.rgb, center.a);

	float3 farColour = center.rgb, nearColour = center.rgb;
	float farWeight = 1.0f, nearWeight = 1.0f;

	[loop] for (int s = 0; s < kSamples; ++s)
	{
		const float t = (float(s) + 0.5f) / float(kSamples);
		const float r = sqrt(t);                 // sqrt keeps the disc uniform in AREA
		const float a = float(s) * kGoldenAngle;
		const float2 offset = float2(cos(a), sin(a)) * r * centerRadius;
		const float4 tap = CocColor.SampleLevel(LinearClamp, i.uv + offset * gTexelSize, 0);
		const float tapRadius = abs(tap.a) * 0.5f;
		const float dist = r * centerRadius;

		// FAR field: a tap only contributes if its own disc is wide enough to
		// reach here. Without that test a sharp foreground object bleeds
		// outward into the blurred background behind it, which is the classic
		// depth of field artefact -- the background is defocused, so it must
		// not inherit colour from something that is in focus in front of it.
		if (tap.a >= 0.0f && tapRadius >= dist - 0.5f)
		{
			farColour += tap.rgb;
			farWeight += 1.0f;
		}

		// NEAR field: the opposite rule. Anything nearer than focus spreads
		// over what is behind it regardless of the receiver, so every near tap
		// within the disc counts. This is what lets an out-of-focus foreground
		// silhouette soften ACROSS its own edge instead of being clipped to it.
		if (tap.a < 0.0f)
		{
			nearColour += tap.rgb;
			nearWeight += 1.0f;
		}
	}

	farColour /= farWeight;
	nearColour /= nearWeight;

	// How much near field was found in the neighbourhood. Used as the blend,
	// so near blur can cover a sharp background rather than being masked by it.
	const float nearCoverage = saturate((nearWeight - 1.0f) / (float(kSamples) * 0.35f));
	const float3 result = lerp(farColour, nearColour, nearCoverage);

	// Alpha is a 0..1 BLEND WEIGHT, not a radius. Returning the radius and
	// letting the composite threshold it meant anything past about a pixel and
	// a half of blur snapped straight to fully defocused -- and since this pass
	// runs at half resolution, "fully defocused" costs real detail even where
	// the lens is barely blurring. Ramped across 1 to 4 pixels instead, so the
	// frame keeps its full-resolution sharpness until the blur is wide enough
	// that half resolution genuinely does not matter.
	const float farAmount = saturate((abs(center.a) - 1.0f) / 3.0f);
	return float4(result, max(farAmount, nearCoverage));
}
