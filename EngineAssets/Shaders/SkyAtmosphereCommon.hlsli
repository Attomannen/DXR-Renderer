#ifndef SKY_ATMOSPHERE_COMMON_HLSLI
#define SKY_ATMOSPHERE_COMMON_HLSLI
#include "SkyAtmosphereConstants.hlsli"

// Shared math for the sky-atmosphere LUT passes (transmittance, multi-
// scatter, sky-view) and the base-cubemap render. Single/multiple Rayleigh +
// Mie + ozone scattering over a spherical planet, following Bruneton &
// Neyret 2008 and Hillaire 2020 ("A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique", the method Unreal Engine ships). Lengths
// are meters; gBottomRadius/gTopRadius already carry Earth's real radius, so
// a local scene's own extent is a tiny perturbation near the planet surface
// -- exactly how Unreal's SkyAtmosphere component treats a non-planetary
// level too.

static const float kSkyPi = 3.14159265f;

// Nearest positive intersection of a ray with a sphere of radius `r`
// centered at the origin. Returns -1 if there is none ahead of the origin.
float RaySphereIntersectNearest(float3 origin, float3 dir, float r)
{
	float b = dot(origin, dir);
	float c = dot(origin, origin) - r * r;
	float disc = b * b - c;
	if (disc < 0.0f) return -1.0f;
	float sq = sqrt(disc);
	float t0 = -b - sq, t1 = -b + sq;
	if (t0 >= 0.0f) return t0;
	if (t1 >= 0.0f) return t1;
	return -1.0f;
}

bool HitsGround(float3 origin, float3 dir)
{
	return RaySphereIntersectNearest(origin, dir, gBottomRadius) >= 0.0f;
}

// Distance to the atmosphere's outer boundary along `dir`, or to the ground
// if `dir` points into it first -- the ray march's far bound either way.
float AtmosphereMarchDistance(float3 origin, float dir_y_unused, float3 dir)
{
	float toGround = RaySphereIntersectNearest(origin, dir, gBottomRadius);
	float toTop = RaySphereIntersectNearest(origin, dir, gTopRadius);
	if (toGround >= 0.0f) return toGround;
	return max(toTop, 0.0f);
}

// Rayleigh + Mie exponential density and the ozone tent layer at a height
// (meters above sea level). Returns per-channel extinction; scattering-only
// coefficients are read directly off the constants where needed (ozone only
// absorbs -- it contributes to extinction but never to in-scattering).
void SampleAtmosphereMedium(float height, out float3 rayleighScatter, out float3 mieScatter, out float3 extinction)
{
	float rayleighDensity = exp(-max(height, 0.0f) / gRayleighDensityH);
	float mieDensity = exp(-max(height, 0.0f) / gMieDensityH);
	float ozoneDensity = saturate(1.0f - abs(height - gOzoneCenterAltitude) / gOzoneWidth);

	rayleighScatter = gRayleighScattering * rayleighDensity;
	mieScatter = gMieScattering * mieDensity;
	float3 mieExtinction = gMieExtinction * mieDensity;
	float3 ozoneExtinction = gOzoneAbsorption * ozoneDensity;
	extinction = rayleighScatter + mieExtinction + ozoneExtinction;
}

float RayleighPhase(float cosTheta)
{
	return 3.0f / (16.0f * kSkyPi) * (1.0f + cosTheta * cosTheta);
}

// Cornette-Shanks: the standard closed-form Mie phase approximation used by
// every public real-time sky implementation (plain Henyey-Greenstein is not
// normalised the same way and looks visibly wrong for Mie's strong forward lobe).
float MiePhase(float g, float cosTheta)
{
	float g2 = g * g;
	float num = 3.0f * (1.0f - g2) * (1.0f + cosTheta * cosTheta);
	float den = 8.0f * kSkyPi * (2.0f + g2) * pow(max(1e-4f, 1.0f + g2 - 2.0f * g * cosTheta), 1.5f);
	return num / den;
}

