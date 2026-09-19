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

namespace Ag { class InputManager; class RenderTarget; class DepthBuffer; }

class GameWorld
{
public:
	GameWorld();
	~GameWorld();

	void Init();
	void Update(float aDeltaTime);
	void Render();
	void DrawDebugUI();   // ImGui tuning panel (free-fly runs only)

	// Render into an editor viewport instead of the application backbuffer.
	//
	// The DeferredRenderer composites into the DX11::BackBuffer/DepthBuffer
	// globals, and the editor already swaps those to point at its viewport
	// target (DefaultEditorGraphics does exactly this for its own scene view).
	// Setting a target here makes Render do the same swap, and makes the camera
	// take its aspect ratio from the viewport rather than from the window --
	// which is otherwise the wrong shape entirely when the game is embedded.
	//
	// Null clears it and returns to the window, which is what the standalone
	// game always uses.
	void SetEmbeddedTarget(Ag::RenderTarget* aColor, Ag::DepthBuffer* aDepth,
	                       int aOriginX, int aOriginY,
	                       unsigned int aWidth, unsigned int aHeight);
	// Embedded only: whether the host viewport currently owns the mouse.
	void SetEmbeddedInput(bool aActive);

	// The host and an embedded session share one DeferredRenderer, and loading
	// a level stamps the game's tunables over the host's. Snapshot around the
	// session so stopping restores the host's view without reloading.
	static void SaveSharedRendererState();
	static void RestoreSharedRendererState();

	// Forwarded from the window proc so the free-fly camera can read input.
	void OnWinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam);

	static GameWorld* Get() { return ourInstance; }

	// Defined in GameWorldImpl.h; only the GameWorld*.cpp files and BenchConfig see it.
	struct Impl;

private:
	std::unique_ptr<Impl> myImpl;
	static GameWorld* ourInstance;
};
