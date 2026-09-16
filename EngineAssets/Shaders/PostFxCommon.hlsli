#ifndef POSTFX_COMMON_HLSLI
#define POSTFX_COMMON_HLSLI

struct FsIn { float4 position : SV_POSITION; float2 uv : UV; };

SamplerState LinearClamp : register(s3);

cbuffer PostFxParams : register(b10)
{
	float2 gTexelSize;       // 1 / source size for the current pass
	float  gBloomThreshold;
	float  gBloomKnee;

	float  gBloomIntensity;
	float  gExposureKeyOrManual;   // auto: target key value; manual: the exposure itself
	float  gExposureMin;
	float  gExposureMax;

	float  gExposureAuto;    // 1 = use adapted auto exposure, 0 = manual
	float  gExposureComp;    // EV bias (stops)
	float  gAdaptRate;       // per-second adaptation lerp rate
	float  gDeltaTime;
};

float Luma(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }

// ACES filmic tonemap (Narkowicz curve fit wrapped in fixed input/output 3x3
// color-space matrices). The checked-in source for this function was lost
// at some point -- DeferredCompositePS.hlsl called an AcesTonemap() with no
// definition anywhere in this tree, in both the current project and the
// known-good reference copy. Recovered by disassembling the reference
// build's compiled DeferredCompositePS.cso (fxc /dumpbin): these are the
// literal float immediates baked into that binary, not a generic ACES fit,
// so this reproduces the reference build's actual tonemap curve exactly.
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

#endif
