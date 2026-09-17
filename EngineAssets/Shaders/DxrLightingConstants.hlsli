#ifndef DXR_LIGHTING_CONSTANTS_HLSLI
#define DXR_LIGHTING_CONSTANTS_HLSLI

// Per-frame constants of the DXR lighting pass (DxrLightingCS, b0).
// Included by both the shader and DeferredRenderer.cpp, so the two sides can
// never drift apart. Keep the layout HLSL-packed by hand: every row is 16
// bytes and matrices start on a new row. The C++ side checks the offsets.

#ifdef __cplusplus
#	include <cstdint>
namespace Tga::DxrShared
{
	using uint = uint32_t;
	struct uint2 { uint v[2]; uint& operator[](int i) { return v[i]; } };
	struct float2 { float v[2]; float& operator[](int i) { return v[i]; } };
	struct float3 { float v[3]; float& operator[](int i) { return v[i]; } };
	struct float4x4 { float m[16]; };
#	define DXR_CBUFFER_BEGIN(name, slot) struct alignas(16) name {
#	define DXR_CBUFFER_END };
#	define DXR_ROW_MAJOR
#else
#	define DXR_CBUFFER_BEGIN(name, slot) cbuffer name : register(slot) {
#	define DXR_CBUFFER_END };
#	define DXR_ROW_MAJOR row_major
#endif

DXR_CBUFFER_BEGIN(DxrLightingConstants, b0)
	// Camera
	float3 gCameraOrigin;
	float  gTanHalfFovY;
	float3 gCameraRight;
	float  gAspect;
	float3 gCameraUp;
	float  gSkyDisplayScale;            // physical sky: environment -> scene units
	float3 gCameraForward;
	float  gNrdReblur;                  // 1: REBLUR packing (YCoCg + normalised hit distance), 0: RELAX
	uint2  gOutputSize;
	float  gNrdHitDistA;                // REBLUR hit-distance normalisation A, metres
	float  _pad0;

	// Lights
	float3 gSunDirToLight;              // normalised, surface -> light
	float  _pad1;
	uint   gLightCount;
	float3 gSunRadiance;                // sun tint * intensity, scene units
	float  gAmbientIntensity;           // flat ambient floor
	float  gReflectionRoughnessCutoff;  // no reflection ray above this roughness
	float2 _pad2;
	float3 gEnvironmentTint;
	float  gEnvironmentMip;             // < 0: no environment

	// Feature switches and budgets
	float  gAoDistance;
	float  gAoStrength;
	uint   gEnableDirectLighting;
	uint   gEnableEnvironmentLighting;
	uint   gEnableIndirectGi;
	uint   gEnableReflections;
	uint   gEnableAmbientOcclusion;
	uint   gLightingView;               // debug view, 0 = beauty
	uint   gBrdfLutValid;
	uint   gNrdCheckerboard;            // AO and reflection rays on alternating half-pixel sets
	uint   gCheckerboardPhase;          // NRD frameIndex parity this frame
	uint   gTextureFiltering;           // ray-cone texture filtering
	uint   gReflectionSamples;
	uint   gAoSamples;
	uint   gPreExposed;                 // multiply every radiance write by PreExposure()
	uint   gSunShadowSamples;           // shadow rays per pixel for the sun disc

	// Temporal
	DXR_ROW_MAJOR float4x4 gWorldToClip;
	DXR_ROW_MAJOR float4x4 gPreviousWorldToClip;
	uint   gTemporalHistoryValid;
	float  gSpecularAaStrength;
	uint   gReflectionFrameIndex;       // seeds stochastic sampling
	uint   gNrdEnabled;
	float2 gJitter;                     // sample offset from the pixel centre, pixels
	float2 gPreviousJitter;
DXR_CBUFFER_END

#ifdef __cplusplus
}   // namespace Tga::DxrShared
#endif

#endif
