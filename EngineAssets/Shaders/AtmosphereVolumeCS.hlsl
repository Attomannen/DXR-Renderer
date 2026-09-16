#include "AtmosphereCommon.hlsli"
Texture2DArray<float> FogShadow : register(t1);
SamplerComparisonState FogShadowCmp : register(s2);
// Same leading fields as the renderer's b9; raw engine matrices are column-major here.
cbuffer FogShadowParams : register(b9)
{
    column_major float4x4 FogCascadeVP[4];
    float4 FogCascadeSplits, FogCascadeTexelWorld, FogCascadeDepthRange;
    float FogShadowTexel, FogShadowBias, FogShadowStrength, FogShadowEnabled;
};
cbuffer FogShadowCamera : register(b7) { row_major float4x4 FogWorldToView; };
float FogSunVisibility(float3 world)
{
    if (FogShadowEnabled < 0.5) return 0;
    float z = mul(float4(world,1), FogWorldToView).z;
    int c = 3;
    for (int i = 0; i < 3; ++i) if (z < FogCascadeSplits[i]) { c = i; break; }
    float4 sc = mul(FogCascadeVP[c], float4(world,1));
    sc.xyz /= sc.w;
    float2 uv = float2(sc.x * 0.5 + 0.5, 0.5 - sc.y * 0.5);
    // Outside available shadow coverage: suppress sunlight rather than leak it indoors.
    if (any(uv < 0) || any(uv > 1) || sc.z < 0 || sc.z > 1) return 0;
    return FogShadow.SampleCmpLevelZero(FogShadowCmp, float3(uv,c), sc.z - FogShadowBias / max(1, FogCascadeDepthRange[c]));
}
#include "AtmosphereVolumeMain.hlsli"
