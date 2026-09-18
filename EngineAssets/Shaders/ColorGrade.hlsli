#ifndef COLORGRADE_HLSLI
#define COLORGRADE_HLSLI

// Primary colour grading, applied to exposed linear HDR just before the
// tonemapper.
//
// Before, not after. Grading a display-referred image means every operation
// fights the tone curve: contrast pushes values that are already compressed,
// and saturation acts on hues the curve has already bent. Working on the
// scene-referred values and letting the tonemapper have the last word is how a
// real grade is built, and it is why highlights stay in the shoulder instead of
// clipping the moment someone raises contrast.
//
// Order matters and is the usual one: white balance, then contrast about a
// mid-grey pivot, then the lift/gamma/gain wheels, then saturation. White
// balance first because it is a property of the capture, not a look; saturation
// last because it should judge the colours the grade actually produced.

static const float3 kGradeLuma = float3(0.2126f, 0.7152f, 0.0722f);

// Von Kries-style white balance in LMS. Doing this in RGB by scaling channels
// shifts hue as well as temperature, which is why a naive "warm it up" slider
// turns skies magenta rather than golden.
float3 WhiteBalance(float3 c, float temperature, float tint)
{
	// Planckian-ish offsets, normalised so 0 is a no-op rather than tied to an
	// absolute Kelvin target -- the sun's own colour is already in the scene.
	const float t = temperature * 0.05f;
	const float g = tint * 0.05f;

	static const float3x3 kRgbToLms = float3x3(
		0.390405f, 0.549941f, 0.008327f,
		0.070841f, 0.963172f, 0.001345f,
		0.023100f, 0.128021f, 0.936245f);
	static const float3x3 kLmsToRgb = float3x3(
		 2.858479f, -1.628790f, -0.024891f,
		-0.210182f,  1.158372f,  0.000324f,
		-0.041800f, -0.118169f,  1.068700f);

	float3 lms = mul(kRgbToLms, c);
	// Temperature trades long against short wavelengths; tint trades the medium
	// channel against both, which is the green/magenta axis.
	lms *= float3(1.0f + t, 1.0f + g * 0.5f, 1.0f - t);
	return mul(kLmsToRgb, lms);
}

// Contrast about a mid-grey pivot, in log space.
//
// In linear space, contrast pivots around a value that depends on exposure and
// pushes highlights away far faster than it pulls shadows, so a small amount
// blows the image out. Log space is perceptually even: the same slider moves
// shadows and highlights by the same number of stops.
float3 Contrast(float3 c, float contrast)
{
	const float kPivot = 0.18f;   // mid grey
	const float3 logC = log2(max(c, 1e-5f));
	const float logPivot = log2(kPivot);
	return exp2((logC - logPivot) * contrast + logPivot);
}

// Lift / gamma / gain: the three colour wheels.
//
// Lift moves the shadows without touching white, gain scales the highlights
// without lifting black, and gamma bends everything between them. Together they
// are the classic primary grade, and they are separable enough that an artist
// can reason about each one.
float3 LiftGammaGain(float3 c, float3 lift, float3 gamma, float3 gain)
{
	// Lift is added in a way that leaves 1.0 fixed, so raising shadows does not
	// also wash out the whites.
	c = c * (1.0f + lift) - lift * 0.5f;
	c = max(c, 0.0f);
	c = pow(c, max(gamma, 1e-3f));
	return c * gain;
}

float3 Saturation(float3 c, float saturation)
{
	const float luma = dot(c, kGradeLuma);
	// Not clamped at 1: values above it push colour past the source, which is a
	// legitimate look, and the tonemapper will handle what leaves the gamut.
	return max(luma + (c - luma) * saturation, 0.0f);
}

float3 ApplyColorGrade(float3 c)
{
	if (gGradeEnabled < 0.5f) return c;
	c = WhiteBalance(c, gGradeTemperature, gGradeTint);
	c = Contrast(c, gGradeContrast);
	c = LiftGammaGain(c, gGradeLift.rgb, gGradeGamma.rgb, gGradeGain.rgb);
	c = Saturation(c, gGradeSaturation);
	return c;
}

#endif
