#pragma once
#include "age/rhi/Format.h"

// Format conversion moved to rhi/Format.h (shared with the DX12 backend --
// DXGI_FORMAT is a DXGI type, not D3D11-specific). Aliased here so existing
// unqualified call sites inside namespace Ag::rhi::dx11 keep compiling.
namespace Ag::rhi::dx11
{
	using Ag::rhi::ToDxgi;
	using Ag::rhi::FromDxgi;
	using Ag::rhi::ToTypeless;
	using Ag::rhi::ToDsvFormat;
	using Ag::rhi::ToDepthSrvFormat;
	using Ag::rhi::IsDepth;
	using Ag::rhi::BitsPerPixel;
}
