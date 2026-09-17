#pragma once

#include <algorithm>
#include <vector>

// Decides which irradiance-volume probes are refreshed each frame.
//
// A fresh volume is "primed": probes are traced in order with no history blend,
// a large batch per frame, until the cursor wraps. After that the scheduler
// trickles through the volume round-robin and blends with hysteresis. A
// lighting refresh (sun or sky edit) runs one sweep with low hysteresis so the
// new light converges quickly without clearing the cache.
class GiProbeScheduler
{
public:
	struct Settings
	{
		bool  rayTraced = true;    // raster capture is six scene draws per probe: one probe per frame
		int   primeBatch = 8;      // probes per frame while priming
		int   trickleBatch = 4;    // minimum RT probes per frame after priming
		int   probeBudget = 0;     // RT probes per frame; 0 = auto from ray count
		int   raysPerProbe = 256;
		float hysteresis = 0.94f;
	};

	struct Batch
	{
		std::vector<int> probes;   // flat probe indices, x + y*cx + z*cx*cy
		float hysteresis = 0.f;    // blend weight of the old value for every probe in the batch
	};

	// Starts a new prime; the caller clears the GPU volume.
	void Restart() { myCursor = 0; myPriming = true; myRefreshBudget = 0; }

	// One low-hysteresis sweep over the volume, keeping existing light.
	void RequestLightingRefresh(int aProbeCount) { myRefreshBudget = aProbeCount; }

	bool IsPriming() const { return myPriming; }
	int  Cursor() const { return myCursor; }

	Batch Next(int aProbeCount, const Settings& aSettings)
	{
		Batch batch;
		if (aProbeCount <= 0) return batch;

		const bool priming = myPriming;
		const bool refreshing = !priming && myRefreshBudget > 0;
		batch.hysteresis = priming ? 0.f : refreshing ? std::min(aSettings.hysteresis, 0.2f) : aSettings.hysteresis;

		// Keep roughly 32k rays per frame when a low ray count is selected, so
		// each probe gets fresh samples quickly.
		const int autoBatch = std::clamp(32768 / std::max(aSettings.raysPerProbe, 1), 1, 256);
		const int rtBatch = aSettings.probeBudget > 0 ? std::min(aSettings.probeBudget, 256) : autoBatch;
		const int count = priming
			? (aSettings.rayTraced ? std::max(aSettings.primeBatch, rtBatch) : aSettings.primeBatch)
			: (aSettings.rayTraced ? std::max(aSettings.trickleBatch, rtBatch) : 1);

		batch.probes.reserve((size_t)count);
		for (int n = 0; n < count; ++n)
		{
			batch.probes.push_back(myCursor);
			myCursor = (myCursor + 1) % aProbeCount;
			if (myCursor == 0) myPriming = false;
			if (refreshing && myRefreshBudget > 0) --myRefreshBudget;
		}
		return batch;
	}

private:
	int  myCursor = 0;
	bool myPriming = true;
	int  myRefreshBudget = 0;
};
