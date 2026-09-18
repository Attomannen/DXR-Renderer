// Motion blur, pass 1 of 3: the largest velocity in each tile. COMPUTE.
//
// Compute rather than a pixel pass, and not for performance. The velocity
// target is written as a UAV by the ray pass, and every other consumer in the
// engine reads it from compute; reading it from a pixel shader reproducibly
// blacked out the entire frame, with the transitions in place and tracked.
// Rather than keep guessing at that, this uses the path the engine already
// exercises for the same resource.
//
// A per-pixel gather alone cannot blur a fast object past its own silhouette,
// because a background pixel just outside the object has no velocity of its own
// and so gathers nothing. The object comes out sharp-edged and smeared only on
// the inside, which reads as a smudge rather than as movement. The fix
// (McGuire et al., "A Reconstruction Filter for Plausible Motion Blur") is to
// let each pixel ask what the fastest thing NEAR it was doing, which is what
// this pass and the neighbour dilation after it build.

Texture2D<float4> Motion : register(t0);
RWTexture2D<float2> TileMaxOut : register(u0);

cbuffer MotionBlurCb : register(b0)
{
	uint2 gTileCount;      // output dimensions, in tiles
	uint2 gSourceSize;     // velocity buffer dimensions, in pixels
	uint2 gOutputSize;     // display dimensions, in pixels
	uint  gTileSize;       // source pixels per tile, per axis
	float gVelocityScale;  // render pixels -> display pixels, times shutter fraction
	float gNear, gFar, gMaxRadius; uint _mbPad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= gTileCount)) return;

	const int2 base = int2(id.xy) * int(gTileSize);
	float2 best = 0.0f;
	float bestLenSq = -1.0f;
	[loop] for (uint y = 0; y < gTileSize; ++y)
	{
		[loop] for (uint x = 0; x < gTileSize; ++x)
		{
			const int2 p = min(base + int2(x, y), int2(gSourceSize) - 1);
			const float2 v = Motion.Load(int3(p, 0)).xy;
			const float l = dot(v, v);
			if (l > bestLenSq) { bestLenSq = l; best = v; }
		}
	}
	TileMaxOut[id.xy] = best;
}
