// Depth of field, pass 1 of 3: half-resolution colour with a signed circle of
// confusion packed in alpha.
//
// CoC is signed on purpose. Negative is the NEAR field (nearer than focus),
// positive is the FAR field. The gather pass needs the distinction because the
// two behave differently: far blur is occluded by anything sharp in front of
// it, while near blur spreads OVER whatever is behind it. Losing the sign is
// what makes a depth of field look like a depth-dependent smudge rather than
// like a lens.
#include "PostFxCommon.hlsli"

Texture2D Src : register(t0);
Texture2D<float> SrcDepth : register(t1);

float LinearZ(float deviceDepth)
{
	return gDofNear * gDofFar / max(gDofFar - deviceDepth * (gDofFar - gDofNear), 1e-6f);
}

// Signed CoC radius in FULL-RESOLUTION pixels.
//
// Thin lens: the blur diameter on the sensor is proportional to (z - zf) / z,
// with the constant folded into gDofCocScale on the CPU because it is all
// camera geometry -- focal length squared over f-number over focus distance,
// times pixels per millimetre of sensor. Keeping it as one scalar means the
// shader has no opinion about sensor size.
float SignedCoc(float deviceDepth)
{
	// The sky is at infinity, not at the far plane; treat it as fully defocused
	// in the far direction rather than letting a finite far plane decide.
	if (deviceDepth >= 0.999999f) return gDofMaxRadius;
	// Both in engine units (centimetres); see DeferredRendererPostFx.cpp.
	const float z = LinearZ(deviceDepth);
	return clamp(gDofCocScale * (z - gDofFocusDistance) / max(z, 1e-4f), -gDofMaxRadius, gDofMaxRadius);
}

float4 main(FsIn i) : SV_TARGET
{
	// Four full-resolution taps per half-resolution output. The colour is a
	// plain average, but the CoC takes the one with the LARGEST magnitude
	// rather than the average: a half-res texel straddling a silhouette should
	// inherit the blur of the defocused side, because averaging a sharp CoC
	// with a blurred one produces a halo of half-blurred pixels along every
	// edge in the frame.
	const float2 o = gTexelSize * 0.5f;
	float3 colour = 0.0f;
	float coc = 0.0f;
	[unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x)
	{
		const float2 uv = i.uv + float2(x * 2 - 1, y * 2 - 1) * o;
		colour += Src.SampleLevel(LinearClamp, uv, 0).rgb;
		const float c = SignedCoc(SrcDepth.SampleLevel(LinearClamp, uv, 0));
		if (abs(c) > abs(coc)) coc = c;
	}
	return float4(colour * 0.25f, coc);
}
