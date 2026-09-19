// Weighted sum of the six blurred bloom mips into one half-resolution target.
//
// This replaces the additive tent cascade. The cascade could only produce one
// falloff shape, because each level's contribution was fixed by the upsample
// filter; here every radius carries its own weight, so the profile of the glow
// is authored rather than inherited.
//
// All six are sampled with LinearClamp at the destination's UV, which upsamples
// the smaller mips bilinearly. That is safe precisely because they are already
// Gaussian-blurred -- interpolation cannot invent detail a blurred image does
// not have, which is the same argument the depth-of-field composite makes.
#include "PostFxCommon.hlsli"

Texture2D Mip0 : register(t0);   // half res, tightest
Texture2D Mip1 : register(t1);
Texture2D Mip2 : register(t2);
Texture2D Mip3 : register(t3);
Texture2D Mip4 : register(t4);
// t5 is NOT free: PostFxFullscreen binds the 1x1 exposure texture there for
// every post pass. Putting a mip on t5 silently replaces it with a single
// smeared luminance value -- which reads as a flat wash over the whole frame,
// red-tinted because the exposure target is R16_Float and only carries a red
// channel. Hence the jump to t6.
Texture2D Mip5 : register(t6);   // widest

float3 main(FsIn i) : SV_TARGET
{
	// Weights arrive normalised (they sum to 1), so the combined result stays
	// in the same range as any single mip and gBloomIntensity remains the only
	// gain in the composite.
	float3 sum = 0.0f;
	sum += Mip0.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights0.x;
	sum += Mip1.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights0.y;
	sum += Mip2.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights0.z;
	sum += Mip3.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights0.w;
	sum += Mip4.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights1.x;
	sum += Mip5.SampleLevel(LinearClamp, i.uv, 0).rgb * gBloomMipWeights1.y;

	sum = (sum == sum) ? sum : (float3)0.0f;
	return clamp(sum, 0.0f, 60000.0f);
}
