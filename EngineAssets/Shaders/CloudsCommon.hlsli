#ifndef CLOUDS_COMMON_HLSLI
#define CLOUDS_COMMON_HLSLI
#include "CloudsConstants.hlsli"

// Shared cloud density field: the baking compute shaders (CloudShapeNoiseCS,
// CloudDetailNoiseCS), the hero raymarch (CloudsVolumeCS), the coarse
// ambient contribution baked into the sky cubemap (SkyCubemapPS), and the
// cloud-shadow lookup fed into the existing volumetric-sunlight march
// (AtmosphereVolumeMain.hlsli) all call the same SampleCloudDensity /
// CloudShadowFactor here, so "what a cloud looks like" and "what shadow it
// casts" can never quietly disagree with each other.

// ---- hash / noise building blocks (used both at bake time and, for the
// coarse cloud-shadow estimate, directly at sample time) ------------------
float3 CloudsHash33(float3 p)
{
	p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
	p += dot(p, p.yxz + 33.33f);
	return frac((p.xxy + p.yxx) * p.zyx);
}

float CloudsHash13(float3 p)
{
	p = frac(p * 0.1031f);
	p += dot(p, p.yzx + 33.33f);
	return frac((p.x + p.y) * p.z);
}

// Wraps an integer lattice cell into [0, period) before hashing, so cell 0
// and cell `period` (which land on the same texel once the baked volume is
// sampled with wrap addressing) hash to the SAME value. Without this, the
// noise has no periodicity at all -- it just happens to be continuous -- so
// the wrap-sampled edges never actually meet and the seam shows up as a hard
// discontinuity where the last texel meets the first.
float3 CloudsWrapCell(float3 cell, float period)
{
	return cell - period * floor(cell / period);
}

float CloudsHash13Tileable(float3 p, float period)
{
	return CloudsHash13(CloudsWrapCell(p, period));
}

float3 CloudsHash33Tileable(float3 p, float period)
{
	return CloudsHash33(CloudsWrapCell(p, period));
}

// period: the lattice repeats every `period` integer cells along each axis
// -- must match how many times the sampled domain wraps around the baked
// volume for the result to tile seamlessly.
float CloudsValueNoise3D(float3 p, float period)
{
	float3 i = floor(p);
	float3 f = frac(p);
	f = f * f * (3.0f - 2.0f * f);
	float n000 = CloudsHash13Tileable(i + float3(0, 0, 0), period);
	float n100 = CloudsHash13Tileable(i + float3(1, 0, 0), period);
	float n010 = CloudsHash13Tileable(i + float3(0, 1, 0), period);
	float n110 = CloudsHash13Tileable(i + float3(1, 1, 0), period);
	float n001 = CloudsHash13Tileable(i + float3(0, 0, 1), period);
	float n101 = CloudsHash13Tileable(i + float3(1, 0, 1), period);
	float n011 = CloudsHash13Tileable(i + float3(0, 1, 1), period);
	float n111 = CloudsHash13Tileable(i + float3(1, 1, 1), period);
	float nx00 = lerp(n000, n100, f.x);
	float nx10 = lerp(n010, n110, f.x);
	float nx01 = lerp(n001, n101, f.x);
	float nx11 = lerp(n011, n111, f.x);
	float nxy0 = lerp(nx00, nx10, f.y);
	float nxy1 = lerp(nx01, nx11, f.y);
	return lerp(nxy0, nxy1, f.z);
}

// basePeriod: the period of octave 0, in integer cells. Each doubling of
// frequency also doubles that octave's period (2x as many cells fit in the
// same span), which is why this only tiles correctly when basePeriod is a
// whole number -- fractional periods can't wrap on an integer cell lattice.
float CloudsFbm3D(float3 p, int octaves, float basePeriod)
{
	float sum = 0.0f, amp = 0.5f, freq = 1.0f;
	for (int i = 0; i < octaves; ++i)
	{
		sum += CloudsValueNoise3D(p * freq, basePeriod * freq) * amp;
		freq *= 2.0f; amp *= 0.5f;
	}
	return sum;
}

