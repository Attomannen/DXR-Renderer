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
	// A format that is itself typeless (R32_Typeless, R8G8B8A8_Typeless -- as
	// opposed to a depth format, which is typed but backed by a typeless
	// resource under the hood). Used where a resource's own desc.format may
	// be typeless by request (e.g. RenderTarget's TYPELESS+sRGB-RTV+linear-SRV
	// case) and the caller can't assume desc.format is itself a valid,
	// directly-clearable D3D12_CLEAR_VALUE format.
	bool          IsTypeless(Format f);
	uint32_t      BitsPerPixel(Format f);
}
