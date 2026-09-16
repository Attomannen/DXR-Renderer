#include "PostprocessStructs.hlsli"

PostProcessPixelOutput main(PostProcessVertexToPixel input)
{
	PostProcessPixelOutput returnValue;
	returnValue.color.rgb = FullscreenTexture1.Sample(DefaultSampler, input.uv).rgb;
	returnValue.color.a = 1.0f;
	return returnValue;
}