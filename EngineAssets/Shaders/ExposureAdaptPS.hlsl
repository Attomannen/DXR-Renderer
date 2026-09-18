// Temporal exposure adaptation. Renders a 1x1 target.
//   t0 = current frame's 1x1 average log2 luminance (scene units)
//   t1 = previous frame's adapted EV100
// Output: adapted EV100. Adapting in EV space makes brightening and darkening
// take the same perceived time.
#include "PostFxCommon.hlsli"

Texture2D CurLogLuma : register(t0);
Texture2D PrevEv100 : register(t1);

float main(FsIn i) : SV_TARGET
{
	// Manual camera: no metering, no adaptation. Still written here so the
	// history always holds the EV the next frame is pre-exposed with.
	if (gExposureAuto < 0.5f)
		return gManualEv100;

	const float avgLogLuma = CurLogLuma.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r;
	const float metered = clamp(MeteredEv100(exp2(avgLogLuma)), gAutoEvMin, gAutoEvMax);

	// PARTIAL adaptation, anchored on the manual camera's EV.
	//
	// A meter that compensates fully makes every scene the same brightness by
	// definition, which is exactly wrong for a day-night cycle: with the sun 30
	// degrees below the horizon the camera opened all the way to the EV floor
	// and midnight came out brighter on screen than noon. Neither a real camera
	// nor an eye does this -- both have a limited range and a night still looks
	// like night.
	//
	// Blending the metered value toward a fixed reference keeps the camera
	// responsive without letting it cancel the cycle out. At 0.65 a 21-stop
	// Measured across the cycle, night mean over day mean: 2.02 at a strength
	// of 1, 1.11 at 0.45, 0.84 at 0.25. Anything near 1 means the meter has
	// cancelled the cycle out; 0.25 leaves night visibly darker than day while
	// still opening up enough to keep it readable. 1 is a fully compensating
	// meter, 0 is a fixed camera.
	const float target = lerp(gManualEv100, metered, saturate(gAdaptStrength));

	float prev = PrevEv100.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r;
	if (!(prev == prev)) prev = target;

	// Exponential adaptation, framerate independent.
	const float t = saturate(1.0f - exp(-gAdaptRate * gDeltaTime));
	return lerp(prev, target, t);
}
