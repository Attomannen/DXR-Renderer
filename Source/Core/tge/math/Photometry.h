#pragma once

#include <algorithm>
#include <cmath>
#include <tge/math/Vector3.h>

// Physical light and camera units.
//
// Shaders work in "scene units" so that the sunlit HDR image fits comfortably
// in FP16. One scene unit of luminance is kNitsPerUnit cd/m², and illuminance
// uses the same scale (L = albedo * E / pi holds in both). The factor is
// chosen so a 100 000 lux clear-sky sun is exactly pi scene units -- the value
// every existing threshold, clamp and GI tuning in the renderer was authored
// against. Keep in sync with PostFxCommon.hlsli.
namespace Tga::Photometry
{
	inline constexpr float kPi = 3.14159265f;
	inline constexpr float kNitsPerUnit = 100000.0f / kPi;

	// Reflected-light meter calibration (ISO 2720, as used by Frostbite/Filament).
	inline constexpr float kMeterCalibration = 12.5f;

	inline float LuxToUnits(float aLux) { return aLux / kNitsPerUnit; }
	inline float NitsToUnits(float aNits) { return aNits / kNitsPerUnit; }
	inline float CandelaToUnits(float aCandela) { return aCandela / kNitsPerUnit; }

	// Luminous intensity of an isotropic point light.
	inline float PointLumensToCandela(float aLumens) { return aLumens / (4.0f * kPi); }

	// Luminous intensity of a spot light whose flux is spread over its cone,
	// so narrowing the cone concentrates the same flux (like a real reflector).
	inline float SpotLumensToCandela(float aLumens, float aOuterHalfAngleRad)
	{
		const float solidAngle = 2.0f * kPi * (1.0f - std::cos(std::clamp(aOuterHalfAngleRad, 0.01f, kPi)));
		return aLumens / solidAngle;
	}

	// EV at ISO 100 for aperture N (f-stop), shutter t (seconds) and ISO S.
	inline float Ev100FromCamera(float aAperture, float aShutterSeconds, float aIso)
	{
		const float n = std::max(aAperture, 0.5f);
		const float t = std::max(aShutterSeconds, 1e-6f);
		const float s = std::max(aIso, 1.0f);
		return std::log2((n * n) / t * 100.0f / s);
	}

	// Scene-unit multiplier that maps luminance to the sensor's linear signal
	// (1.0 = saturation), after an EV compensation in stops.
	inline float ExposureFromEv100(float aEv100, float aCompensation = 0.0f)
	{
		return kNitsPerUnit / (1.2f * std::pow(2.0f, aEv100 - aCompensation));
	}

	// Approximate chromaticity of a black body (Kang et al. 2002, 1667-25000 K)
	// converted to linear sRGB and normalised to unit luminance, so it can be
	// multiplied by an illuminance without changing its brightness.
	inline Vector3f BlackbodyToLinearSrgb(float aKelvin)
	{
		const float t = std::clamp(aKelvin, 1667.0f, 25000.0f);
		const float t2 = t * t, t3 = t2 * t;
		const float x = t <= 4000.0f
			? -0.2661239e9f / t3 - 0.2343589e6f / t2 + 0.8776956e3f / t + 0.179910f
			: -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;
		const float x2 = x * x, x3 = x2 * x;
		const float y = t <= 2222.0f
			? -1.1063814f * x3 - 1.34811020f * x2 + 2.18555832f * x - 0.20219683f
			: t <= 4000.0f
			? -0.9549476f * x3 - 1.37418593f * x2 + 2.09137015f * x - 0.16748867f
			:  3.0817580f * x3 - 5.87338670f * x2 + 3.75112997f * x - 0.37001483f;

		// xyY (Y = 1) -> XYZ -> linear sRGB (D65).
		const float X = x / y, Y = 1.0f, Z = (1.0f - x - y) / y;
		Vector3f rgb{
			 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z,
			-0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z,
			 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z };
		rgb.x = std::max(rgb.x, 0.0f); rgb.y = std::max(rgb.y, 0.0f); rgb.z = std::max(rgb.z, 0.0f);
		const float luminance = 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
		return luminance > 1e-6f ? rgb / luminance : Vector3f{ 1.0f, 1.0f, 1.0f };
	}
}