// Cellular (Worley) noise: distance from p to the nearest of one jittered
// point per neighboring unit cell -- the "cauliflower" cloud silhouette.
float CloudsWorley3D(float3 p, float period)
{
	float3 cell = floor(p);
	float3 f = frac(p);
	float minDistSq = 3.0f;
	[unroll] for (int z = -1; z <= 1; ++z)
	[unroll] for (int y = -1; y <= 1; ++y)
	[unroll] for (int x = -1; x <= 1; ++x)
	{
		float3 neighbor = float3(x, y, z);
		float3 cellPoint = neighbor + CloudsHash33Tileable(cell + neighbor, period) * 0.9f + 0.05f;
		float3 diff = cellPoint - f;
		minDistSq = min(minDistSq, dot(diff, diff));
	}
	return saturate(sqrt(minDistSq));
}

float CloudsRemap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
	return newMin + saturate((value - oldMin) / max(1e-5f, oldMax - oldMin)) * (newMax - newMin);
}

// A divergence-free ("curl") 2D vector field: the rotated gradient of a
// scalar potential (finite-differenced value noise) is guaranteed
// non-divergent, i.e. it warps space by swirling it rather than by pushing
// it apart or pinching it together -- the standard cheap stand-in for fluid
// turbulence (Schneider/HZD use a baked curl texture; this is the same idea
// evaluated analytically so it never needs its own baked texture or an
// extra binding threaded through every call site). Only ever used to offset
// a SAMPLE position, so it needs no tiling of its own -- an arbitrary large
// period is enough for the finite-difference taps to stay well clear of any
// wrap seam within the range clouds are actually sampled over.
float2 CloudsCurl2D(float3 p)
{
	const float eps = 0.06f;
	const float period = 512.0f;
	float n1 = CloudsValueNoise3D(p + float3(0.0f, eps, 0.0f), period);
	float n2 = CloudsValueNoise3D(p - float3(0.0f, eps, 0.0f), period);
	float n3 = CloudsValueNoise3D(p + float3(eps, 0.0f, 0.0f), period);
	float n4 = CloudsValueNoise3D(p - float3(eps, 0.0f, 0.0f), period);
	float dNdy = (n1 - n2) / (2.0f * eps);
	float dNdx = (n3 - n4) / (2.0f * eps);
	return float2(dNdy, -dNdx);   // rotate the gradient 90 degrees
}

// ---- density field --------------------------------------------------------
// Cumulus-ish vertical profile: rises quickly off the base, tapers into a
// top whose HEIGHT varies per column via `growth` (0..1, driven by the base
// shape noise at the call site) -- without this every puff in the field
// shares the exact same vertical silhouette just scaled by density, which
// reads as a flat, uniform slab no matter how noisy the horizontal shape
// is. Weak columns stay low and wispy; strong ones build into taller
// towers, the way real convective cumulus does.
float CloudHeightGradient(float heightFraction, float growth)
{
	float bottom = smoothstep(0.0f, 0.2f, heightFraction);
	float topStart = lerp(0.4f, 1.0f, growth);
	float top = smoothstep(0.0f, 0.3f, topStart - heightFraction);   // rounded, not a linear cut
	return bottom * top;
}

