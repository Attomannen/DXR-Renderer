#ifndef SKY_ATMOSPHERE_CONSTANTS_HLSLI
#define SKY_ATMOSPHERE_CONSTANTS_HLSLI

// Per-(sun direction / atmosphere tunable) constants shared by every sky-LUT
// pass (transmittance, multi-scatter, sky-view) and the base-cubemap render.
// Included by both HLSL and DeferredRendererSky.cpp, so the two sides can
// never drift apart -- same pattern as DxrLightingConstants.hlsli. Keep the
// layout HLSL-packed by hand: every row is 16 bytes.
//
// All lengths are in METERS, independent of the engine's centimeter scene
// units -- this simulates an actual Earth-scale atmosphere. The one place
// scene units enter is where the camera's world-space height gets converted
// to meters before being added to BottomRadius (see SkyAtmosphereCommon.hlsli).

#ifdef __cplusplus
#	include <cstdint>
namespace Ag::SkyShared
{
	using uint = uint32_t;
	struct float3 { float v[3]; float& operator[](int i) { return v[i]; } };
#	define SKY_CBUFFER_BEGIN(name, slot) struct alignas(16) name {
#	define SKY_CBUFFER_END };
#else
#	define SKY_CBUFFER_BEGIN(name, slot) cbuffer name : register(slot) {
#	define SKY_CBUFFER_END };
#endif

SKY_CBUFFER_BEGIN(SkyAtmosphereConstants, b11)
	float3 gSunDirToLight;         // normalised, ground -> sun
	float  gBottomRadius;          // meters
	float3 gSunIlluminance;        // sun tint * illuminance, scene radiance units
	float  gTopRadius;             // meters

	float3 gRayleighScattering;    // per meter, sea level
	float  gRayleighDensityH;      // exponential scale height, meters
	float3 gMieScattering;         // per meter, sea level (already turbidity-scaled)
	float  gMieDensityH;           // exponential scale height, meters
	float3 gMieExtinction;         // scattering + absorption, per meter
	float  gMiePhaseG;
	float3 gOzoneAbsorption;       // per meter, at the ozone layer's center altitude
	float  gOzoneCenterAltitude;   // meters

	float  gOzoneWidth;            // tent half-width, meters
	float  gGroundAlbedo;
	float  gCameraHeight;          // meters above gBottomRadius (i.e. above sea level)
	uint   gMultiScatterLutRes;    // square resolution of the multi-scatter LUT

	uint   gSkyViewLutWidth, gSkyViewLutHeight;
	float  gSunAngularRadius;      // radians
	float  gSunDiskIntensity;      // HDR, same units as the sun-disk in AtmosphereCommon.hlsli
SKY_CBUFFER_END

#ifdef __cplusplus
}   // namespace Ag::SkyShared
#endif

#endif
