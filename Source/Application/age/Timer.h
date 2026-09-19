#pragma once
#include <chrono>

class Timer
{
	double myTimeStart;
	double myLastTimestamp;
	float myCurrentDelta;
	
public:
	Timer();
	Timer(const Timer& aTimer) = delete;
	Timer& operator=(const Timer& aTimer) = delete;

	void Update();

	float GetDeltaTime() const;
	double GetTotalTime() const;
};

