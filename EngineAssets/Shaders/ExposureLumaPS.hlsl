// First exposure pass: HDR colour -> log-luminance (into a half-res R16F target).
#include "PostFxCommon.hlsli"

Texture2D Hdr : register(t0);

float main(FsIn i) : SV_TARGET
{
	float2 o = gTexelSize * 0.5f;
	float3 c  = Hdr.SampleLevel(LinearClamp, i.uv + float2(-o.x, -o.y), 0).rgb;
	c += Hdr.SampleLevel(LinearClamp, i.uv + float2( o.x, -o.y), 0).rgb;
	c += Hdr.SampleLevel(LinearClamp, i.uv + float2(-o.x,  o.y), 0).rgb;
	c += Hdr.SampleLevel(LinearClamp, i.uv + float2( o.x,  o.y), 0).rgb;
	c *= 0.25f;
	float l = max(Luma(c) / HdrPreExposure(), 1e-9f);
	return log2(l);
}
