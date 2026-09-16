#include "PostprocessStructs.hlsli"

cbuffer SelectionOutlineBuffer : register(b13)
{
    uint4 P4Color;
};

Texture2D<uint3> FullscreenTextureUint : register(t1);

PostProcessPixelOutput main(PostProcessVertexToPixel input)
{
	
	
    uint selection[3][3];
    bool hasDifference = false;
    
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            selection[i][j] = FullscreenTextureUint.Load(int3((int) input.position.x - 1 + i, (int) input.position.y - 1 + j, 0)).g;
	
    for (i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
            if (selection[i][j] != selection[i][j+1])
                hasDifference = true;

    for (i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            if (selection[i][j] != selection[i+1][j])
                hasDifference = true;
    
    //
    uint p4status[3][3];
    bool hasP4Difference = false;

    for (i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            p4status[i][j] = FullscreenTextureUint.Load(int3((int) input.position.x - 1 + i, (int) input.position.y - 1 + j, 0)).b;
    
    for (i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
            if (p4status[i][j] != p4status[i][j+1])
                hasP4Difference = true;

    for (i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            if (p4status[i][j] != p4status[i+1][j])
                hasP4Difference = true;
//
    if (!hasDifference && !hasP4Difference)
        discard;
    
    float4 color = float4(0.f, 0.f, 0.f, 0.f);
    if (hasP4Difference)
    {
        uint p4value = FullscreenTextureUint.Load(int3((int) input.position.x, (int) input.position.y, 0)).b;
        if (p4value == 1)
        {
            color += float4(0.f, 0.f, 1.f, 1.f);
        }
        else if (p4value == 2)
        {
            color += float4(1.f, 0.f, 0.f, 1.f);
        }
    }
    color += hasDifference ? float4(1.f, 0.1f, 0.f, 1.f) : float4(0.f, 0.f, 0.f, 1.f);

	PostProcessPixelOutput returnValue;
    returnValue.color = color;
    //returnValue.color.a = 1.f;
	return returnValue;
}