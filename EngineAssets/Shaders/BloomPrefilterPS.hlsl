// HDR -> soft-knee thresholded bright pass (into a half-res target).
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);

float3 main(FsIn i) : SV_TARGET
{
	float3 c = Src.SampleLevel(LinearClamp, i.uv, 0).rgb;

	// Sanitize before thresholding: a single NaN / Inf / >65k firefly texel here
	// would divide to NaN below and then smear across the whole mip chain as a
	// black block once it reaches the composite. Kill NaN, clamp the rest.
	c = (c == c) ? c : (float3)0.0f;          // NaN -> 0 (componentwise)
	c = clamp(c, 0.0f, 60000.0f);            // Inf / overbright -> finite

	float br = max(c.r, max(c.g, c.b));

	// Soft knee curve around gBloomThreshold.
	float knee = gBloomThreshold * gBloomKnee + 1e-5f;
	float soft = clamp(br - gBloomThreshold + knee, 0.0f, 2.0f * knee);
	soft = soft * soft / (4.0f * knee);
	float contrib = max(soft, br - gBloomThreshold) / max(br, 1e-5f);
	return c * saturate(contrib);
}
