// Copies the DXR renderer's device depth (TemporalDeviceDepth, written by
// DxrLightingCS) into the raster depth buffer, so the forward transparent
// pass can depth-test glass against ray-traced opaque geometry. The source
// may be at a lower render resolution than the depth buffer (DLSS).
#include "PostprocessStructs.hlsli"

Texture2D<float> DxrDeviceDepth : register(t1);

float main(PostProcessVertexToPixel input) : SV_Depth
{
	uint width, height;
	DxrDeviceDepth.GetDimensions(width, height);
	const uint2 texel = min(uint2(input.uv * float2(width, height)), uint2(width - 1, height - 1));
	return DxrDeviceDepth.Load(int3(texel, 0));
}
