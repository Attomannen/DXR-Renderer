// Native DXR TAA. Inputs are scene-linear HDR and unjittered pixel motion.
Texture2D<float4> CurrentColor : register(t0);
Texture2D<float> CurrentDepth : register(t1);
Texture2D<float2> Motion : register(t2);
Texture2D<float2> MotionMetadata : register(t3);
Texture2D<float4> HistoryColor : register(t4);
Texture2D<float> HistoryDepth : register(t5);
Texture2D<float4> CurrentSurfaceGuide : register(t6);
Texture2D<float4> HistorySurfaceGuide : register(t7);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> ResolvedColor : register(u0);
RWTexture2D<float> ResolvedDepth : register(u1);
RWTexture2D<float4> Diagnostic : register(u2);
RWTexture2D<float4> ResolvedSurfaceGuide : register(u3);
cbuffer TemporalCB : register(b0)
{
    uint2 Size;
    uint HistoryValid;
    uint DebugView;
    float2 Jitter;
    float2 PreviousJitter;
    float NearPlane;
    float FarPlane;
    float HistoryWeight;
    // Bit 0: unchanged camera and rigid scene.
    uint TemporalFlags;
};

float LinearDepth(float d)
{
    return NearPlane * FarPlane / max(FarPlane - d * (FarPlane - NearPlane), 0.00001f);
}
float3 ToYCoCg(float3 c)
{
    return float3(dot(c,float3(0.25f,0.5f,0.25f)), (c.r-c.b)*0.5f, (-c.r+2*c.g-c.b)*0.25f);
}
float3 ToRGB(float3 c) { return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z); }

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size)) return;
    const int2 p = int2(id.xy);
    // The primary-ray texture is in the raw jittered sample domain: raw pixel
    // q represents the camera sample q + Jitter.  History deliberately lives
    // in the fixed output domain, so every *current* temporal input has to be
    // fetched from q = p - Jitter.  Fetching color there but depth/velocity at
    // p pairs unrelated surfaces around silhouettes as Jitter changes.
    const float2 sourcePixel = float2(p) + 0.5f - Jitter;
    const float2 currentUv = sourcePixel / float2(Size);
    const int2 sourceP = clamp(int2(floor(sourcePixel)), int2(0,0), int2(Size)-1);
    const float depth = CurrentDepth.Load(int3(sourceP,0));
    float3 current = max(CurrentColor.SampleLevel(LinearClamp,currentUv,0).rgb,0);
    // Depth, velocity, and validity are discontinuous across geometry edges;
    // point-fetch the same source sample instead of filtering across an edge.
    const float2 motion = Motion.Load(int3(sourceP,0));
    const float2 metadata = MotionMetadata.Load(int3(sourceP,0));
	const float4 currentGuide = CurrentSurfaceGuide.Load(int3(sourceP,0));
    const bool stationaryCoverage = (TemporalFlags & 1u) != 0u;
	// Kill only isolated HDR outliers before they poison temporal history. This
	// depth-aware cross filter leaves normal edges and authored bright regions
	// alone, while taming rare one-sample DXR reflection/AO fireflies.
	const float centerLuma = ToYCoCg(current).x;
	float3 crossSum = current; float crossWeight = 1.0f;
	[unroll] for (int axis = 0; axis < 4; ++axis) {
		const int2 o = axis == 0 ? int2(-1,0) : axis == 1 ? int2(1,0) : axis == 2 ? int2(0,-1) : int2(0,1);
		const int2 n = clamp(sourceP + o, int2(0,0), int2(Size)-1);
		const float nd = CurrentDepth.Load(int3(n,0));
		const float3 nc = max(CurrentColor.Load(int3(n,0)).rgb,0);
		const float w = (depth >= 1.0f && nd >= 1.0f) || (depth < 1.0f && nd < 1.0f && abs(LinearDepth(nd)-LinearDepth(depth)) <= max(1.0f, LinearDepth(depth)*0.02f)) ? 1.0f : 0.0f;
		crossSum += nc * w; crossWeight += w;
	}
	const float3 crossMean = crossSum / crossWeight;
	const float firefly = saturate((centerLuma / max(ToYCoCg(crossMean).x, 0.03f) - 3.0f) / 5.0f);
	current = lerp(current, crossMean, firefly * 0.65f);
    const float2 previousPixel = float2(p)+0.5f + (stationaryCoverage ? float2(0,0) : motion);
    bool reject = HistoryValid == 0u || metadata.x < 0.5f || !all(isfinite(previousPixel)) ||
        any(previousPixel < 0.5f) || any(previousPixel > float2(Size)-0.5f);
    float3 history = current;
    if (!reject) {
        const float2 previousUv = previousPixel / float2(Size);
        if (!stationaryCoverage) {
            // Match depths supporting the bilinear history footprint rather
            // than rejecting a thin surface against one point sample.
            const int2 base = int2(floor(previousPixel-0.5f));
            const bool sky = depth >= 1.0f;
            const float expected = LinearDepth(metadata.y);
            const float2 fraction = frac(previousPixel-0.5f);
            bool depthMatch = false;
            [unroll] for (int dy=0;dy<2;++dy) [unroll] for (int dx=0;dx<2;++dx) {
                const int2 hp = clamp(base+int2(dx,dy),int2(0,0),int2(Size)-1);
                const float oldDepth = HistoryDepth.Load(int3(hp,0));
                const float weightX = dx == 0 ? 1.0f-fraction.x : fraction.x;
                const float weightY = dy == 0 ? 1.0f-fraction.y : fraction.y;
                depthMatch = depthMatch || (weightX*weightY > 0.05f && (sky ? oldDepth >= 1.0f :
                    oldDepth < 1.0f && abs(LinearDepth(oldDepth)-expected) <= max(1.0f,expected*0.02f)));
            }
            reject = !depthMatch;
        }
        history = HistoryColor.SampleLevel(LinearClamp,previousUv,0).rgb;
		// Guides are material IDs in disguise.  Bilinear filtering them across a
		// thin edge creates a fictitious normal/roughness, then rejects valid
		// history on alternating frames.  Depth already validated the footprint;
		// select the nearest matching guide instead.
		const int2 guidePixel = clamp(int2(floor(previousPixel)), int2(0,0), int2(Size)-1);
		const float4 historyGuide = HistorySurfaceGuide.Load(int3(guidePixel,0));
		// Do not reuse a surface's noisy history across a normal/roughness edge.
		// This makes the current resolve materially closer to an NRD/DLAA guide path.
		const bool surfaceMatch = currentGuide.w < 0.0f ? historyGuide.w < 0.0f :
			historyGuide.w >= 0.0f && dot(currentGuide.xyz, historyGuide.xyz) > 0.84f && abs(currentGuide.w - historyGuide.w) < 0.30f;
		reject = reject || !surfaceMatch;
        reject = reject || !all(isfinite(history));
    }
    float3 lo = ToYCoCg(current), hi = lo;
    float3 mean = 0.0f, meanSq = 0.0f;
    [unroll] for (int y=-1;y<=1;++y) [unroll] for (int x=-1;x<=1;++x) {
        const int2 neighbor = clamp(sourceP+int2(x,y),int2(0,0),int2(Size)-1);
        // Keep raw extrema so subpixel coverage history survives clamping.
        const float3 c = ToYCoCg(max(CurrentColor.Load(int3(neighbor,0)).rgb,0));
        lo = min(lo,c); hi = max(hi,c);
        mean += c;
        meanSq += c * c;
    }
    mean *= (1.0f / 9.0f);
    // Variance expansion retains legitimate HDR highlights while clipping
    // fireflies from AO, glossy reflections, and ray-traced shadows.
    const float3 sigma = sqrt(max(meanSq * (1.0f / 9.0f) - mean * mean, 0.0f));
    lo = min(lo, mean - sigma * 1.25f);
    hi = max(hi, mean + sigma * 1.25f);
    // Only unchanged camera AND all rigid geometry permit coverage accumulation
    // without depth/color clipping. Moving scenes keep both protections.
    const float3 clippedHistory = stationaryCoverage ? max(history,0) : max(ToRGB(clamp(ToYCoCg(history),lo,hi)),0);
    // Motion/depth validation and neighborhood clipping protect disocclusions.
    // Lighting changes on a static receiver remain temporally filtered: trying
    // to infer them from per-pixel luminance also reacts to DXR noise and turns
    // every moving emissive into full-screen TAA shimmer.
    const float lumaDelta = abs(ToYCoCg(history).x - ToYCoCg(current).x) /
        max(max(ToYCoCg(history).x, ToYCoCg(current).x), 0.05f);
    // Keep history for stochastic DXR noise, but rapidly fade it at real
    // illumination discontinuities so moving AO/shadows and glossy highlights
    // do not leave trails.
    const float reactive = saturate((lumaDelta - 0.15f) / 0.85f);
    // A glossy surface legitimately changes luminance between stochastic GGX
    // samples. Do not throw away almost all history on those pixels: depth and
    // the normal/roughness guide already reject real disocclusions and light
    // edits clear the history globally.
    const float smoothSpecular = 1.0f - saturate((currentGuide.w - 0.08f) / 0.42f);
    const float reactiveForSpecular = reactive * lerp(1.0f, 0.25f, smoothSpecular);
    float weight = reject ? 0 : saturate(HistoryWeight) *
        lerp(1.0f,0.88f,saturate(length(motion)/8.0f)) * lerp(1.0f,0.35f,reactiveForSpecular);
    float3 result = lerp(current,clippedHistory,weight);
    result = all(isfinite(result)) ? min(result,65504.0f) : current;
    ResolvedColor[p] = float4(result,1);
    ResolvedDepth[p] = depth;
	ResolvedSurfaceGuide[p] = currentGuide;
    // Store beauty in history even while a diagnostic is displayed.
    const float3 debugColor = DebugView == 1u ? (reject ? float3(1,0,1) : max(history,0)) :
        (reject ? float3(1,0,0) : float3(0,1,0));
    Diagnostic[p] = float4(debugColor,1);
}
