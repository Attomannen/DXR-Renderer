#include "Common.hlsli"

struct SkyboxPixelInput
{
    float4 position : SV_POSITION;
    float2 clipPos  : TEXCOORD0;
};

SkyboxPixelInput main(uint vertexID : SV_VertexID)
{
    SkyboxPixelInput output;

    // Full-screen triangle covering [-1, 1] clip space:
    // Vertex 0: (-1, 1), Vertex 1: (3, 1), Vertex 2: (-1, -3)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 clip = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    
    output.position = float4(clip, 1.0f, 1.0f);
    output.clipPos = clip;

    return output;
}
