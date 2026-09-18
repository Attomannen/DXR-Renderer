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
// cloudType: 0 = stratus, a shallow sheet lying along the base of the layer;
// 1 = cumulus, a tall tower with a rounded top. Driven by a field that varies
// over tens of kilometres, so one part of the sky carries flat sheet cloud and
// another carries heaped towers. A single profile, however much its height was
// modulated, still gave every cloud the same silhouette; real skies mix cloud
// of genuinely different kinds, and that mixture is most of what reads as
// "variation" rather than "one noise field at one scale".
float CloudHeightGradient(float heightFraction, float growth, float cloudType = 1.0f)
{
	// A sheet sits low and ends abruptly; a tower climbs and rounds off.
	//
	// The weak end of the cumulus range is 0.15, not 0.45. That single number
	// is most of why every cloud looked the same height: at 0.45 the shortest
	// possible cumulus still filled nearly half the layer, so the tallest was
	// only about twice the shortest and the deck read as one slab with a bumpy
	// top. At 0.15 the range is closer to seven to one, which is the sort of
	// spread real convection produces -- flat scraps of cloud in the same sky
	// as towers that climb the whole layer.
	const float stratusTop = 0.18f;
	const float cumulusTop = lerp(0.15f, 1.0f, growth);
	const float topStart = lerp(stratusTop, cumulusTop, cloudType);
	// Cumulus forms at the condensation level, which is one altitude across
	// the whole sky, so its base is close to planar -- that hard flat
	// underside is one of the most recognisable things about the shape. A
	// gradual base was making towers look like floating blobs. The taller the
	// column, the sharper the cut, because a tall column is a strong updraught
	// with a well-defined base.
	const float bottom = smoothstep(0.0f, lerp(0.08f, 0.035f, growth), heightFraction);
	// The top rounds off over a distance that scales with how tall the column
	// is, so a tower gets a broad cauliflower crown rather than the same small
	// rounding a scrap of cloud gets.
	const float top = smoothstep(0.0f, lerp(0.10f, 0.40f, cloudType) * lerp(0.5f, 1.0f, growth), topStart - heightFraction);
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
	float layerFraction = (altitude - gCloudBaseAltitude) / (gCloudTopAltitude - gCloudBaseAltitude);
	if (layerFraction <= 0.0f || layerFraction >= 1.0f) return 0.0f;

	float2 wind = gCloudSpeed * gTime;
	float3 shapeUvw = (worldPosMeters + float3(wind.x, 0.0f, wind.y)) / max(1.0f, gCloudScale);
	// Vertical period pinned to THREE layer thicknesses.
	//
	// It used to be a flat 3 km, chosen when the layer was 1.5 km thick to stop
	// every column being a flat-topped plateau. Against the taller layer that
	// number became the thing preventing towers: the height profile would
	// happily let a strong column climb three kilometres, but the shape volume
	// ran through most of a period over that distance, so the column was dense
	// near its base and empty higher up whatever the profile allowed. Every
	// cloud came out a wide flat pancake no matter how much vertical room it
	// had. At three layer-thicknesses a column varies gently over its own
	// height -- enough to keep tops from being planar, little enough that the
	// profile is what decides where a cloud ends, which is the whole point of
	// having a profile. Two was still steep enough to contribute to the
	// horizontal banding on tall towers.
	const float verticalSqueeze = max(0.25f, gCloudScale / (3.0f * max(1.0f, gCloudTopAltitude - gCloudBaseAltitude)));
	shapeUvw.y *= verticalSqueeze;
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

	// Erode by REMAPPING the base rather than subtracting from it. Subtracting
	// lowers every value by the same amount, so a cloud's solid interior is
	// dimmed just as much as its fringe and the whole field ends up thin and
	// ragged -- the shape gets chewed instead of carved. Remapping rescales
	// what survives back up to full density, so the interior stays solid and
	// only the fringe is cut away. This is the form Schneider uses and it is
	// the single biggest difference between clouds that look torn and clouds
	// that look billowed.
	const float shapeErosion = min(0.9f, (1.0f - shape.g) * 0.25f + (1.0f - shape.b) * 0.15f);
	float base = CloudsRemap(shape.r, shapeErosion, 1.0f, 0.0f, 1.0f);

	// Large-scale coverage variation: one extra tap of the same shape volume
	// at a ~48 km tile so the deck has open and dense regions instead of the
	// shape tile visibly repeating toward the horizon. Held at a fixed size
	// in meters: the aerial fade hides the deck past ~40 km, so a coverage
	// period much larger than that reads as one open half and one dense half
	// rather than as variety.
	// Weather is a map over the GROUND, not a volume. Both low-frequency
	// fields below were being read at the sample's full 3D position, so
	// coverage and growth changed as a ray climbed through a cloud. Near the
	// coverage threshold a small change in altitude then swung density hard,
	// which cut tall columns into horizontal slabs -- the stacked-discs look
	// on the big towers. Sampling them at a fixed height makes each an honest
	// 2D weather field: one coverage value and one growth value per column,
	// whatever altitude is being shaded.
	const float3 weatherUvw = float3(shapeUvw.x, 0.41f, shapeUvw.z);
	float largeFreq = min(1.0f, gCloudScale / 48000.0f);
	// Both channels of one read. .r is the Perlin-Worley shape and .g is an
	// independent Worley octave baked beside it, so this costs nothing extra
	// and gives a cloud-type field that is not simply a copy of the coverage
	// field -- dense weather is not always heaped weather.
	const float2 largeSample = shapeNoise.SampleLevel(samp, frac(weatherUvw * largeFreq + float3(0.37f, 0.11f, 0.73f)), 0).rg;
	const float large = largeSample.r;
	// Stretched to use the full 0..1 range: Worley noise clusters around its
	// mean, and an unstretched field would sit near "half stratus, half
	// cumulus" everywhere and blend the two profiles into one average shape,
	// which is the opposite of the point.
	const float cloudType = saturate(largeSample.g * 1.8f - 0.4f);
	// A second coverage field at roughly a 14 km period, an order of magnitude
	// finer than the 48 km one. The 48 km field is so broad that within any
	// one view it is nearly constant, so every cloud faced the same threshold
	// and every cloud came out the same size. Varying the threshold over a
	// distance comparable to the clouds themselves is what makes some of them
	// survive as broad masses and others as small scraps: near the top of this
	// field a wide plateau of the base noise clears the cut, near the bottom
	// only the isolated peaks do. This is the one extra texture read in the
	// function and it is what buys the size variation.
	const float midFreq = min(1.0f, gCloudScale / 14000.0f);
	const float2 midSample = shapeNoise.SampleLevel(samp, frac(weatherUvw * midFreq + float3(0.83f, 0.29f, 0.51f)), 0).rg;
	const float midField = midSample.r;
	// Scaled to 0.75 of what it was. Remapping the shape erosion above instead
	// of subtracting it raises the base field by roughly a quarter -- that is
	// the point, it is what keeps cloud interiors solid -- but the coverage
	// threshold is applied to that same field, so leaving this alone would
	// have turned every existing scene overcast. 0.75 is the ratio that keeps
	// a given gCloudCoverage covering about the sky fraction it covered
	// before, so the authored value still means what it used to.
	// Not every cloud starts at the same altitude. A single condensation level
	// is right for one air mass, but a real sky stacks several: low cloud under
	// mid-level cloud under high cloud, and that separation in depth is most of
	// what makes a sky read as a volume rather than as a ceiling. Each weather
	// region now gets its own floor within the layer, so some clouds sit low
	// and others ride well above them.
	//
	// The region's sub-layer is renormalised rather than offset, so everything
	// still ends at the top of the shell the march actually traverses -- an
	// offset would push high clouds through the roof and get them clipped flat.
	const float bandFloor = saturate(midSample.g) * 0.4f;
	const float heightFraction = saturate((layerFraction - bandFloor) / max(0.25f, 1.0f - bandFloor));
	float coverage = saturate(gCloudCoverage * lerp(0.41f, 1.09f, large) * lerp(0.55f, 1.35f, midField));
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
	//
	// Driven by the MID-scale field, not the large one. Height needs to vary
	// across a single view to be seen varying at all, and the 48 km field is
	// so broad that one view sits almost entirely inside one value of it --
	// every cloud on screen then got the same growth and the same height, no
	// matter how wide the profile's range was. The 14 km field changes several
	// times across a view, so towers cluster into groups a few kilometres
	// across with flatter cloud between them, which is how convection
	// actually organises itself.
	const float growth = saturate(base * base * 0.55f + midField * 0.75f - 0.15f);
	float columnGradient = CloudHeightGradient(heightFraction, growth, cloudType);
	density *= columnGradient;
	// A tall cloud is not merely taller, it is denser: it holds more water and
	// more of it per metre, which is why a cumulus tower has a heavy grey base
	// and a scrap of fair-weather cloud is translucent all the way through.
	// Without this the field varied only in silhouette, and a tower lit and
	// shaded exactly like the flat cloud beside it -- so the eye read the
	// whole deck as one uniform material at one altitude.
	density *= lerp(0.45f, 1.35f, growth);

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
		// Selected from this volume's own texel size, not by offsetting the
		// shape mip. The offset was a constant log2(detailFreq) -- at a 9 km
		// tile that is +2.58 mips applied at every distance, including point
		// blank, so a 32^3 volume was being read as though it were 5^3 on the
		// nearest cloud in the sky. That is the softness: the erosion detail
		// was mipped away before it ever reached the screen. The two volumes
		// have different texel sizes in world space and there is no fixed
		// offset between their correct mips; each has to be measured against
		// the pixel footprint separately.
		uint dw, dh, dd; detailNoise.GetDimensions(dw, dh, dd);
		const float detailTexelMeters = kDetailTile / max(1u, dw);
		float detailMip = min(3.0f, log2(max(1.0f, footprintMeters / detailTexelMeters)));
		// Undo the shape volume's vertical squeeze completely, so the erosion
		// is isotropic in world space. It inherited that squeeze through
		// warpedUvw and came out roughly twice as tall as it was wide, which
		// makes billows that are wider than they are high -- flat lobes
		// stacked up a column, which is the other half of why tall clouds read
		// as a pile of discs rather than one heaped mass. Cauliflower is
		// isotropic; that is what makes it read as cauliflower.
		float3 detailUvw = warpedUvw * detailFreq;
		detailUvw.y /= verticalSqueeze;
		float detail = detailNoise.SampleLevel(samp, frac(detailUvw), detailMip).r;
		float fineDetail = detailNoise.SampleLevel(samp, frac(detailUvw * 2.25f + 0.37f), detailMip + 1.17f).r;
		float erosion = detail * 0.7f + fineDetail * 0.3f;
		// "If you invert the Worley noise at the base of the clouds you get
		// nice whispy shapes" (Schneider/HZD) -- near the cloud's own base
		// blend toward inverted erosion so density trails off in wisps
		// there, instead of just fading uniformly.
		// ...but only on sheet cloud. Stratus really does trail off into wisps
		// underneath; cumulus does not, and applying this to everything was
		// fraying exactly the flat base the profile is trying to produce.
		float baseBlend = saturate(1.0f - heightFraction / 0.3f) * (1.0f - cloudType);
		erosion = lerp(erosion, 1.0f - erosion, baseBlend);
		// Edge-weighted: CloudHeightGradient is ~0 right at the top/bottom
		// of this column's silhouette and ~1 through its body, so 1-that is
		// large at the edges and small in the core -- erode the boundary
		// into wisps and billows while keeping a dense, mostly-intact core.
		// The lerp floor keeps a little surface grain everywhere so the
		// interior doesn't read as a flat, noise-free wall either.
		float edgeWeight = 1.0f - columnGradient;
		// Remapped, for the same reason the shape erosion above is: subtracting
		// the erosion noise from the density took it out of the cloud's core as
		// well as its edge, which both thinned the whole deck and printed the
		// erosion noise directly onto surfaces that should read as solid. That
		// is the scratchiness. Remapping leaves the core at full density and
		// spends the whole erosion budget on the fringe.
		// Weighted toward the top as well as toward the edges. Eroding the
		// underside as hard as the crown rounds off the flat base the height
		// profile just built, and a cumulus with a rounded base reads as a
		// floating ball of cotton rather than a cloud sitting on its
		// condensation level.
		// The floors here are what the erosion keeps in the cloud's BODY, and
		// they were set low to protect the flat base. Too low: at 0.2 and 0.3
		// the interior received six percent of the erosion budget, so the sides
		// and the middle of every cloud came out smooth and each one looked
		// like the next. Raised to 0.45 and 0.55, the body now carries real
		// erosion while the crown and the edges still get roughly twice as
		// much, which is the ordering that matters -- a cumulus is rougher at
		// its top than in its middle, not smooth everywhere but the rim.
		const float crown = smoothstep(0.05f, 0.55f, heightFraction);
		const float erosionAmount = min(0.9f, erosion * gCloudDetailStrength
			* lerp(0.45f, 1.0f, edgeWeight) * lerp(0.55f, 1.0f, crown));
		density = CloudsRemap(density, erosionAmount, 1.0f, 0.0f, 1.0f);

		// Structure INSIDE the cloud, as a modulation rather than as more
		// erosion. Erosion can only ever remove density, and it is deliberately
		// weighted toward the edges and the crown, so the body of a cloud had
		// nothing varying in it at all and lit like a smooth ball of milk. This
		// varies density about its own mean instead: the silhouette and the
		// average optical depth are left alone, and the interior gains billows
		// for the light to catch. Weighted by columnGradient so it applies in
		// the body and fades out at the fringe, where erosion already rules.
		//
		// This is not per-sample randomness and there is nothing here for a
		// denoiser to fight. It is a function of world position, read from the
		// same detail volume at the same footprint-selected mip as the erosion,
		// so it reprojects exactly like the cloud it belongs to and is
		// band-limited by distance the same way. That mip selection is what
		// keeps it from turning into temporal noise under the march's step
		// jitter -- a grain finer than the sampling would do exactly that, and
		// is the thing worth avoiding here.
		const float grain = lerp(detail, fineDetail, 0.35f);
		const float grainAmp = 0.8f * gCloudDetailStrength;
		// Applied everywhere, NOT weighted toward the body. Weighting it by
		// columnGradient was backwards: what the eye reads as a cloud's surface
		// is the shell where accumulated transmittance falls off, and that sits
		// at the fringe, exactly where columnGradient is small and the grain was
		// being faded out. The interior it was strongest in is already opaque,
		// so nothing there is visible at all. Varying density through the outer
		// shell is what makes that iso-surface bumpy instead of smooth, which is
		// the difference between cauliflower and a soft lobe.
		// A strong, near-binary CARVE rather than a gentle modulation.
		//
		// A cloud is optically thick, so what the eye reads as its surface is
		// the shell where accumulated transmittance falls off. Moving that
		// shell enough to be seen takes a large change in density: a plus or
		// minus fifty percent modulation shifted it by almost nothing and was
		// invisible, while cutting density to a twentieth carved obvious
		// cauliflower. The response is that nonlinear, so the grain has to be
		// close to binary to register at all.
		//
		// Smoothstepped rather than thresholded, because a hard cut on a
		// mip-selected noise aliases badly up close, and scaled by the detail
		// slider so turning it down still gives smooth clouds.
		const float kCarveWidth = 0.18f;
		const float kCarveDepth = 0.95f;
		const float carve = smoothstep(0.5f - kCarveWidth, 0.5f + kCarveWidth, grain);
		// Depth is absolute, not scaled by the detail slider. Scaling it turned
		// a 0.95 carve into a 0.57 one at the default slider position, which
		// lands back in the range that is too weak to move the transmittance
		// shell at all. The slider still gates the whole detail block, so
		// turning it to zero still gives smooth clouds.
		density *= lerp(1.0f - kCarveDepth, 1.0f, carve);
		density = saturate(density);
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
