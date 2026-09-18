#ifndef CLOUDS_CONSTANTS_HLSLI
#define CLOUDS_CONSTANTS_HLSLI

// Shared C++/HLSL cloud tunables -- same pattern as SkyAtmosphereConstants.hlsli
// (and DxrLightingConstants.hlsli before it): included by both the shaders
// and DeferredRendererClouds.cpp so the layout can never drift. All lengths
// in meters, consistent with the sky system's own convention.

#ifdef __cplusplus
#	include <cstdint>
namespace Tga::CloudsShared
{
	using uint = uint32_t;
	struct float2 { float v[2]; float& operator[](int i) { return v[i]; } };
	struct float3 { float v[3]; float& operator[](int i) { return v[i]; } };
	struct float4x4 { float m[16]; };
#	define CLOUDS_CBUFFER_BEGIN(name, slot) struct alignas(16) name {
#	define CLOUDS_CBUFFER_END };
#	define CLOUDS_ROW_MAJOR
#else
#	define CLOUDS_CBUFFER_BEGIN(name, slot) cbuffer name : register(slot) {
#	define CLOUDS_CBUFFER_END };
#	define CLOUDS_ROW_MAJOR row_major
#endif

CLOUDS_CBUFFER_BEGIN(CloudsConstants, b13)
	uint  gCloudsEnabled;
	float gCloudCoverage;      // 0..1, fraction of sky covered
	float gCloudDensity;       // extinction multiplier
	float gCloudBaseAltitude;  // meters above sea level

	float gCloudTopAltitude;   // meters above sea level
	float gCloudScale;         // meters per noise tile
	float2 gCloudSpeed;        // wind, meters/second (world X, Z)

	float gTime;               // seconds, wrapped
	uint  gCloudLightSteps;
	float gCloudDetailStrength;
	uint  gCloudHistoryValid;  // hero raymarch's temporal reprojection (CloudsVolumeCS)

	// Hero raymarch's temporal reprojection target. Declared here (rather
	// than its own cbuffer) so every cloud-consuming shader keeps sharing
	// one binding -- the bake shaders and shadow lookup just carry the
	// extra unused bytes, same as they already do for the fields above.
	CLOUDS_ROW_MAJOR float4x4 gCloudPrevWorldToClip;

	// Camera basis for ray generation. Rays used to be reconstructed from the
	// fog pass's clip-to-world matrix at depth 0.99999, i.e. at the far plane
	// of a 1 km non-linear depth range, where the homogeneous divide cancels
	// catastrophically and the direction comes out quantised into steps. The
	// cloud shell entry distance, and everything after it, then showed as
	// terraces across the deck whenever it was viewed obliquely.
	float3 gCloudCamRight;   float gCloudTanHalfFovY;
	float3 gCloudCamUp;      float gCloudAspect;
	float3 gCloudCamForward; float _cloudPad0;
CLOUDS_CBUFFER_END

#ifdef __cplusplus
}   // namespace Tga::CloudsShared
#endif

#endif
