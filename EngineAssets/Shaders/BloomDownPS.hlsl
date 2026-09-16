// 13-tap downsample (Call of Duty / Sledgehammer bloom). gTexelSize = 1/src size.
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);

float3 main(FsIn i) : SV_TARGET
{
	float2 t = gTexelSize;
	float3 a = Src.SampleLevel(LinearClamp, i.uv + t * float2(-2, -2), 0).rgb;
	float3 b = Src.SampleLevel(LinearClamp, i.uv + t * float2( 0, -2), 0).rgb;
	float3 c = Src.SampleLevel(LinearClamp, i.uv + t * float2( 2, -2), 0).rgb;
	float3 d = Src.SampleLevel(LinearClamp, i.uv + t * float2(-2,  0), 0).rgb;
	float3 e = Src.SampleLevel(LinearClamp, i.uv,                       0).rgb;
	float3 f = Src.SampleLevel(LinearClamp, i.uv + t * float2( 2,  0), 0).rgb;
	float3 g = Src.SampleLevel(LinearClamp, i.uv + t * float2(-2,  2), 0).rgb;
	float3 h = Src.SampleLevel(LinearClamp, i.uv + t * float2( 0,  2), 0).rgb;
	float3 ii= Src.SampleLevel(LinearClamp, i.uv + t * float2( 2,  2), 0).rgb;
	float3 j = Src.SampleLevel(LinearClamp, i.uv + t * float2(-1, -1), 0).rgb;
	float3 k = Src.SampleLevel(LinearClamp, i.uv + t * float2( 1, -1), 0).rgb;
	float3 l = Src.SampleLevel(LinearClamp, i.uv + t * float2(-1,  1), 0).rgb;
	float3 m = Src.SampleLevel(LinearClamp, i.uv + t * float2( 1,  1), 0).rgb;

	float3 o = e * 0.125f;
	o += (a + c + g + ii) * 0.03125f;
	o += (b + d + f + h) * 0.0625f;
	o += (j + k + l + m) * 0.125f;
	o = (o == o) ? o : (float3)0.0f;          // NaN guard
	return clamp(o, 0.0f, 60000.0f);
}
