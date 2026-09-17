#ifndef EXPOSURE_HLSLI
#define EXPOSURE_HLSLI

// One scene unit of luminance/illuminance in cd/m² / lux. Must match
// Tga::Photometry::kNitsPerUnit: a 100 000 lux sun is pi scene units.
static const float kNitsPerUnit = 100000.0f / 3.14159265f;
static const float kMeterCalibration = 12.5f;

// Scene-unit multiplier for a camera at this EV100 (before compensation).
float ExposureScaleFromEv100(float ev100)
{
	return kNitsPerUnit / (1.2f * exp2(ev100));
}

// Pre-exposure: the DXR renderer writes lighting already multiplied by the
// previous frame's exposure so moonlight and sunlight both land in FP16's
// precise range. Everything that reads that HDR divides it back out.
// A sane fallback keeps a missing/garbage history from producing INF.
float PreExposureFromEv100(float previousEv100)
{
	const float ev = (previousEv100 == previousEv100) ? clamp(previousEv100, -16.0f, 24.0f) : 15.0f;
	return ExposureScaleFromEv100(ev);
}

#endif
