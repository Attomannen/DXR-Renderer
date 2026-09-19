// Screen-space reflections. Fullscreen pass after the lighting resolve:
// view-space ray-march of the reflection ray against the depth buffer, sample the
// lit HDR colour on hit. Output is premultiplied reflection radiance; a following
// additive pass blends it into the HDR target. Pair with PostprocessVS.
#include "Common.hlsli"

struct FsIn { float4 position : SV_POSITION; float2 uv : UV; };

Texture2D    SceneColor    : register(t1);    // lit HDR, pre-SSR
Texture2D    GBufferAlbedo : register(t10);
Texture2D    GBufferNormal  : register(t11);
Texture2D    GBufferMaterial: register(t12);
Texture2D    GBufferDepth   : register(t14);
SamplerState PointClamp     : register(s1);
SamplerState LinearClamp    : register(s3);

cbuffer SsrParams : register(b8)
{
	float4x4 gViewToProj;
	float4x4 gProjToView;
	float4x4 gWorldToView;
	float2   gScreen;
	float    gMaxDistance;      // view-space march length, metres
	float    gThickness;        // depth-match tolerance, metres

	float    gRoughnessCutoff;  // no SSR past this roughness
	float    gStrength;         // global multiplier
	int      gSteps;            // linear march steps
	int      gRefineSteps;      // binary refine steps
};

float3 ViewPosFromDepth(float2 uv, float depth)
{
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	float4 v = mul(gProjToView, float4(ndc, depth, 1.0f));
	return v.xyz / v.w;
}

float2 ViewToUv(float3 vp, out float outW)
{
	float4 c = mul(gViewToProj, float4(vp, 1.0f));
	outW = c.w;
	float2 ndc = c.xy / c.w;
	return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

float4 main(FsIn i) : SV_TARGET
{
	float depth = GBufferDepth.Sample(PointClamp, i.uv).r;
	if (depth >= 1.0f) return 0.0f;                       // sky

	float3 orm = GBufferMaterial.Sample(PointClamp, i.uv).rgb;
	float roughness = orm.g, metalness = orm.b;
	if (roughness >= gRoughnessCutoff) return 0.0f;

	float3 viewPos = ViewPosFromDepth(i.uv, depth);
	float3 nWorld  = normalize(GBufferNormal.Sample(PointClamp, i.uv).xyz);
	float3 nView   = normalize(mul((float3x3)gWorldToView, nWorld));

	float3 V = normalize(viewPos);                        // eye -> point (view space)
	float3 R = reflect(V, nView);
	if (R.z <= 0.0f) return 0.0f;                         // reflection points back at the camera

	// --- linear march ---
	float stepLen = gMaxDistance / (float)gSteps;
	float3 p = viewPos + nView * (stepLen * 0.5f) + R * stepLen;
	float  hitW = 0.0f;
	float2 hitUv = 0.0f;
	bool   hit = false;

	[loop] for (int s = 0; s < gSteps; ++s)
	{
		float w; float2 uv = ViewToUv(p, w);
		if (w <= 0.0f || uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) break;

		float sd = GBufferDepth.SampleLevel(PointClamp, uv, 0).r;
		float sceneZ = ViewPosFromDepth(uv, sd).z;
		float delta = p.z - sceneZ;                       // >0 : ray is behind a surface
		if (delta > 0.0002f && delta < gThickness)   // 0.2 mm in metres
		{
			hit = true; hitUv = uv; hitW = w;
			// --- binary refine ---
			float3 lo = p - R * stepLen, hi = p;
			[loop] for (int r = 0; r < gRefineSteps; ++r)
			{
				float3 mid = 0.5f * (lo + hi);
				float mw; float2 muv = ViewToUv(mid, mw);
				float msd = GBufferDepth.SampleLevel(PointClamp, muv, 0).r;
				float mz = ViewPosFromDepth(muv, msd).z;
				if (mid.z - mz > 0.0f) { hi = mid; hitUv = muv; }
				else                   lo = mid;
			}
			break;
		}
		p += R * stepLen;
	}
	if (!hit) return 0.0f;

	// --- fades ---
	float3 hitVp = ViewPosFromDepth(hitUv, GBufferDepth.SampleLevel(PointClamp, hitUv, 0).r);
	float dist = length(hitVp - viewPos);
	float distFade = saturate(1.0f - dist / gMaxDistance);
	float2 e = abs(hitUv * 2.0f - 1.0f);
	float edgeFade = saturate((1.0f - max(e.x, e.y)) * 6.0f);
	float backFade = saturate(R.z * 3.0f);              // fade in as the ray heads into the scene
	float roughFade = saturate(1.0f - roughness / gRoughnessCutoff);
	float ndv = max(dot(-V, nView), 0.0f);
	float fresnel = 0.15f + 0.85f * pow(saturate(1.0f - ndv), 4.0f);

	float3 albedo = GBufferAlbedo.Sample(PointClamp, i.uv).rgb;
	float3 reflectivity = lerp((float3)0.04f, albedo, metalness);

	float3 refl = SceneColor.SampleLevel(LinearClamp, hitUv, 0).rgb;
	float w2 = distFade * edgeFade * backFade * roughFade * gStrength;
	// rgb = reflected radiance * BRDF (NOT premultiplied by confidence);
	// a = confidence. The resolve pass does hdr += a * (rgb - probeIblSpec).
	return float4(refl * reflectivity * fresnel, w2);
}
