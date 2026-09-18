#ifndef ATMOSPHERE_COMMON
#define ATMOSPHERE_COMMON
cbuffer AtmosphereParams : register(b8)
{
    row_major float4x4 FogClipToWorld;
    float3 FogCamera; float FogDensity;
    float3 FogSunDirection; float FogHeightFalloff;
    float3 FogSunRadiance; float FogBaseHeight;
    float3 FogColor; float FogStart;
    float FogMaxDistance, FogVolumeDistance, FogVolumeStrength, FogAnisotropy;
    uint FogWidth, FogHeight, FogSteps, FogAffectSky;
    uint FogVolumeEnabled, FogDebugView; float2 FogJitter;
	float FogSunDiskAngularRadius, FogSunDiskIntensity;
	uint FogSunDiskEnabled; float FogPreExposed;
	float3 FogCamRight; float FogTanHalfFovY;
	float3 FogCamUp; float FogAspect;
	float3 FogCamForward; float FogTime;   // seconds, wrapped; drives star scintillation
	float FogStarIntensity, FogStarDensity, FogStarTwinkle, FogStarsEnabled;
};
// View direction through a pixel from the camera basis. FogWorld() at a
// depth near 1 is numerically unusable for directions: see the comment on
// FogCamRight above.
float3 FogViewDir(float2 uv)
{
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	return normalize(FogCamForward + FogCamRight * (ndc.x * FogAspect * FogTanHalfFovY) + FogCamUp * (ndc.y * FogTanHalfFovY));
}
Texture2D<float> FogDepth : register(t4);
float3 FogWorld(float2 uv, float depth)
{
    float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), FogClipToWorld);
    return w.xyz / w.w;
}
// Analytic line integral of exponential height density. Distances in meters.
//
// The closed form is density * (1 - exp(-k*dy*L)) / (k*dy), and for an upward
// ray that converges to density/(k*dy) however far L runs: the ray climbs out
// of the layer and stops accumulating. That is the whole character of height
// fog, and it is what makes the horizon a dense band that thins smoothly
// toward the zenith instead of a hard line.
//
// The previous form clamped k*dy*L to 20 and then multiplied the result by L,
// which destroys exactly that: past the clamp the integral pinned at 1/20 and
// the optical depth went on growing with distance, so an upward ray never
// escaped the layer and the angular falloff above the horizon was lost. The
// clamp now sits only inside the exponential, where it belongs, and the
// division by k*dy keeps the limit finite.
float FogOpticalDepth(float3 direction, float distance)
{
    float len = max(0, min(distance, FogMaxDistance) - FogStart);
    float h = (FogCamera.y * 0.01 - FogBaseHeight) + direction.y * FogStart;
    float density = FogDensity * exp(clamp(-FogHeightFalloff * h, -20, 20));
    float kdy = FogHeightFalloff * direction.y;
    float opticalDepth;
    if (abs(kdy * len) < 0.001)
        opticalDepth = density * len;   // effectively horizontal, or no falloff
    else
        opticalDepth = density * (1 - exp(-clamp(kdy * len, -20, 20))) / kdy;
    return min(80, max(0, opticalDepth));
}
float FogPhase(float cosine)
{
    float g = FogAnisotropy;
    return (1 - g * g) / (12.5663706 * pow(max(0.01, 1 + g * g - 2 * g * cosine), 1.5));
}
#endif
