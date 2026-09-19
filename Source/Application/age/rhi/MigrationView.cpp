#include "stdafx.h"
#include "age/rhi/MigrationView.h"
#include "age/rhi/Device.h"
#include <age/graphics/DX11.h>

namespace Ag
{
	void MigrationView_Destroy(uint32_t index, uint32_t generation, int kind)
	{
		rhi::IDevice* r = DX11::Rhi();
		if (!r) return;   // device already gone at shutdown; pool released the view
		switch (kind)
		{
		case 0: r->Destroy(rhi::SrvHandle{ index, generation }); break;
		case 1: r->Destroy(rhi::RtvHandle{ index, generation }); break;
		case 2: r->Destroy(rhi::DsvHandle{ index, generation }); break;
		case 3: r->Destroy(rhi::TextureHandle{ index, generation }); break;
		}
	}
}
