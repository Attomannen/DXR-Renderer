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
};
Texture2D<float> FogDepth : register(t4);
float3 FogWorld(float2 uv, float depth)
{
    float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), FogClipToWorld);
    return w.xyz / w.w;
}
// Analytic line integral of exponential height density. Distances in meters.
float FogOpticalDepth(float3 direction, float distance)
{
    float length = max(0, min(distance, FogMaxDistance) - FogStart);
    float h = (FogCamera.y * 0.01 - FogBaseHeight) + direction.y * FogStart;
    float density = FogDensity * exp(clamp(-FogHeightFalloff * h, -20, 20));
    float x = clamp(FogHeightFalloff * direction.y * length, -20, 20);
    float integral = abs(x) < 0.001 ? 1 - x * 0.5 + x * x / 6 : (1 - exp(-x)) / x;
    return min(80, max(0, density * length * integral));
}
float FogPhase(float cosine)
{
    float g = FogAnisotropy;
    return (1 - g * g) / (12.5663706 * pow(max(0.01, 1 + g * g - 2 * g * cosine), 1.5));
}
#endif
