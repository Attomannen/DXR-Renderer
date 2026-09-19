// Equirectangular (latitude/longitude) panorama -> cubemap faces.
//
// This is the step that was missing: CubemapPrefilter::LoadBaseFromEquirectangular
// has always dispatched "EquirectangularToCubemapCS", but the shader did not
// exist anywhere in the tree, so every authored .hdr environment silently
// failed to become a cubemap. What reached the renderer instead was a flat
// Texture2D view bound to a TextureCube register, which samples as black --
// hence "environment sky averages 1e-08 units, rgb=(0,0,0)".
//
// Everything downstream (PrefilterSpecularCS / PrefilterDiffuseCS) consumes a
// cube, so this only has to produce the base cube; the mip chain and the
// roughness convolution are someone else's job.
#include "CubemapCommon.hlsli"

Texture2D<float4> gPanorama : register(t0);
SamplerState gLinearSampler : register(s0);
RWTexture2DArray<float4> gCubemap : register(u0);

cbuffer PanoCB : register(b0)
{
    uint gFaceResolution;
    float3 gPadding;
};

// Direction -> equirectangular UV.
//
// atan2(d.z, d.x) is the azimuth and asin(d.y) the elevation; dividing by 2*PI
// and PI maps them to [0,1]. The v flip is because image row 0 is the top of
// the panorama, which is +Y, while asin gives +1 at +Y.
float2 DirectionToEquirectUv(float3 direction)
{
    const float2 invAtan = float2(0.1591549f, 0.3183099f);   // 1/(2*PI), 1/PI
    float2 uv = float2(atan2(direction.z, direction.x), asin(clamp(direction.y, -1.0f, 1.0f)));
    uv *= invAtan;
    uv += 0.5f;
    uv.y = 1.0f - uv.y;
    return uv;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // z is the cube face; the dispatch is (res+7)/8 x (res+7)/8 x 6, so the
    // last group in each axis can run past the edge on non-multiple-of-8 faces.
    if (id.x >= gFaceResolution || id.y >= gFaceResolution || id.z >= 6u)
        return;

    // Texel centre, then to [-1, 1] across the face.
    const float2 texel = (float2(id.xy) + 0.5f) / float(gFaceResolution);
    const float2 faceUv = texel * 2.0f - 1.0f;

    const float3 direction = GetCubeDirection(id.z, faceUv);

    // SampleLevel, not Sample: there is no derivative in a compute shader, and
    // mip 0 is what we want regardless -- the panorama is already at source
    // resolution and the cube's own mip chain is generated afterwards.
    const float3 colour = gPanorama.SampleLevel(gLinearSampler, DirectionToEquirectUv(direction), 0).rgb;

    // HDR panoramas carry values far above 1; the destination is a float format
    // precisely so they survive. Only guard against negatives and NaNs, which
    // some .hdr exporters do emit and which poison the prefilter's averages.
    gCubemap[uint3(id.xy, id.z)] = float4(max(colour, 0.0f), 1.0f);
}
