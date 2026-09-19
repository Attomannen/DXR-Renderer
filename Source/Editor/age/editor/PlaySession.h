#pragma once

#include <age/math/Vector.h>

namespace Ag
{
	class RenderTarget;
	class DepthBuffer;

	// Playing the level inside the editor viewport, rather than launching the
	// game as a separate process.
	//
	// The editor cannot call into the game directly: Editor and
	// EditorDefaultGraphics link Core/Graphics/Application, not Game, and
	// reversing that would make the editor depend on a specific game. So the
	// editor owns the button and the viewport, and the executable that links
	// both (GameEditor) installs the four functions that actually drive a
	// GameWorld. With nothing installed the hooks are null and Play falls back
	// to launching the separate process, which is what a bare Editor build does.
	struct PlaySessionHooks
	{
		// Load the level and start ticking it. Returns false if it could not
		// start, in which case the caller should fall back to the old launch.
		// aWidth/aHeight: the viewport the session will render into. Passed at
		// start, not first tick, because loading the level sizes the camera and
		// the shared deferred renderer.
		bool (*start)(const char* aLevelPath, unsigned int aWidth, unsigned int aHeight) = nullptr;
		// Advance and render one frame into the given viewport target.
		// aInputActive: the viewport has the mouse, so the game may steer its
		// camera. False while the cursor is over the rest of the editor.
		// aOriginX/aOriginY: the panel's top-left in window client coordinates,
		// so the session can confine the cursor to it while looking around.
		void (*tick)(float aDeltaSeconds, RenderTarget* aColor, DepthBuffer* aDepth,
		             int aOriginX, int aOriginY,
		             unsigned int aWidth, unsigned int aHeight, bool aInputActive) = nullptr;
		// Tear the session down. The caller reloads the level afterwards.
		void (*stop)() = nullptr;
		// Forward a window message while a session is running.
		void (*winProc)(unsigned int aMessage, unsigned long long aWParam, long long aLParam) = nullptr;
	};

	class PlaySession
	{
	public:
		static void Install(const PlaySessionHooks& aHooks);
		static bool Available();

		// True between a successful Start and a Stop.
		static bool IsPlaying();

		static bool Start(const char* aLevelPath, unsigned int aWidth, unsigned int aHeight);
		static void Tick(float aDeltaSeconds, RenderTarget* aColor, DepthBuffer* aDepth,
		                 int aOriginX, int aOriginY,
		                 unsigned int aWidth, unsigned int aHeight, bool aInputActive);
		static void Stop();
		static void WinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam);
	};
}
