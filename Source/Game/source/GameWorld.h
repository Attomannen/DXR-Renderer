#pragma once

#include <memory>

// Scene-driven render benchmark harness.
//
// Env vars (all optional):
//   BENCH_FRAMES        >0 : run a deterministic fly-through for N frames, write
//                            a report and quit.  0 (default): free-fly, no report.
//   BENCH_WARMUP        frames excluded from stats (default 60).
//   BENCH_SPONZA_COPIES grid of Sponza copies to raise mesh / draw-call load (default 1).
//   BENCH_LIGHTS        active point lights, capped at NUMBER_OF_LIGHTS_ALLOWED (default 8).
//   BENCH_REPORT        report path (default "bench_report.json", next to the exe).
//   BENCH_SCENE         scene path (without .tgs); when unset, the first .tgs
//                       found under the game asset root is loaded.

namespace Tga { class InputManager; }

class GameWorld
{
public:
	GameWorld();
	~GameWorld();

	void Init();
	void Update(float aDeltaTime);
	void Render();
	void DrawDebugUI();   // ImGui tuning panel (free-fly runs only)

	// Forwarded from the window proc so the free-fly camera can read input.
	void OnWinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam);

	static GameWorld* Get() { return ourInstance; }

private:
	struct Impl;
	std::unique_ptr<Impl> myImpl;
	static GameWorld* ourInstance;
};