// worldPosMeters: world position in meters (callers convert from the
// engine's native centimeters). aCheap skips the fine detail-erosion sample,
// for call sites that evaluate this many times per pixel (the shadow lookup)
// or don't need full fidelity (the sky cubemap's ambient contribution).
//
// shapeNoise layout (baked by CloudShapeNoiseCS, see its comment for why):
//   r: "Perlin-Worley" -- FBM remapped by an inverted-Worley floor (HZD's
//      technique) rather than blended with it, so the cellular structure
//      carves the FBM into one connected, billowy shape instead of two
//      textures visibly overlaid.
//   g, b: two more Worley octaves at increasing frequency, baked alongside
//      it and used to erode the base shape at the SHAPE-texture stage --
//      this is what breaks one big smooth lobe into several differently
//      sized puffs instead of a single round "popcorn ball" per cell.
// footprintMeters: how much world space one sample stands for (the march
// step, or the pixel's angular size at that distance). Selects a noise mip so
// far, coarse samples read a pre-averaged field instead of aliasing the 6 km
// tile into moire rows toward the horizon. 0 = full detail.
// heightMeters: altitude of the sample above the (curved) sea level, when the
// caller marches a spherical shell (CloudsVolumeCS); < -1e8 = use worldPos.y,
// i.e. the flat approximation the cheap consumers still use.
float SampleCloudDensity(Texture3D<float4> shapeNoise, Texture3D<float> detailNoise, SamplerState samp,
                          float3 worldPosMeters, bool aCheap, float footprintMeters = 0.0f, float heightMeters = -1e9f)
{
	if (gCloudsEnabled == 0 || gCloudTopAltitude <= gCloudBaseAltitude) return 0.0f;
	float altitude = heightMeters > -1e8f ? heightMeters : worldPosMeters.y;
	float heightFraction = (altitude - gCloudBaseAltitude) / (gCloudTopAltitude - gCloudBaseAltitude);
	if (heightFraction <= 0.0f || heightFraction >= 1.0f) return 0.0f;

	float2 wind = gCloudSpeed * gTime;
	float3 shapeUvw = (worldPosMeters + float3(wind.x, 0.0f, wind.y)) / max(1.0f, gCloudScale);
	// Vertical period pinned to 3 km against a 1.5 km layer: with the tile's
	// own period spanning the layer, density hardly changed with height,
	// every column was a flat-topped plateau and, seen obliquely, the deck
	// was a stack of terraces. This factor tracks gCloudScale -- everything
	// below is in tile units, so widening the tile alone would scale the
	// whole cloud field up rather than just push its repeat further out.
	shapeUvw.y *= max(1.0f, gCloudScale / 3000.0f);
	uint sw, sh, sd; shapeNoise.GetDimensions(sw, sh, sd);
	float texelMeters = max(1.0f, gCloudScale) / max(1u, sw);
	// Capped at mip 2 (32^3 effective): coarser mips of a Worley volume are
	// literally rectangles, and read as boxy blobs when the deck is seen from
	// above or at a distance. Beyond this the aerial fade hides the period.
	float shapeMip = min(2.0f, log2(max(1.0f, footprintMeters / texelMeters)));
	float3 shape = shapeNoise.SampleLevel(samp, frac(shapeUvw), shapeMip).rgb;
	// Second read of the same volume through a rotated, irrationally scaled
	// domain: the two tilings never line up, so no cloud ever repeats exactly
	// and the tile period stops being readable across a 100 km deck.
	const float kCa = 0.8480f, kSa = 0.5299f;   // cos/sin 32 degrees
	float3 alt = shapeUvw / 1.6180f;
	float3 altUvw = float3(alt.x * kCa - alt.z * kSa + 0.417f, alt.y + 0.29f, alt.x * kSa + alt.z * kCa + 0.733f);
	float3 shapeB = shapeNoise.SampleLevel(samp, frac(altUvw), shapeMip).rgb;
	// A small zero-mean perturbation from the rotated domain. Stronger mixes
	// were tried: an equal blend halves the field's variance, and both larger
	// additive and multiplicative modulation thicken the deck into overcast,
	// because the base field is floored at zero by the Worley remap. 0.3 is
	// enough to stop exact repeats without changing coverage; the far-field
	// period is hidden by the mip selection and the aerial fade instead.
	shape.r = saturate(shape.r + (shapeB.r - 0.5f) * 0.3f);

	float base = shape.r;
	base = saturate(base - (1.0f - shape.g) * 0.25f);
	base = saturate(base - (1.0f - shape.b) * 0.15f);

	// Large-scale coverage variation: one extra tap of the same shape volume
	// at a ~48 km tile so the deck has open and dense regions instead of the
	// shape tile visibly repeating toward the horizon. Held at a fixed size
	// in meters: the aerial fade hides the deck past ~40 km, so a coverage
	// period much larger than that reads as one open half and one dense half
	// rather than as variety.
	float largeFreq = min(1.0f, gCloudScale / 48000.0f);
	float large = shapeNoise.SampleLevel(samp, frac(shapeUvw * largeFreq + float3(0.37f, 0.11f, 0.73f)), 0).r;
	float coverage = saturate(gCloudCoverage * lerp(0.55f, 1.45f, large));
	// Coverage-as-threshold: only noise above (1-coverage) survives, remapped
	// back to [0,1] -- the standard cheap way to make "coverage" read as an
	// actual sky-fraction knob instead of just biasing brightness.
	float density = saturate((base - (1.0f - coverage)) / max(0.05f, coverage));
	// `base` also drives how tall this column's silhouette builds -- see
	// CloudHeightGradient -- so the field reads as puffs of varied height
	// instead of one uniform slab.
	// Squared: with coverage thresholding most surviving columns have base
	// > 0.5, and a linear growth term then gave every puff the same flat top,
	// which from above read as an extruded plateau with holes.
	float columnGradient = CloudHeightGradient(heightFraction, base * base);
	density *= columnGradient;

	if (!aCheap && density > 0.0f && gCloudDetailStrength > 0.0f)
	{
		// Curl-warp the erosion sample position: without this, erosion is
		// just a second smooth noise layer subtracted from the first, which
		// still reads as round/blobby -- swirling the sample position is
		// what turns that into actual wispy, turbulent structure at the
		// cloud's boundary instead of a slightly-fuzzier sphere.
		// Erosion frequencies are pinned to a size in meters rather than to
		// the shape tile: wisps and billows are a property of clouds, not of
		// how far away we pushed the repeat, so widening gCloudScale must not
		// inflate them. kDetailTile is the coarse erosion period in meters.
		const float kDetailTile = 1500.0f;
		float detailFreq = max(1.0f, gCloudScale / kDetailTile);
		float2 curl = CloudsCurl2D(shapeUvw * detailFreq * 0.125f);
		// 1.5 km of swirl, also in meters rather than in tile units.
		float3 warpedUvw = shapeUvw + float3(curl.x, 0.0f, curl.y) * (1500.0f / max(1.0f, gCloudScale));
		float detailMip = max(0.0f, shapeMip + log2(detailFreq));
		// shapeUvw.y already carries the shape volume's vertical squeeze;
		// undo the part of it that scales with the tile so the erosion keeps
		// a 750 m vertical period through a 1500 m layer, i.e. billows stay
		// about half the layer's height whatever gCloudScale is.
		float3 detailUvw = warpedUvw * detailFreq;
		detailUvw.y *= 4.0f / detailFreq;
		float detail = detailNoise.SampleLevel(samp, frac(detailUvw), detailMip).r;
		float fineDetail = detailNoise.SampleLevel(samp, frac(detailUvw * 2.25f + 0.37f), detailMip + 1.17f).r;
		float erosion = detail * 0.7f + fineDetail * 0.3f;
		// "If you invert the Worley noise at the base of the clouds you get
		// nice whispy shapes" (Schneider/HZD) -- near the cloud's own base
		// blend toward inverted erosion so density trails off in wisps
		// there, instead of just fading uniformly.
		float baseBlend = saturate(1.0f - heightFraction / 0.3f);
		erosion = lerp(erosion, 1.0f - erosion, baseBlend);
		// Edge-weighted: CloudHeightGradient is ~0 right at the top/bottom
		// of this column's silhouette and ~1 through its body, so 1-that is
		// large at the edges and small in the core -- erode the boundary
		// into wisps and billows while keeping a dense, mostly-intact core.
		// The lerp floor keeps a little surface grain everywhere so the
		// interior doesn't read as a flat, noise-free wall either.
		float edgeWeight = 1.0f - columnGradient;
		density = saturate(density - erosion * gCloudDetailStrength * lerp(0.35f, 1.0f, edgeWeight));
	}

	return density * gCloudDensity;
}

