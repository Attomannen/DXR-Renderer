// Box-average downsample of a single-channel log-luma target.
// Run repeatedly (each pass halves size) until 1x1.
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);

float main(FsIn i) : SV_TARGET
{
	float2 o = gTexelSize * 0.5f;
	float s = 0.0f;
	s += Src.SampleLevel(LinearClamp, i.uv + float2(-o.x, -o.y), 0).r;
	s += Src.SampleLevel(LinearClamp, i.uv + float2( o.x, -o.y), 0).r;
	s += Src.SampleLevel(LinearClamp, i.uv + float2(-o.x,  o.y), 0).r;
	s += Src.SampleLevel(LinearClamp, i.uv + float2( o.x,  o.y), 0).r;
	return s * 0.25f;
}
