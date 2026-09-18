// Motion blur, pass 3 of 3: the reconstruction gather. COMPUTE.
//
// Walks the dominant nearby velocity and asks, for each tap, whether that tap's
// surface could plausibly have covered this pixel during the shutter. Two cases
// matter and they are not symmetric:
//
//   the tap is NEARER than us  -- it may have swept across us, so it
//                                 contributes according to its own speed.
//   the tap is FARTHER than us -- we may have swept off it and revealed it, so
//                                 it contributes according to OUR speed.
//
// Ignoring the depth relationship is what makes naive motion blur drag
// background through foreground and leave halos around moving objects.

Texture2D<float4> Colour : register(t0);
Texture2D<float4> Motion : register(t1);
Texture2D<float2> NeighbourMax : register(t2);
Texture2D<float> Depth : register(t3);
RWTexture2D<float4> BlurredOut : register(u0);
SamplerState LinearClamp : register(s0);

cbuffer MotionBlurCb : register(b0)
{
	uint2 gTileCount;
	uint2 gSourceSize;
	uint2 gOutputSize;
	uint  gTileSize;
	float gVelocityScale;
	float gNear, gFar, gMaxRadius; uint _mbPad;
};

static const int kTaps = 15;

float LinearZ(float d)
{
	return gNear * gFar / max(gFar - d * (gFar - gNear), 1e-6f);
}

// 1 when a is nearer than b, 0 when farther, ramped so coplanar surfaces do not
// flip between the two cases on a depth bit.
float SoftDepthCompare(float za, float zb)
{
	return saturate(1.0f - (za - zb) / max(0.01f * min(za, zb), 1e-4f));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= gOutputSize)) return;
	const float2 uv = (float2(id.xy) + 0.5f) / float2(gOutputSize);
	const float2 outTexel = 1.0f / float2(gOutputSize);
	const float3 centre = Colour.SampleLevel(LinearClamp, uv, 0).rgb;

	// Display-space pixels. The velocity buffer is in RENDER pixels, which
	// differ under upscaling; gVelocityScale carries that and the shutter.
	const uint2 tile = min(id.xy / gTileSize, gTileCount - 1);
	float2 tileVel = NeighbourMax.Load(int3(tile, 0)) * gVelocityScale;
	float tileLen = length(tileVel);
	if (tileLen > gMaxRadius) { tileVel *= gMaxRadius / tileLen; tileLen = gMaxRadius; }
	// Below half a pixel there is no path to walk.
	if (tileLen < 0.5f) { BlurredOut[id.xy] = float4(centre, 1.0f); return; }

	const float2 ownVel = Motion.SampleLevel(LinearClamp, uv, 0).xy * gVelocityScale;
	const float ownLen = length(ownVel);
	const float centreZ = LinearZ(Depth.SampleLevel(LinearClamp, uv, 0));

	// Jittered so neighbouring pixels do not sample the same points and band the
	// trail. Interleaved gradient noise, ordered rather than random, for the same
	// reason the cloud march uses it: a neighbourhood then averages evenly.
	const float jitter = frac(52.9829189f * frac(dot(float2(id.xy), float2(0.06711056f, 0.00583715f)))) - 0.5f;

	float3 sum = centre;
	float weight = 1.0f;

	[loop] for (int s = 0; s < kTaps; ++s)
	{
		// Symmetric about the pixel: the shutter opens before and closes after
		// the instant this frame represents.
		const float t = ((float(s) + 0.5f) / float(kTaps) - 0.5f) * 2.0f;
		const float2 tapUv = uv + tileVel * (t + jitter / float(kTaps)) * outTexel;

		const float tapZ = LinearZ(Depth.SampleLevel(LinearClamp, tapUv, 0));
		const float2 tapVel = Motion.SampleLevel(LinearClamp, tapUv, 0).xy * gVelocityScale;
		const float dist = abs(t) * tileLen;

		// Nearer tap sweeping over us, weighted by whether its own blur is long
		// enough to reach; farther tap revealed by our motion, weighted by ours.
		const float nearer = SoftDepthCompare(tapZ, centreZ) * saturate(1.0f - dist / max(length(tapVel), 1e-4f));
		const float farther = SoftDepthCompare(centreZ, tapZ) * saturate(1.0f - dist / max(ownLen, 1e-4f));
		const float w = nearer + farther;
		if (w > 0.0f)
		{
			sum += Colour.SampleLevel(LinearClamp, tapUv, 0).rgb * w;
			weight += w;
		}
	}
	BlurredOut[id.xy] = float4(sum / weight, 1.0f);
}
