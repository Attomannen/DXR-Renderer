#include "PostprocessStructs.hlsli"
#include "Common.hlsli"

// ACES filmic tonemap (Narkowicz curve fit wrapped in fixed input/output 3x3
// color-space matrices). This file has no exposure cbuffer wired up, so it
// tonemaps raw, un-exposed linear HDR directly -- this used to call an
// AgX-based approximation whose log2 encoding
// (clamp(log2(color)*0.18+0.38, 0, 1)) clips any linear value below ~0.23 to
// exactly 0 -- not a deep-shadow value, an ordinary mid-tone. With no
// exposure step ahead of it, that made every caller of this shader (the
// editor viewport's no-postfx fallback composite included) render solid
// black for any realistically-lit PBR scene, regardless of whether the
// actual lighting was correct. This curve has no hard floor. Recovered by
// disassembling a known-good reference build's compiled DeferredCompositePS
// shader (fxc /dumpbin); see PostFxCommon.hlsli's AcesTonemap for the full
// story. These are the literal float immediates from that binary.
float3 AcesTonemap(float3 color)
{
    float3 x;
    x.x = dot(color, float3(0.645679, 0.259115, 0.095206));
    x.y = dot(color, float3(0.087530, 0.759700, 0.152770));
    x.z = dot(color, float3(0.036957, 0.129281, 0.833762));

    const float3 a = x * 2.51 + 0.03;
    const float3 c = x * 2.43 + 0.59;
    const float3 s = saturate((x * a) / (x * c + 0.14));

    float3 result;
    result.x = saturate(dot(float3(1.626947, -0.540139, -0.086809), s));
    result.y = saturate(dot(float3(-0.178516, 1.417941, -0.239425), s));
    result.z = saturate(dot(float3(-0.044436, -0.195920, 1.240356), s));
    return result;
}

PostProcessPixelOutput main(PostProcessVertexToPixel input)
{
    PostProcessPixelOutput returnValue;
	float3 resource = FullscreenTexture1.Sample(DefaultSampler, input.uv.xy).rgb;

	returnValue.color.rgb = AcesTonemap(resource);

	returnValue.color.a = 1.0f;
	return returnValue;
}