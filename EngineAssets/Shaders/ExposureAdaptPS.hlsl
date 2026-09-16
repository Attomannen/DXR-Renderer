// Temporal exposure adaptation. Renders a 1x1 target.
//   t0 = current frame's 1x1 average log-luma
//   t1 = previous frame's adapted exposure (1x1)
// Output: adapted linear exposure multiplier.
#include "PostFxCommon.hlsli"

Texture2D CurLogLuma : register(t0);
Texture2D PrevExposure : register(t1);

float main(FsIn i) : SV_TARGET
{
	float avgLogLuma = CurLogLuma.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r;
	float avgLuma = exp2(avgLogLuma);

	// EV100 Physical Exposure Formula
	// gExposureKeyOrManual is either the auto-exposure Key value, or manual EV100.
	float exposureMultiplier;
	
	if (gExposureAuto > 0.5f) {
	    // Auto exposure: target exposure so that the scene's average luma maps to the key value.
	    float targetExposure = gExposureKeyOrManual / max(avgLuma, 1e-4f);
	    targetExposure = clamp(targetExposure, gExposureMin, gExposureMax);
	    exposureMultiplier = targetExposure;
	} else {
	    // Manual exposure: gExposureKeyOrManual holds an EV100 value here, not a
	    // plain multiplier. Confirmed against the known-good reference build by
	    // disassembling its compiled ExposureAdaptPS.cso (fxc /dumpbin): that
	    // binary computes exactly this L_max = 1.2 * exp2(EV100) formula, so a
	    // prior "fix" that replaced it with a direct linear multiplier was a
	    // regression against the validated-correct behaviour, not a bug fix.
	    float L_max = 1.2f * exp2(gExposureKeyOrManual);
	    exposureMultiplier = 1.0f / max(L_max, 1e-4f);
	}

	float prev = PrevExposure.SampleLevel(LinearClamp, float2(0.5f, 0.5f), 0).r;
	if (prev <= 0.0f) prev = exposureMultiplier;

	// Exponential adaptation, framerate independent.
	float t = saturate(1.0f - exp(-gAdaptRate * gDeltaTime));
	return lerp(prev, exposureMultiplier, t);
}
