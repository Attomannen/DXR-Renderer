#include "stdafx.h"

#include "Timer.h"

#include <chrono>

Timer::Timer()
{
	using namespace std::chrono;

	double startMs = static_cast<double>(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
	myTimeStart = startMs;
	myLastTimestamp = startMs;
}

void Timer::Update()
{
	using namespace std::chrono;
	double newTimestamp = static_cast<double>(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());

	myCurrentDelta = static_cast<float>((newTimestamp - myLastTimestamp) / 1000);
	myLastTimestamp = newTimestamp;
}

float Timer::GetDeltaTime() const
{
	return myCurrentDelta;
}

double Timer::GetTotalTime() const
{
	return static_cast<double>((myLastTimestamp - myTimeStart) / 1000);
}
