#include "stdafx.h"
#include "tge/rhi/Device.h"
#include "tge/rhi/dx11/Dx11Device.h"

namespace Tga::rhi
{
	std::unique_ptr<IDevice> CreateDevice(Backend backend, const DeviceDesc& desc)
	{
		switch (backend)
		{
		case Backend::DX11:
			return std::make_unique<dx11::Dx11Device>(desc);
		case Backend::DX12:
			// Stage 2.
			return nullptr;
		}
		return nullptr;
	}
}
