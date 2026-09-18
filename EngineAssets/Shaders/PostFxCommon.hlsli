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
	float  gManualEv100;     // camera EV100 (aperture / shutter / ISO) for manual exposure
	float  gAutoEvMin;       // auto exposure: metered EV100 is clamped to this range
	float  gAutoEvMax;

	float  gExposureAuto;    // 1 = metered auto exposure, 0 = manual camera
	float  gExposureComp;    // EV compensation in stops (+1 = twice as bright)
	float  gAdaptRate;       // per-second adaptation rate
	float  gDeltaTime;

	uint   gTonemapper;      // 0 = AgX, 1 = AgX Punchy, 2 = ACES (fitted), 3 = none
	uint   gHdrPreExposed;   // 1 = the HDR input carries the previous frame's exposure
	float  gAdaptStrength;   // 0 = fixed camera, 1 = fully compensating meter
	float  _postFxPad2;

	// Depth of field. gDofCocScale folds all the camera geometry (focal length
	// squared, over f-number, over focus distance, times pixels per mm of
	// sensor) into one number so the shaders have no opinion about sensor size.
	float  gDofCocScale;
	float  gDofFocusDistance;   // ENGINE units (cm), matching gDofNear/gDofFar
	float  gDofMaxRadius;       // full-resolution pixels
	float  gDofEnabled;

	float  gDofNear, gDofFar;
	float2 _postFxPad3;

	// Motion blur. gMbVelocityScale converts the velocity buffer's RENDER-pixel
	// vectors into display pixels, which differ under upscaling.
	float  gMbEnabled;
	float  gMbVelocityScale;
	float  gMbTileSize;
	float  gMbMaxRadius;
};

#include "Exposure.hlsli"

// The EV100 the HDR input was pre-exposed with (bound for every post-FX pass).
Texture2D<float> gPreviousEv100 : register(t5);

float HdrPreExposure()
{
	return gHdrPreExposed != 0u ? PreExposureFromEv100(gPreviousEv100.Load(int3(0, 0, 0))) : 1.0f;
}

// EV100 a reflected-light meter reports for a scene averaging this luminance.
float MeteredEv100(float avgLuminanceUnits)
{
	return log2(max(avgLuminanceUnits, 1e-8f) * kNitsPerUnit * 100.0f / kMeterCalibration);
}

// Linear multiplier from scene units to sensor signal (1 = saturation).
float ExposureFromEv100(float ev100)
{
	return ExposureScaleFromEv100(ev100 - gExposureComp);
}

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

// AgX (Troy Sobotka), as fitted by Benjamin Wrensch for Blender 4 parity.
// Input is linear Rec.709 scene signal; output is linear display Rec.709.
float3 AgxContrast(float3 x)
{
	const float3 x2 = x * x;
	const float3 x4 = x2 * x2;
	return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

float3 AgxTonemap(float3 color, bool punchy)
{
	// GLSL column-major matrices; mul(v, M) applies them identically here.
	const float3x3 inset = float3x3(
		0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
		0.0784335999999992f, 0.878468636469772f, 0.0784336f,
		0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
	const float3x3 outset = float3x3(
		1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
		-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
		-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
	const float minEv = -12.47393f;
	const float maxEv = 4.026069f;

	float3 v = mul(max(color, 1e-10f), inset);
	v = (clamp(log2(v), minEv, maxEv) - minEv) / (maxEv - minEv);
	v = AgxContrast(v);
	if (punchy)
	{
		v = pow(max(v, 0.0f), 1.35f);
		const float luma = dot(v, float3(0.2126f, 0.7152f, 0.0722f));
		v = luma + 1.4f * (v - luma);
	}
	v = mul(v, outset);
	// The curve produces a display-encoded (2.2) signal; the sRGB target
	// re-encodes, so return linear.
	return pow(saturate(v), 2.2f);
}

float3 Tonemap(float3 color)
{
	if (gTonemapper == 0u) return AgxTonemap(color, false);
	if (gTonemapper == 1u) return AgxTonemap(color, true);
	if (gTonemapper == 2u) return AcesTonemap(color);
	return saturate(color);
}

#endif
