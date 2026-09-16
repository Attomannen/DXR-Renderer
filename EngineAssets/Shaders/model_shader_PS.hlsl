#include "common.hlsli"

#define DIFFUSE_MAP 0
#define NORMAL_MAP 1

Texture2D shaderTextures[2] : register( t1 );

float4 main(ModelVertexToPixel input) : SV_TARGET
{
    float4 color = shaderTextures[DIFFUSE_MAP].Sample(defaultSampler, input.texCoord0);
	
    if (color.a <= AlphaTestThreshold)
	{
		discard;
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

    return color;
}