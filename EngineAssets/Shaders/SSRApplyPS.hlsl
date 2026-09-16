// SSR-over-probe resolve. Blends the screen-space reflection over the probe's IBL
// specular by SSR confidence, then this is additively blended into HDR:
//   hdr += conf * (ssrRadiance - probeIblSpecular)   ==   lerp(probe, ssr, conf)
struct FsIn { float4 position : SV_POSITION; float2 uv : UV; };

Texture2D    Ssr         : register(t0);   // HALF-RES rgb = reflected radiance * BRDF, a = confidence
Texture2D    IblSpec     : register(t1);   // full-res rgb = probe IBL specular already in HDR
SamplerState PointClamp  : register(s1);
SamplerState LinearClamp : register(s3);

float4 main(FsIn i) : SV_TARGET
{
	float4 s   = Ssr.Sample(LinearClamp, i.uv);   // bilinear upsample from half res
	float3 ibl = IblSpec.Sample(PointClamp, i.uv).rgb;
	float3 delta = s.a * (s.rgb - ibl);
	return float4(delta, 1.0f);
}