// Coarse "how much cloud is between this point and the sun" estimate for the
// existing volumetric-sunlight march (AtmosphereVolumeMain.hlsli) -- a fixed
// handful of steps through the cloud shell along the sun direction, not a
// full raymarch, since this runs once per existing fog-shaft sample.
float CloudShadowFactor(Texture3D<float4> shapeNoise, Texture3D<float> detailNoise, SamplerState samp,
                         float3 worldPosMeters, float3 sunDir)
{
	if (gCloudsEnabled == 0 || sunDir.y <= 0.01f) return 1.0f;
	float tBase = (gCloudBaseAltitude - worldPosMeters.y) / sunDir.y;
	float tTop = (gCloudTopAltitude - worldPosMeters.y) / sunDir.y;
	float t0 = max(0.0f, min(tBase, tTop));
	float t1 = max(tBase, tTop);
	if (t1 <= t0) return 1.0f;

	const int kShadowSteps = 4;
	float stepLen = (t1 - t0) / kShadowSteps;
	float opticalDepth = 0.0f;
	[unroll] for (int i = 0; i < kShadowSteps; ++i)
	{
		float3 samplePos = worldPosMeters + sunDir * (t0 + (i + 0.5f) * stepLen);
		opticalDepth += SampleCloudDensity(shapeNoise, detailNoise, samp, samplePos, true) * stepLen;
	}
	// Extinction-per-meter is folded into gCloudDensity already; this extra
	// factor just keeps a fully overcast shell from reading as a hard black
	// wall of shadow -- clouds attenuate the shafts, they don't erase them.
	return exp(-opticalDepth * 0.01f);
}

#endif
