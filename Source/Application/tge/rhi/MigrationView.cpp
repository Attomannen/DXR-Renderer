#include "stdafx.h"
#include "tge/rhi/MigrationView.h"
#include "tge/rhi/Device.h"
#include <tge/graphics/DX11.h>

namespace Tga
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
		}
	}
}
