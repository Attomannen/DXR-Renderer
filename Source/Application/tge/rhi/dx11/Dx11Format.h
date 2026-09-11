#pragma once
#include <dxgiformat.h>
#include "tge/rhi/Descs.h"

// rhi::Format <-> DXGI_FORMAT. Backend-private.
namespace Tga::rhi::dx11
{
	DXGI_FORMAT   ToDxgi(Format f);
	Format        FromDxgi(DXGI_FORMAT f);
	// Depth resource formats need a typeless texture + typed DSV/SRV.
	DXGI_FORMAT   ToTypeless(Format f);       // for the ID3D11Texture2D
	DXGI_FORMAT   ToDsvFormat(Format f);
	DXGI_FORMAT   ToDepthSrvFormat(Format f); // e.g. D32_FLOAT -> R32_FLOAT
	bool          IsDepth(Format f);
	uint32_t      BitsPerPixel(Format f);
}
