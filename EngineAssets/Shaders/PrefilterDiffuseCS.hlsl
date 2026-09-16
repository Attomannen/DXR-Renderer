// Diffuse irradiance convolution for cubemap tail mips via cosine-weighted hemisphere sampling

#include "CubemapCommon.hlsli"

TextureCube inputCubemap : register(t0);
SamplerState defaultSampler : register(s0);

RWTexture2DArray<float4> outputCubemap : register(u0);

cbuffer DiffuseConstants : register(b0)
{
    uint MipIndex;                  // Target mip level being written
    uint NumMips;                   // Total number of mips in output cubemap
    uint FaceResolution;            // Resolution of the current mip face in pixels
    uint SampleCount;               // Number of importance samples (e.g. 512 or 1024)
    float SourceCubemapResolution;  // Resolution of base input cubemap face
    float3 Padding;
};

// Cosine-weighted hemisphere sampling (PDF = cos(theta) / PI)
float3 CosineSampleHemisphere(float2 Xi, float3 N)
{
    float phi = 2.0f * PI * Xi.x;
    float cosTheta = sqrt(1.0f - Xi.y);
    float sinTheta = sqrt(Xi.y);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint face = dispatchThreadId.z;
    uint2 texel = dispatchThreadId.xy;

    if (texel.x >= FaceResolution || texel.y >= FaceResolution || face >= 6)
        return;

    float2 uv = (float2(texel) + 0.5f) / float(FaceResolution);
    uv = uv * 2.0f - 1.0f;

    float3 N = GetCubeDirection(face, uv);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    // Solid angle of a texel in the source cubemap
    float omegaTexel = 4.0f * PI / (6.0f * SourceCubemapResolution * SourceCubemapResolution);

    for (uint i = 0; i < SampleCount; ++i)
    {
        float2 Xi = Hammersley(i, SampleCount);
        float3 L = CosineSampleHemisphere(Xi, N);
        float NdotL = saturate(dot(N, L));

        if (NdotL > 0.0f)
        {
            // PDF for cosine-weighted sampling is NdotL / PI
            float pdf = NdotL / PI;
            float omegaSample = 1.0f / (float(SampleCount) * pdf + 0.0001f);
            float mipLevel = max(0.5f * log2(omegaSample / omegaTexel), 0.0f);

            irradiance += inputCubemap.SampleLevel(defaultSampler, L, mipLevel).rgb;
            count += 1.0f;
        }
    }

    // Normalized diffuse radiance (divided by PI to match Lys reference generation and lysBurleyCube consumption)
    irradiance = irradiance / (PI * max(count, 1.0f));
    outputCubemap[uint3(texel, face)] = float4(irradiance, 1.0f);
}
