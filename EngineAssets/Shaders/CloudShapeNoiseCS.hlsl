#include "CloudsCommon.hlsli"

// Bakes the base-shape volume once at startup (DeferredRenderer::Init,
// mirroring how the DXR BRDF LUT is baked once and kept forever). Follows
// Schneider/HZD's "Perlin-Worley" technique: rather than blending an FBM
// and a Worley noise together (which just looks like two textures overlaid,
// and reads as smooth round blobs), the FBM is REMAPPED using an inverted,
// low-frequency Worley as a per-cell floor -- density can only fully express
// itself near a Worley cell's centre and gets pinched off toward its edges,
// which is what makes the result read as one connected, billowy shape
// carved by the cell structure instead of "fbm blurred over cellular noise".
//   r: the Perlin-Worley combination above -- the primary shape driver.
//   g, b: two further Worley octaves at increasing frequency, baked
//      alongside it and used in SampleCloudDensity to erode the base shape
//      at the shape-texture's own resolution -- this is what breaks one
//      big smooth lobe into several differently sized puffs instead of a
//      single round "popcorn ball" per cell.
// Tileable by construction (the noise functions are periodic in integer
// cells and the volume is sampled with wrap addressing), so it scrolls
// seamlessly under wind offset without ever needing to be re-baked.

RWTexture3D<float4> ShapeNoiseOut : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint3 size;
	ShapeNoiseOut.GetDimensions(size.x, size.y, size.z);
	if (any(id >= size)) return;

	// kPeriod tile periods across the volume; CloudsFbm3D/CloudsWorley3D wrap
	// their lattice hashing at this period so the baked result tiles
	// seamlessly under the wrap-address sampler that reads it back.
	const float kPeriod = 4.0f;
	float3 p = (float3(id) + 0.5f) / size * kPeriod;

	float fbm = CloudsFbm3D(p, 5, kPeriod);
	float worleyDilate = CloudsWorley3D(p, kPeriod);
	float perlinWorley = CloudsRemap(fbm, saturate(1.0f - worleyDilate) - 1.0f, 1.0f, 0.0f, 1.0f);

	// Two more octaves, tighter cells each time, so the erosion this feeds
	// at sample time reads as several distinct puffs rather than one
	// oversized lobe per cell.
	float worleyMid = CloudsWorley3D(p * 2.0f, kPeriod * 2.0f);
	float worleyFine = CloudsWorley3D(p * 4.0f, kPeriod * 4.0f);

	ShapeNoiseOut[id] = float4(saturate(perlinWorley), saturate(worleyMid), saturate(worleyFine), 1.0f);
}
