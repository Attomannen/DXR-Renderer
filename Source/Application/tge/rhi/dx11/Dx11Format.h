#pragma once
#include "tge/rhi/Format.h"

// Format conversion moved to rhi/Format.h (shared with the DX12 backend --
// DXGI_FORMAT is a DXGI type, not D3D11-specific). Aliased here so existing
// unqualified call sites inside namespace Tga::rhi::dx11 keep compiling.
namespace Tga::rhi::dx11
{
	using Tga::rhi::ToDxgi;
	using Tga::rhi::FromDxgi;
	using Tga::rhi::ToTypeless;
	using Tga::rhi::ToDsvFormat;
	using Tga::rhi::ToDepthSrvFormat;
	using Tga::rhi::IsDepth;
	using Tga::rhi::BitsPerPixel;
}
