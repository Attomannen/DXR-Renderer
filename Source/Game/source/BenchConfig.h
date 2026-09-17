#pragma once

// Environment overrides for the bench harness. Every BENCH_* variable the game
// reads is parsed here, so a run's configuration can be read top to bottom.
// The full list with meanings is in Source/Game/BENCH.md. (Go.cpp still reads
// BENCH_FRAMES / BENCH_NOVSYNC itself, before the engine starts.)

#include "GameWorld.h"
#include <tge/render/DeferredRenderer.h>
#include <optional>
#include <string>

namespace BenchConfig
{
	// One-shot run settings that belong to a single code path. Read once at
	// startup and kept on GameWorld::Impl::bench.
	struct Run
	{
		// Capture
		int  shotCount = 1;             // BENCH_SHOT_COUNT: consecutive frames name_0, name_1, ...
		int  freezeFrame = 0;           // BENCH_FREEZE_FRAME: stop the scripted camera at this frame
		int  taaResetFrame = -1;        // BENCH_TAA_RESET_FRAME
		bool costSweep = false;         // BENCH_COST_SWEEP: run the feature cost sweep after warm-up
		bool noCullFace = false;        // BENCH_NOCULLFACE

		// Debug UI in bench runs
		bool debugUi = false;           // BENCH_DEBUG_UI
		std::string debugTab;           // BENCH_DEBUG_TAB: tab to open

		// Startup content
		std::string cubemap;            // BENCH_CUBEMAP
		std::string transparentMats;    // BENCH_TRANSPARENT_MATS, comma separated

		// Scripted camera (scene load with aEnv = true)
		std::optional<std::string> camMode;   // BENCH_CAM: fixed | spin | orbit | room
		std::optional<std::string> camFile;   // BENCH_CAMFILE
		std::optional<float> spinDeg;         // BENCH_SPIN
		std::optional<float> orbitRadius;     // BENCH_ORBIT
		std::optional<float> exposure;        // BENCH_EXPOSURE
		std::optional<float> modelRotX;       // BENCH_ROT_X
	};

	Run ReadRun();

	// Run length, report and scene selection. Call first in Init().
	void ApplyStartupOverrides(GameWorld::Impl& s);

	// Debug spheres, reflection probe and GI volume. Call after the scene and
	// any per-scene probe file are loaded.
	void ApplyContentOverrides(GameWorld::Impl& s);

	// Renderer mode, lighting and feature toggles on the GameWorld state.
	void ApplyWorldOverrides(GameWorld::Impl& s);

	// Renderer tunables. Call once the deferred renderer is ready; the caller
	// recreates the DXR targets afterwards if dlssMode changed.
	void ApplyRendererOverrides(Tga::DeferredRenderer::Tunables& tun);
}
