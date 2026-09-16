// Project one captured probe cube into L2 SH and temporally blend it into the
// GI volume's SH buffer. One thread walks the whole (low-res) cube.
#include "GiCommon.hlsli"

TextureCube  SrcCube    : register(t0);
SamplerState LinearWrap : register(s0);

// 9 float4 per probe (rgb used, a spare). base = probeIndex * 9.
RWStructuredBuffer<float4> GiSH : register(u0);

cbuffer GiProjectParams : register(b0)
{
	uint  gProbeIndex;
	float gHysteresis;   // fraction of the previous SH to keep (0 = replace, 0.95 = smooth)
	uint  gFaceRes;      // captured cube face resolution to integrate over
	float _giPad;
};

[numthreads(1, 1, 1)]
void main()
{
	float3 sh[9];
	[unroll] for (uint k = 0; k < 9; ++k) sh[k] = 0.0f;

	const uint N = max(gFaceRes, 1u);
	const float inv = 1.0f / (float)N;

	// Sky visibility = solid-angle fraction of the hemisphere that hit open sky
	// (captured cube alpha 0 = sky, 1 = geometry). Lets the renderer fade the
	// sun / sky-IBL for probes sealed inside a room -- procedural interior detect.
	float skyOmega = 0.0f;
	float totOmega = 0.0f;

	for (uint f = 0; f < 6; ++f)
	for (uint y = 0; y < N; ++y)
	for (uint x = 0; x < N; ++x)
	{
		float2 uv = (float2(x, y) + 0.5f) * inv * 2.0f - 1.0f;
		float3 dv = CubeFaceDir(f, uv);
		float  dOmega = 4.0f / (pow(dot(dv, dv), 1.5f) * (float)(N * N));
		float3 dir = normalize(dv);

		float4 s = SrcCube.SampleLevel(LinearWrap, dir, 0);
		float b[9];
		ShBasis(dir, b);
		[unroll] for (uint i = 0; i < 9; ++i) sh[i] += s.rgb * b[i] * dOmega;

		totOmega += dOmega;
		skyOmega += dOmega * saturate(1.0f - s.a * 2.0f);   // a<0.5 -> sky
	}

	const float skyVis = totOmega > 1e-5f ? saturate(skyOmega / totOmega) : 1.0f;

	const uint base = gProbeIndex * 9;
	[unroll] for (uint c = 0; c < 9; ++c)
	{
		float3 prev = GiSH[base + c].rgb;
		float  prevW = GiSH[base + c].w;
		// Stash sky visibility in slot 0's spare .w (temporally blended like the SH).
		// Slot 0.w is sky visibility; slot 8.w marks a populated probe so
		// reconstruction can reject cleared/uninitialized black entries.
		float  w = (c == 0) ? lerp(skyVis, prevW, gHysteresis) : (c == 8 ? 1.0f : 0.0f);
		GiSH[base + c] = float4(lerp(sh[c], prev, gHysteresis), w);
	}
}
