// Motion blur, pass 2 of 3: the largest velocity among a tile and its eight
// neighbours. COMPUTE, for the same reason as pass 1.
//
// One more dilation step. A tile only knows about the pixels inside it, so an
// object moving fast enough to cross more than a tile in one frame would still
// be clipped at the tile boundary; taking the neighbourhood maximum lets its
// blur reach a full tile beyond wherever the object actually is, which is the
// distance it could have travelled.

Texture2D<float2> TileMax : register(t0);
RWTexture2D<float2> NeighbourMaxOut : register(u0);

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

	float2 best = 0.0f;
	float bestLenSq = -1.0f;
	[unroll] for (int y = -1; y <= 1; ++y)
	{
		[unroll] for (int x = -1; x <= 1; ++x)
		{
			const int2 p = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(gTileCount) - 1);
			const float2 v = TileMax.Load(int3(p, 0));
			const float l = dot(v, v);
			if (l > bestLenSq) { bestLenSq = l; best = v; }
		}
	}
	NeighbourMaxOut[id.xy] = best;
}
