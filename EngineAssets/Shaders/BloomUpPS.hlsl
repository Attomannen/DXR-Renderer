// 9-tap tent upsample of the lower mip (t0). The result is ADDITIVELY blended
// onto the destination mip by the pipeline (blend = One, One), so this shader
// only needs to produce the filtered lower-mip contribution.
// gTexelSize = 1 / (lower-mip size).
#include "PostFxCommon.hlsli"

Texture2D LowerMip : register(t0);

float4 main(FsIn i) : SV_TARGET
{
	float2 t = gTexelSize;
	float3 s;
	s  = LowerMip.SampleLevel(LinearClamp, i.uv + t * float2(-1, -1), 0).rgb * 1.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2( 0, -1), 0).rgb * 2.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2( 1, -1), 0).rgb * 1.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2(-1,  0), 0).rgb * 2.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv,                       0).rgb * 4.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2( 1,  0), 0).rgb * 2.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2(-1,  1), 0).rgb * 1.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2( 0,  1), 0).rgb * 2.0f;
	s += LowerMip.SampleLevel(LinearClamp, i.uv + t * float2( 1,  1), 0).rgb * 1.0f;
	s *= (1.0f / 16.0f);
	s = (s == s) ? s : (float3)0.0f;          // NaN guard
	return float4(clamp(s, 0.0f, 60000.0f), 1.0f);
}
