#include "stdafx.h"
#include "tge/rhi/Device.h"
#include "tge/rhi/dx11/Dx11Device.h"
#include "tge/rhi/dx12/Dx12Device.h"

namespace Tga::rhi
{
	std::unique_ptr<IDevice> CreateDevice(Backend backend, const DeviceDesc& desc)
	{
		switch (backend)
		{
		case Backend::DX11:
			return std::make_unique<dx11::Dx11Device>(desc);
		case Backend::DX12:
			// Stage 2, milestone 1: device/swapchain/heaps/resources/present
			// loop only -- see Dx12Device.h's class comment. Not wired into
			// DX11::Init()'s live bootstrap yet; construct directly (as the
			// isolated smoke test does) until that wiring lands.
			return std::make_unique<dx12::Dx12Device>(desc);
		}
		return nullptr;
	}
}
