#include "Common.hlsli"

struct SkyboxPixelInput
{
    float4 position : SV_POSITION;
    float2 clipPos  : TEXCOORD0;
};

float4 main(SkyboxPixelInput input) : SV_TARGET
{
    float mipLevel = CustomShaderParameters.x;
    float intensity = CustomShaderParameters.y;

    // Compute ray direction in camera space:
    // x_cam = clip.x / CameraToProjection._m00
    // y_cam = clip.y / CameraToProjection._m11
    // z_cam = 1.0f
    float xCam = input.clipPos.x / CameraToProjection._m00;
    float yCam = input.clipPos.y / CameraToProjection._m11;
    float zCam = 1.0f;

    // Camera basis vectors in world space from CameraToWorld matrix:
    // _m00_m10_m20 = Right, _m01_m11_m21 = Up, _m02_m12_m22 = Forward
    float3 right   = CameraToWorld._m00_m10_m20;
    float3 up      = CameraToWorld._m01_m11_m21;
    float3 forward = CameraToWorld._m02_m12_m22;

    float3 worldRay = normalize(xCam * right + yCam * up + zCam * forward);

    float3 color = environmentTexture.SampleLevel(defaultSampler, worldRay, mipLevel).rgb;
    // Alpha 0 = "this texel is open sky" (used by GI sky-visibility capture).
    // Only consumed by the offscreen probe / GI captures; nothing reads sky alpha.
    return float4(color * intensity, 0.0f);
}
