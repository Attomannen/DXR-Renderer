#include "stdafx.h"

#include <age/render/GpuMarker.h>
#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>

void Ag::GpuMarkerBegin(const char* aName)
{
	if (rhi::IDevice* r = DX11::Rhi())
		r->GetContext().PushMarker(aName);
}

void Ag::GpuMarkerEnd()
{
	if (rhi::IDevice* r = DX11::Rhi())
		r->GetContext().PopMarker();
}
