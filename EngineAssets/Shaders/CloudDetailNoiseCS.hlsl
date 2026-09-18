#include "CloudsCommon.hlsli"

// Bakes the 32^3 erosion-detail volume once at startup -- matching
// Schneider/HZD's own detail-texture resolution: three octaves of Worley
// noise, subtracted from the base shape near density edges
// (SampleCloudDensity, with the sample position curl-warped there for
// turbulence) for the wispy, eroded look instead of flat cotton-ball
// silhouettes.

RWTexture3D<float> DetailNoiseOut : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint3 size;
	DetailNoiseOut.GetDimensions(size.x, size.y, size.z);
	if (any(id >= size)) return;

	// 4 / 6 / 8 cells per axis against a 64^3 volume: 16, 10.7 and 8 texels
	// each, all properly resolved. The original was 4 / 6 / 10 cells at 32^3,
	// which gave the top octave 3.2 texels -- speckle, not noise, applied
	// straight onto every cloud edge. Dropping to 2 / 3 / 4 cells fixed that
	// but threw out the detail with it: this volume supplies every cloud's
	// surface texture, and two cells is two blobs. Doubling the volume instead
	// of halving the frequencies keeps both.
	const float kPeriod = 4.0f;
	float3 p = (float3(id) + 0.5f) / size * kPeriod;
	float w0 = CloudsWorley3D(p, kPeriod);
	float w1 = CloudsWorley3D(p * 1.5f + 11.3f, kPeriod * 1.5f);
	// A third octave so the erosion texture carries more than two frequencies
	// of structure -- otherwise every erosion sample looks like a scaled copy
	// of the same two blobs instead of real grain.
	float w2 = CloudsWorley3D(p * 2.0f + 23.7f, kPeriod * 2.0f);
	DetailNoiseOut[id] = saturate(w0 * 0.5f + w1 * 0.3f + w2 * 0.2f);
}
