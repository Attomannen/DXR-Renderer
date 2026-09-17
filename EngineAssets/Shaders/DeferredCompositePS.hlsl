// Final composite: HDR * exposure + bloom, then tonemap (AgX by default) -> backbuffer.
//   t0 = HDR scene target
//   t1 = bloom result (half-res, linear-sampled)
//   t2 = 1x1 adapted (metered) EV100
#include "PostFxCommon.hlsli"

Texture2D Hdr      : register(t0);
Texture2D Bloom    : register(t1);
Texture2D Exposure : register(t2);

float4 main(FsIn i) : SV_TARGET
{
	float3 hdr   = Hdr.SampleLevel(LinearClamp, i.uv, 0).rgb;
	float3 bloom = Bloom.SampleLevel(LinearClamp, i.uv, 0).rgb;

	// Scalar (not vector) ternaries: HLSL's ?: requires a scalar condition, and
	// `select()` needs a newer HLSL language version than this project's
	// runtime shader compiler is invoked with. Per-component scalar ternary
	// works on every profile/version.
	hdr   = float3(hdr.x   == hdr.x   ? hdr.x   : 0.0f,
	               hdr.y   == hdr.y   ? hdr.y   : 0.0f,
	               hdr.z   == hdr.z   ? hdr.z   : 0.0f);
	bloom = float3(bloom.x == bloom.x ? bloom.x : 0.0f,
	               bloom.y == bloom.y ? bloom.y : 0.0f,
	               bloom.z == bloom.z ? bloom.z : 0.0f);
	hdr   = clamp(hdr,   0.0f, 60000.0f);
	bloom = clamp(bloom, 0.0f, 60000.0f);

	const float ev100 = gExposureAuto > 0.5f
		? Exposure.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r
		: gManualEv100;
	const float exposure = ExposureFromEv100(ev100) / HdrPreExposure();

	// Bloom is gathered from the unexposed HDR, so expose it the same way.
	float3 color = (hdr + bloom * gBloomIntensity) * exposure;
	color = Tonemap(color);
	return float4(color, 1.0f);   // opaque: the editor shows this target through ImGui
}
