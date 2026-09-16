// Depth-aware box blur for SSAO. Reads raw AO (t18) + depth (t14), writes the
// smoothed AO. 4x4 window, weights drop where linear depth differs (edge-stop).

Texture2D SsaoRaw      : register(t18);
Texture2D GBufferDepth : register(t14);
SamplerState PointSampler : register(s1);

cbuffer SsaoBlurParams : register(b8)
{
	float2 gBlurTexelSize;   // 1 / screen
	float  gBlurDepthSigma;  // NDC-depth difference tolerance
	float  _blurPad;
};

struct FSInput { float4 position : SV_POSITION; float2 uv : UV; };

float main(FSInput input) : SV_TARGET
{
	float centerDepth = GBufferDepth.Sample(PointSampler, input.uv).r;

	float sum = 0.0f;
	float wsum = 0.0f;
	[unroll]
	for (int y = -3; y <= 3; ++y)
	[unroll]
	for (int x = -3; x <= 3; ++x)
	{
		float2 uv = input.uv + float2(x, y) * gBlurTexelSize;
		float d = GBufferDepth.Sample(PointSampler, uv).r;
		float w = exp(-abs(d - centerDepth) / max(gBlurDepthSigma, 1e-6f));
		sum  += SsaoRaw.Sample(PointSampler, uv).r * w;
		wsum += w;
	}
	return wsum > 0.0f ? sum / wsum : SsaoRaw.Sample(PointSampler, input.uv).r;
}
