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
	const float target = clamp(MeteredEv100(exp2(avgLogLuma)), gAutoEvMin, gAutoEvMax);

	float prev = PrevEv100.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r;
	if (!(prev == prev)) prev = target;

	// Exponential adaptation, framerate independent.
	const float t = saturate(1.0f - exp(-gAdaptRate * gDeltaTime));
	return lerp(prev, target, t);
}
