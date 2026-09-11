#pragma once
#include <dxgiformat.h>
#include "tge/rhi/Descs.h"

// rhi::Format <-> DXGI_FORMAT. Shared by both backends -- DXGI_FORMAT is a
// DXGI (not D3D11-specific) type, so this has no D3D11 dependency. Originally
// lived under rhi::dx11 as the DX11 backend's own helper; hoisted here so the
// DX12 backend can reuse it verbatim (Dx11Format.h now just aliases this).
namespace Tga::rhi
{
	DXGI_FORMAT   ToDxgi(Format f);
	Format        FromDxgi(DXGI_FORMAT f);
	// Depth resource formats need a typeless texture + typed DSV/SRV.
	DXGI_FORMAT   ToTypeless(Format f);       // for the texture resource itself
	DXGI_FORMAT   ToDsvFormat(Format f);
	DXGI_FORMAT   ToDepthSrvFormat(Format f); // e.g. D32_FLOAT -> R32_FLOAT
	bool          IsDepth(Format f);
	uint32_t      BitsPerPixel(Format f);
}
