// One axis of a separable Gaussian blur over a bloom mip.
//
// Run twice per mip, horizontally then vertically, with gBloomBlurDir set to
// (1,0) then (0,1). Because each mip is half the size of the one above it, the
// same kernel in texels is twice the radius in screen space at every level --
// which is the whole point: the mips end up as a family of Gaussians whose
// radii double, and the combine pass sums them with independent weights.
//
// The previous bloom had no blur pass at all. It relied on a repeated 9-tap
// tent during upsampling, which does converge toward a Gaussian but only with
// a shape the cascade happens to produce. Blurring explicitly makes the radius
// and the weighting separate controls.
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);

// 9 taps (a 17-texel span) via linear-sampled pairs.
//
// The offsets and weights below are the standard trick: sampling between two
// texels with the right sub-texel offset returns their weighted average for
// free, so 5 fetches cover 9 kernel entries. Weights are for sigma ~= 2.4
// texels, normalised to sum to 1.
static const int   kTaps = 5;
static const float kOffset[kTaps] = { 0.0f, 1.3846153846f, 3.2307692308f, 5.1764705882f, 7.1304347826f };
static const float kWeight[kTaps] = { 0.2270270270f, 0.3162162162f, 0.0702702703f, 0.0093918919f, 0.0005945946f };

float3 main(FsIn i) : SV_TARGET
{
	// gTexelSize is 1 / this mip's own size, so the step is in this mip's texels.
	const float2 step = gBloomBlurDir * gTexelSize * max(gBloomRadius, 0.0f);

	float3 sum = Src.SampleLevel(LinearClamp, i.uv, 0).rgb * kWeight[0];
	float  wsum = kWeight[0];

	[unroll] for (int t = 1; t < kTaps; ++t)
	{
		const float2 o = step * kOffset[t];
		sum += Src.SampleLevel(LinearClamp, i.uv + o, 0).rgb * kWeight[t];
		sum += Src.SampleLevel(LinearClamp, i.uv - o, 0).rgb * kWeight[t];
		wsum += 2.0f * kWeight[t];
	}

	sum /= max(wsum, 1e-5f);

	// Same guards as the rest of the chain: the HDR source can carry NaN from a
	// degenerate ray and Inf from an overbright emissive, and either one poisons
	// every mip below it once it enters the blur.
	sum = (sum == sum) ? sum : (float3)0.0f;
	return clamp(sum, 0.0f, 60000.0f);
}
