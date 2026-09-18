#ifndef STARFIELD_HLSLI
#define STARFIELD_HLSLI

// Procedural star field, evaluated per pixel in the atmosphere composite.
//
// Deliberately NOT baked into the sky cubemap alongside the existing night-sky
// texture. That cubemap is 512 per face, so a star -- which is a point source,
// far smaller than one texel at any sane resolution -- lands as a smear across
// several texels and then gets prefiltered again for the IBL. Evaluating the
// field per pixel at display resolution is the only way a star comes out the
// size a star should be, and it costs one hash for sky pixels only.
//
// Composited before the clouds so a cloud occludes the stars behind it, and
// before fog so the horizon haze washes them out the way it really does.

float3 StarHash33(float3 p)
{
	p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)),
	           dot(p, float3(269.5f, 183.3f, 246.1f)),
	           dot(p, float3(113.5f, 271.9f, 124.6f)));
	return frac(sin(p) * 43758.5453123f);
}

// Blackbody-ish stellar colour. Real stars run from cool red dwarfs through
// yellow to hot blue-white; the population is heavily weighted toward the cool
// end, which is why a real sky is not a field of neutral white dots. t in [0,1]
// is the draw, biased so most stars land warm.
float3 StarColor(float t)
{
	const float3 cool = float3(1.00f, 0.72f, 0.48f);   // ~3500 K
	const float3 mid  = float3(1.00f, 0.96f, 0.92f);   // ~6000 K
	const float3 hot  = float3(0.75f, 0.83f, 1.00f);   // ~12000 K
	return t < 0.5f ? lerp(cool, mid, t * 2.0f) : lerp(mid, hot, (t - 0.5f) * 2.0f);
}

// dir:        world view direction, normalised.
// sunDir:     world direction TO the sun. Only used to build the celestial
//             frame, so the field turns rigidly with the sun rather than being
//             pinned to world axes -- stars have to wheel overhead as the night
//             passes or the cycle reads as a static backdrop with a moving sun.
// density:    cells per unit across a cube face. Higher is more, smaller stars.
// twinkleTime:   seconds; 0 disables scintillation.
// twinkleAmount: 0..1 scale on the scintillation depth.
float3 StarField(float3 dir, float3 sunDir, float density, float twinkleTime, float twinkleAmount)
{
	// Rigid rotation shared with the sun. Any orthonormal basis built from
	// sunDir will do, as long as it is continuous as the sun moves.
	const float3 cf = sunDir;
	const float3 cr = normalize(abs(cf.y) < 0.99f ? cross(cf, float3(0, 1, 0)) : cross(cf, float3(1, 0, 0)));
	const float3 cu = cross(cf, cr);
	const float3 d = float3(dot(dir, cr), dot(dir, cu), dot(dir, cf));

	// Cube-face parameterisation: a direction maps to one face and a square uv
	// on it, which gives an even cell grid over the sphere. A latitude/longitude
	// grid would crowd every cell into the poles and put a visible knot of stars
	// directly overhead.
	const float3 ad = abs(d);
	float2 uv; float face;
	if (ad.x >= ad.y && ad.x >= ad.z)      { uv = d.yz / ad.x; face = d.x > 0 ? 0.0f : 1.0f; }
	else if (ad.y >= ad.z)                 { uv = d.xz / ad.y; face = d.y > 0 ? 2.0f : 3.0f; }
	else                                   { uv = d.xy / ad.z; face = d.z > 0 ? 4.0f : 5.0f; }

	const float2 grid = uv * density;
	const float2 cell = floor(grid);
	const float2 local = grid - cell;

	// One star per cell, tested against the containing cell only -- not the
	// 3x3 neighbourhood. That is affordable because the jitter below keeps a
	// star well inside its own cell, so it can never be clipped by the edge,
	// and a star's radius is a small fraction of a cell. The grid never shows
	// because the jitter is large next to the star.
	const float3 h = StarHash33(float3(cell, face));

	// Magnitude: a steep power law. Real star counts rise sharply toward the
	// faint end, and a uniform draw gives a sky of identical mid-bright dots
	// that reads as noise rather than as stars.
	// Most cells hold nothing at all. Without this every cell contributes a
	// dot, and an evenly populated grid of dots reads as television static
	// however steeply their brightness is weighted -- what makes a real sky
	// look like a sky is the empty space between the stars, not the stars.
	if (h.z < 0.55f) return 0.0f;
	const float brightness = pow((h.z - 0.55f) / 0.45f, 3.0f);
	if (brightness < 0.0015f) return 0.0f;

	const float2 starPos = 0.5f + (h.xy - 0.5f) * 0.7f;
	const float dist = length(local - starPos);

	// Angular size floor: a true point source aliases into a flickering speck
	// under any temporal resolve. Giving every star a small finite radius lets
	// the resolve integrate it instead, at the cost of the very faintest ones
	// being slightly larger than they should be.
	const float radius = 0.055f + brightness * 0.10f;
	float star = saturate(1.0f - dist / radius);
	star = star * star * (3.0f - 2.0f * star);   // smooth falloff, no hard rim

	// Scintillation. Atmospheric, so it is strongest near the horizon where the
	// path is longest; kept slow and shallow because a fast one fights the
	// temporal resolve and reads as noise rather than as twinkle.
	float twinkle = 1.0f;
	if (twinkleTime > 0.0f && twinkleAmount > 0.0f)
	{
		const float phase = twinkleTime * (0.7f + h.x) + h.y * 6.2831853f;
		const float horizonWeight = saturate(1.0f - abs(dir.y) * 2.5f);
		twinkle = 1.0f + sin(phase) * 0.28f * horizonWeight * twinkleAmount;
	}

	return StarColor(h.x) * (brightness * star * twinkle);
}

#endif
