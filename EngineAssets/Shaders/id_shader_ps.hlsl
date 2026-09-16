#include "common.hlsli"
SamplerState SampleType;
Texture2D shaderTextures[2] : register(t1);


cbuffer IdConstantBuffer : register(b13)
{
    uint ObjectID;
    uint SelectionID;
    uint P4Status;
    uint Unused;
};

uint4 main(ModelVertexToPixel input) : SV_TARGET
{
    float4 Diffuse = shaderTextures[0].Sample(SampleType, input.texCoord0);
    if (Diffuse.w <= AlphaTestThreshold)
    {
        discard;
    }
    return uint4(ObjectID, SelectionID, P4Status, 0);
}
