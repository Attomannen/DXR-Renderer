#pragma once

namespace Tga
{
	enum class RhiBackendChoice { Legacy, Dx12, Cancelled };

	// Shows a small native Win32 window with two buttons, "Legacy" and "DX12",
	// and blocks until the user picks one or closes the window. This has to be
	// plain Win32 (no ImGui) since it runs before any graphics device exists --
	// picking the device IS the point. On a pick, sets the TGE_RHI env var
	// (via _putenv_s) so DX11::Init()'s existing TGE_RHI=dx12 selection check
	// picks it up completely unchanged -- this is a friendlier front end for
	// that switch, not a new selection mechanism, and every existing
	// TGE_RHI=dx12/dx11-driven script (bench runs, CI, etc.) is unaffected
	// since callers only invoke this when TGE_RHI isn't already set.
	RhiBackendChoice ShowBackendChooser(const wchar_t* aWindowTitle);
}
