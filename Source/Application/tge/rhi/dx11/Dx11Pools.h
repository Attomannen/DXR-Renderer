#pragma once
#include "tge/rhi/Pool.h"

// The generic slot pool moved to rhi::Pool (Source/Application/tge/rhi/Pool.h)
// so the DX12 backend can share it too. Kept as an alias here so Dx11Device's
// existing `Pool<T,HandleT>` usages (unqualified, relying on being inside
// namespace Tga::rhi::dx11) keep compiling unchanged.
namespace Tga::rhi::dx11
{
	template <class T, class HandleT>
	using Pool = Tga::rhi::Pool<T, HandleT>;
}
