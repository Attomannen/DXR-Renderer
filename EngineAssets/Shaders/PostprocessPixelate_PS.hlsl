#include "PostprocessStructs.hlsli"


cbuffer PixelateEffectBuffer : register(b13)
{
	float PixelSize;
	uint2 Resolution;
	float Garbage;
}

PostProcessPixelOutput main(PostProcessVertexToPixel input)
{
	PostProcessPixelOutput returnValue;

	const float2 uv = input.uv;
	const float2 pixelSize = 0.1f / (Resolution / PixelSize);
	const float2 fixedUV = uv + pixelSize / 2.0f;
	const float2 pixelUV = floor(fixedUV / pixelSize) * pixelSize;

	returnValue.color.rgb = FullscreenTexture1.Sample(DefaultSampler, pixelUV).rgb;
	returnValue.color.a = 1.0f;
	return returnValue;
}