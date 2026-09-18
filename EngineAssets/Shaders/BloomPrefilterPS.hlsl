// HDR -> soft-knee thresholded bright pass (into a half-res target).
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);

float3 main(FsIn i) : SV_TARGET
{
	// Luma-weighted 2x2 box (Karis average): a single hot texel -- one emissive
	// RIS sample or one reflection ray -- gets weight 1/(1+luma), so it cannot
	// own the bloom on its own and flicker it from frame to frame.
	uint w, h; Src.GetDimensions(w, h);
	const float2 texel = 1.0f / float2(max(w, 1u), max(h, 1u));
	float3 c = 0.0f; float wsum = 0.0f;
	[unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x)
	{
		float3 t = Src.SampleLevel(LinearClamp, i.uv + (float2(x, y) - 0.5f) * texel, 0).rgb;
		t = (t == t) ? t : (float3)0.0f;          // NaN -> 0 (componentwise)
		t = clamp(t, 0.0f, 60000.0f);            // Inf / overbright -> finite
		const float lum = dot(t, float3(0.2126f, 0.7152f, 0.0722f)) / HdrPreExposure();
		const float kw = 1.0f / (1.0f + lum);
		c += t * kw; wsum += kw;
	}
	c /= max(wsum, 1e-5f);

	// Threshold in scene units, but keep the (pre-exposed) signal so a night
	// scene's bloom does not underflow the R11G11B10 mip chain.
	float br = max(c.r, max(c.g, c.b)) / HdrPreExposure();

	// Soft knee curve around gBloomThreshold.
	float knee = gBloomThreshold * gBloomKnee + 1e-5f;
	float soft = clamp(br - gBloomThreshold + knee, 0.0f, 2.0f * knee);
	soft = soft * soft / (4.0f * knee);
	float contrib = max(soft, br - gBloomThreshold) / max(br, 1e-5f);
	return c * saturate(contrib);
}
