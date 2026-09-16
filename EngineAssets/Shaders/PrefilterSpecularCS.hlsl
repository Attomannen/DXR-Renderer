// Specular GGX cubemap prefiltering using importance sampling (Split-Sum approximation Part 1).
// Computes prefiltered environment map for V = R = N. Part 2 (environment BRDF scale/bias) is evaluated in PBRFunctions.hlsli.
//
// Reference: Karis 2013 ("Real Shading in Unreal Engine 4"):
// https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
//
// Filtered Importance Sampling (mip selection): Lagarde 2014 ("Moving Frostbite to PBR"):
// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf

#include "CubemapCommon.hlsli"

TextureCube inputCubemap : register(t0);
SamplerState defaultSampler : register(s0);

RWTexture2DArray<float4> outputCubemap : register(u0);

cbuffer PrefilterConstants : register(b0)
{
    float Alpha;                    // Linear roughness alpha = perceptualRoughness^2
    uint MipIndex;                  // Target mip level being written
    uint NumMips;                   // Total number of mips in output cubemap
    uint FaceResolution;            // Resolution of the current mip face in pixels
    uint SampleCount;               // Number of importance samples (e.g. 512 or 1024)
    float SourceCubemapResolution;  // Resolution of base input cubemap face (e.g. 512 or 1024)
    float2 Padding;
};

// GGX Microfacet Distribution function D(H)
// alpha: Linear roughness (alpha = perceptualRoughness^2)
float DistributionGGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return a2 / max(denom, 1e-7f);
}

// GGX Importance Sampling: generates a half-vector H given uniform random variables Xi
// and linear roughness alpha (Karis 2013, Section 3.2).
float3 ImportanceSampleGGX(float2 Xi, float3 N, float alpha)
{
    float a2 = alpha * alpha;

    float phi = 2.0f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a2 - 1.0f) * Xi.y));
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    // Spherical to Cartesian coordinates in tangent space
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent to world space transform
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint face = dispatchThreadId.z;
    uint2 texel = dispatchThreadId.xy;

    if (texel.x >= FaceResolution || texel.y >= FaceResolution || face >= 6)
        return;

    // Normalize texel coordinates to [-1, 1] centered at pixel centers
    float2 uv = (float2(texel) + 0.5f) / float(FaceResolution);
    uv = uv * 2.0f - 1.0f;

    float3 N = GetCubeDirection(face, uv);
    float3 V = N; // Split-sum approximation assumes V = R = N

    // Mip 0 with zero alpha represents perfect mirror reflections:
    if (MipIndex == 0 || Alpha <= 0.00001f)
    {
        float3 color = inputCubemap.SampleLevel(defaultSampler, N, 0).rgb;
        outputCubemap[uint3(texel, face)] = float4(color, 1.0f);
        return;
    }

    float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    // Solid angle of a texel in the base source cubemap (Lagarde 2014):
    // Omega_t = 4 * PI / (6 * W * W)
    float omegaTexel = 4.0f * PI / (6.0f * SourceCubemapResolution * SourceCubemapResolution);

    for (uint i = 0; i < SampleCount; ++i)
    {
        float2 Xi = Hammersley(i, SampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, Alpha);
        float3 L = normalize(2.0f * dot(V, H) * H - V);

        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0f)
        {
            float NdotH = saturate(dot(N, H));
            float VdotH = saturate(dot(V, H));

            // Probability Distribution Function for GGX sample
            float D = DistributionGGX(NdotH, Alpha);
            float pdf = (D * NdotH / (4.0f * VdotH)) + 0.0001f;

            // Solid angle subtended by this sample (Lagarde 2014 / Karis 2013)
            float omegaSample = 1.0f / (float(SampleCount) * pdf + 0.0001f);

            // Calculate source cubemap mip level to sample from (Filtered Importance Sampling, Karis 2013 / Lagarde 2014)
            float mipLevel = (Alpha <= 0.00001f || MipIndex == 0) ? 0.0f : max(0.5f * log2(omegaSample / omegaTexel), 0.0f);

            prefilteredColor += inputCubemap.SampleLevel(defaultSampler, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / max(totalWeight, 0.0001f);
    outputCubemap[uint3(texel, face)] = float4(prefilteredColor, 1.0f);
}
