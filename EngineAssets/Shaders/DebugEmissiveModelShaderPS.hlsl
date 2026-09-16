#include "Common.hlsli"
#include "LambertFunctions.hlsli"

PixelOutput main(ModelVertexToPixel input)
{
    PixelOutput result;

    float2 scaledUV = input.texCoord0;
	
    float3 toEye = normalize(CameraToWorld._m03_m13_m23 - input.worldPosition.xyz);
    float4 albedo = albedoTexture.Sample(defaultSampler, scaledUV).rgba;

    if (albedo.a <= AlphaTestThreshold)
    {
        discard;
        result.color = float4(0.f, 0.f, 0.f, 0.f);
        return result;
    }

    float3 fx = fxTexture.Sample(defaultSampler, scaledUV).rgb;

    float emissive = fx.r;
    
    result.color.rgb = (float3) emissive * albedo.rgb;
    result.color.a = albedo.a;
    return result;
}

