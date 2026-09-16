// Screen-space ambient occlusion. Fullscreen pass after the G-buffer, before the
// lighting resolve. Reads world normal (t11) + depth (t14), writes single-channel
// occlusion (1 = open, 0 = fully occluded). View-space hemisphere kernel, rotated
// per pixel by a hash to trade banding for noise (SSAOBlurPS cleans it up).

Texture2D GBufferNormal : register(t11);
Texture2D GBufferDepth  : register(t14);
SamplerState PointSampler : register(s1);

cbuffer SsaoParams : register(b8)
{
	float4x4 ProjectionToView;   // clip -> view (inverse projection)
	float4x4 ViewToProjection;   // view  -> clip
	float4x4 WorldToView;
	float2 gSsaoScreen;          // pixels
	float  gSsaoRadius;          // world units
	float  gSsaoBias;            // view-Z bias to avoid self-occlusion
	float  gSsaoIntensity;
	float  gSsaoPower;
	float2 _ssaoPad;
};

struct FSInput { float4 position : SV_POSITION; float2 uv : UV; };

// 16 hemisphere directions (z >= 0), roughly cosine-ish, length-varied.
static const float3 kKernel[16] = {
	float3( 0.048,  0.017,  0.031), float3(-0.061, -0.041,  0.088),
	float3( 0.098, -0.079,  0.132), float3(-0.031,  0.116,  0.061),
	float3( 0.170,  0.043,  0.099), float3(-0.132, -0.170,  0.045),
	float3( 0.021,  0.099, -0.000), float3(-0.223,  0.106,  0.212),
	float3( 0.148, -0.234,  0.281), float3( 0.271,  0.187,  0.111),
	float3(-0.302, -0.054,  0.360), float3( 0.093,  0.354,  0.229),
	float3(-0.397,  0.221,  0.143), float3( 0.331, -0.331,  0.456),
	float3( 0.116,  0.155,  0.640), float3(-0.492, -0.470,  0.230),
};

float3 ViewPosFromDepth(float2 uv, float depth)
{
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	float4 v = mul(ProjectionToView, float4(ndc, depth, 1.0f));
	return v.xyz / v.w;
}

float Hash12(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * 0.1031f);
	p3 += dot(p3, p3.yzx + 33.33f);
	return frac((p3.x + p3.y) * p3.z);
}

float main(FSInput input) : SV_TARGET
{
	float depth = GBufferDepth.Sample(PointSampler, input.uv).r;
	if (depth >= 1.0f) return 1.0f;   // sky

	float3 viewPos = ViewPosFromDepth(input.uv, depth);
	float3 worldN  = normalize(GBufferNormal.Sample(PointSampler, input.uv).xyz);
	float3 viewN   = normalize(mul(WorldToView, float4(worldN, 0.0f)).xyz);

	// Per-pixel rotation vector -> TBN in view space.
	float a = Hash12(input.position.xy) * 6.2831853f;
	float3 randDir = float3(cos(a), sin(a), 0.0f);
	float3 tangent = normalize(randDir - viewN * dot(randDir, viewN));
	float3 bitangent = cross(viewN, tangent);
	float3x3 TBN = float3x3(tangent, bitangent, viewN);

	float occlusion = 0.0f;
	[unroll]
	for (int i = 0; i < 16; ++i)
	{
		float3 samplePos = viewPos + mul(kKernel[i], TBN) * gSsaoRadius;

		float4 clip = mul(ViewToProjection, float4(samplePos, 1.0f));
		clip.xyz /= clip.w;
		float2 sUv = float2(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f);
		if (sUv.x < 0 || sUv.x > 1 || sUv.y < 0 || sUv.y > 1) continue;

		float sDepth = GBufferDepth.Sample(PointSampler, sUv).r;
		float sceneZ = ViewPosFromDepth(sUv, sDepth).z;

		// Occluded if real geometry at that pixel is closer than our sample point.
		float rangeCheck = smoothstep(0.0f, 1.0f, gSsaoRadius / max(abs(viewPos.z - sceneZ), 1e-4f));
		occlusion += (sceneZ <= samplePos.z - gSsaoBias ? 1.0f : 0.0f) * rangeCheck;
	}

	float ao = 1.0f - (occlusion / 16.0f) * gSsaoIntensity;
	return pow(saturate(ao), gSsaoPower);
}
