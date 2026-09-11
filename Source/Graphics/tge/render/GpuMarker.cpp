#include "stdafx.h"

#include <tge/render/GpuMarker.h>
#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>

void Tga::GpuMarkerBegin(const char* aName)
{
	if (rhi::IDevice* r = DX11::Rhi())
		r->GetContext().PushMarker(aName);
}

void Tga::GpuMarkerEnd()
{
	if (rhi::IDevice* r = DX11::Rhi())
		r->GetContext().PopMarker();
}
