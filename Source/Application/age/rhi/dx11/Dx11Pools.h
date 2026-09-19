#pragma once
#include "age/rhi/Pool.h"

// The generic slot pool moved to rhi::Pool (Source/Application/age/rhi/Pool.h)
// so the DX12 backend can share it too. Kept as an alias here so Dx11Device's
// existing `Pool<T,HandleT>` usages (unqualified, relying on being inside
// namespace Ag::rhi::dx11) keep compiling unchanged.
namespace Ag::rhi::dx11
{
	template <class T, class HandleT>
	using Pool = Ag::rhi::Pool<T, HandleT>;
}