// ---- Transmittance LUT parameterization (Bruneton 2008 / 2017) ----------
// x: normalised distance to the top of the atmosphere along the view ray.
// y: normalised closest-approach radius (0 = grazing the ground, 1 = at the
// top of the atmosphere) -- the standard mapping that keeps the whole LUT
// valid (no wasted texels on rays that would go through the planet).
float2 LutTransmittanceParamsToUv(float viewHeight, float viewZenithCosAngle)
{
	float H = sqrt(max(0.0f, gTopRadius * gTopRadius - gBottomRadius * gBottomRadius));
	float rho = sqrt(max(0.0f, viewHeight * viewHeight - gBottomRadius * gBottomRadius));
	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0f) + gTopRadius * gTopRadius;
	float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));
	float dMin = gTopRadius - viewHeight;
	float dMax = rho + H;
	float xMu = (dMax > dMin) ? (d - dMin) / (dMax - dMin) : 0.0f;
	float xR = (H > 0.0f) ? rho / H : 0.0f;
	return float2(saturate(xMu), saturate(xR));
}

void UvToLutTransmittanceParams(float2 uv, out float viewHeight, out float viewZenithCosAngle)
{
	float H = sqrt(max(0.0f, gTopRadius * gTopRadius - gBottomRadius * gBottomRadius));
	float rho = H * uv.y;
	viewHeight = sqrt(rho * rho + gBottomRadius * gBottomRadius);
	float dMin = gTopRadius - viewHeight;
	float dMax = rho + H;
	float d = dMin + uv.x * (dMax - dMin);
	viewZenithCosAngle = (d < 1e-4f) ? 1.0f : clamp((H * H - rho * rho - d * d) / (2.0f * viewHeight * d), -1.0f, 1.0f);
}

float3 SampleTransmittanceLut(Texture2D<float4> lut, SamplerState samp, float viewHeight, float viewZenithCosAngle)
{
	float2 uv = LutTransmittanceParamsToUv(clamp(viewHeight, gBottomRadius, gTopRadius), viewZenithCosAngle);
	return lut.SampleLevel(samp, uv, 0).rgb;
}

// Transmittance from `origin` (radius = viewHeight) to the sun, folding in a
// ground-occlusion test the LUT itself can't encode (it only knows the
// closest-approach radius, not whether *this* ray started below the horizon
// on the planet's near side).
float3 SunTransmittance(Texture2D<float4> transmittanceLut, SamplerState samp, float3 origin, float3 sunDir)
{
	float viewHeight = length(origin);
	float cosSunZenith = dot(origin / max(viewHeight, 1.0f), sunDir);
	if (HitsGround(origin, sunDir)) return 0.0f;
	return SampleTransmittanceLut(transmittanceLut, samp, viewHeight, cosSunZenith);
}

// ---- Sky-view LUT parameterization ---------------------------------------
// Deliberately a plain linear (zenith angle, azimuth-from-sun) mapping
// rather than Hillaire's non-linear horizon-concentrating one: it spends a
// few more texels than strictly necessary on directions far from the
// horizon, but the LUT already feeds a 128px cubemap that's GGX-prefiltered
// immediately afterward, so that slight inefficiency is invisible in the
// final image while the mapping itself stays simple and unambiguous.
float2 SkyViewDirToUv(float3 dir)
{
	float zenithAngle = acos(clamp(dir.y, -1.0f, 1.0f));            // 0 = up, pi = down
	float azimuth = atan2(dir.z, dir.x);                            // -pi..pi
	return float2(azimuth / (2.0f * kSkyPi) + 0.5f, zenithAngle / kSkyPi);
}

float3 SkyViewUvToDir(float2 uv)
{
	float azimuth = (uv.x - 0.5f) * 2.0f * kSkyPi;
	float zenithAngle = uv.y * kSkyPi;
	float sinZenith = sin(zenithAngle);
	return float3(sinZenith * cos(azimuth), cos(zenithAngle), sinZenith * sin(azimuth));
}

#endif
