#ifndef POSTPROCESSSTRUCTS_HLSLI
#define POSTPROCESSSTRUCTS_HLSLI

struct PostProcessVertexInput
{
	unsigned int index : SV_VertexID;
};

struct PostProcessVertexToPixel
{
	float4 position	: SV_POSITION;
	float2 uv		: UV;
};

struct PostProcessPixelOutput
{
	float4 color	: SV_TARGET;
};

static const float GaussianKernel5[5] =
{ 0.06136f, 0.24477f, 0.38774f, 0.24477f, 0.06136 };

Texture2D FullscreenTexture1 : register(t1);
Texture2D FullscreenTexture2 : register(t2);
Texture2D FullscreenTexture3 : register(t3);
Texture2D FullscreenTexture4 : register(t4);

SamplerState DefaultSampler : register(s0);

#endif