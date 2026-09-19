#pragma once

#include <cmath>

// One set of numbers and one feel for the editor viewport's free-flight camera
// and for a running game's, so a level handles the same before and after
// pressing Play. Modelled on Unreal's viewport flight: hold the right mouse
// button to look, WASD to move relative to the view, Q/E for down/up, the wheel
// to change speed while looking, Shift to boost -- and movement that eases in
// and out instead of snapping between full speed and a dead stop.
//
// Lives in Core, beside Camera.h, because Editor and Game both link it and
// neither links the other -- Editor does not have Source/Graphics on its
// include path. The two camera implementations stay separate; only the feel is
// shared.
namespace Ag::FlyCamera
{
	// Metres per second, before the boost.
	inline constexpr float kMinSpeed     = 0.5f;
	inline constexpr float kMaxSpeed     = 200.f;
	inline constexpr float kDefaultSpeed = 6.f;

	// Wheel steps are multiplicative. A fixed increment cannot serve both a
	// room and a city block: it is either unusably coarse at one end or takes
	// dozens of clicks at the other.
	inline constexpr float kWheelStepUp   = 1.25f;
	inline constexpr float kWheelStepDown = 0.8f;

	inline constexpr float kBoost = 3.f;   // while Shift is held

	// Seconds to reach ~63% of the target velocity. Short enough to still feel
	// direct, long enough to take the twitch out of starting and stopping.
	inline constexpr float kResponse = 0.06f;

	// Written without std::min/std::max on purpose: <windows.h> defines min and
	// max as macros in some of the translation units that include this, and
	// NOMINMAX is not set everywhere.
	inline float StepSpeed(float aSpeed, float aWheel)
	{
		if (aWheel > 0.f)
		{
			const float stepped = aSpeed * kWheelStepUp;
			return stepped > kMaxSpeed ? kMaxSpeed : stepped;
		}
		if (aWheel < 0.f)
		{
			const float stepped = aSpeed * kWheelStepDown;
			return stepped < kMinSpeed ? kMinSpeed : stepped;
		}
		return aSpeed;
	}

	// Exponential smoothing toward aTarget, independent of frame rate -- a
	// plain lerp by a constant factor accelerates faster the better the
	// machine, which is exactly the kind of difference that makes a camera
	// feel wrong on someone else's computer.
	template <class Vector>
	inline Vector Smooth(const Vector& aCurrent, const Vector& aTarget, float aDeltaSeconds)
	{
		const float dt = aDeltaSeconds > 0.f ? aDeltaSeconds : 0.f;
		const float t = 1.f - std::exp(-dt / kResponse);
		return aCurrent + (aTarget - aCurrent) * t;
	}
}
