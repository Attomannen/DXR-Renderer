#pragma once
#include <cstdint>

namespace Ag
{
	// A borderless, per-pixel-alpha splash window shown while the engine loads.
	//
	// It deliberately owns nothing of the engine: no device, no asset paths, no
	// ImGui. The PNG comes out of the executable's own resources and is drawn
	// with GDI, so Show() can be the very first thing main() does -- which is the
	// point, because the two-and-a-half seconds it is covering are spent creating
	// the D3D12 device and loading the scene, long before anything could render.
	//
	// The fades run on a worker thread. The loading thread is busy and often
	// blocked on the GPU, so driving the animation from the main message pump
	// would produce a fade that stutters or stops entirely.
	class SplashScreen
	{
	public:
		// Creates the window and fades it in. Safe to call when the resource is
		// missing -- it simply does nothing.
		static void Show();

		// Fades out and destroys the window. Safe to call if Show() failed or was
		// never called, and safe to call more than once.
		static void Hide();

		// True between a successful Show() and a Hide().
		static bool IsVisible();
	};
}
