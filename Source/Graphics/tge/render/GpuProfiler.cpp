#include "stdafx.h"
#include <tge/render/GpuProfiler.h>

#include <tge/graphics/DX11.h>
#include <tge/rhi/Device.h>
#include <tge/log/Log.h>

using namespace Tga;

bool GpuProfiler::Init(int aMaxScopesPerFrame)
{
	myMaxScopes = aMaxScopesPerFrame;
	myStack.reserve(myMaxScopes);

	rhi::IDevice* dev = DX11::Rhi();
	if (!dev)
	{
		ERROR_PRINT("GpuProfiler: no RHI device");
		return false;
	}

	for (Frame& f : myFrames)
	{
		f.query.resize(myMaxScopes);
		for (int i = 0; i < myMaxScopes; ++i)
		{
			f.query[i] = dev->CreateTimestampQuery();
			if (!f.query[i].IsValid())
			{
				ERROR_PRINT("GpuProfiler: failed to create timestamp query");
				return false;
			}
		}
		f.scopes.reserve(myMaxScopes);
	}
	return true;
}

void GpuProfiler::Shutdown()
{
	if (rhi::IDevice* dev = DX11::Rhi())
	{
		for (Frame& f : myFrames)
			for (rhi::TimestampQueryHandle h : f.query)
				dev->DestroyTimestampQuery(h);
	}
	for (Frame& f : myFrames)
	{
		f.query.clear();
		f.scopes.clear();
		f.pending = false;
	}
	myResults.clear();
}

void GpuProfiler::BeginFrame()
{
	myWrite = (int)(myFrameIndex % kBufferedFrames);
	Frame& f = myFrames[myWrite];

	// If this slot still has unresolved data (readback fell behind), drop it.
	f.scopes.clear();
	f.pending = false;
	myDepth = 0;
	myStack.clear();
}

void GpuProfiler::Push(const char* aName)
{
	Frame& f = myFrames[myWrite];
	const int idx = (int)f.scopes.size();
	if (idx >= myMaxScopes)
		return;   // silently ignore excess scopes

	DX11::Rhi()->GetContext().WriteTimestampBegin(f.query[idx]);
	f.scopes.push_back({ aName, myDepth });
	myStack.push_back(idx);
	++myDepth;
}

void GpuProfiler::Pop()
{
	if (myStack.empty())
		return;
	Frame& f = myFrames[myWrite];
	const int idx = myStack.back();
	myStack.pop_back();
	--myDepth;
	DX11::Rhi()->GetContext().WriteTimestampEnd(f.query[idx]);
}

void GpuProfiler::EndFrame()
{
	Frame& f = myFrames[myWrite];
	f.pending = true;

	// Try to resolve the oldest buffered frame (non-blocking).
	Frame& oldest = myFrames[(myWrite + 1) % kBufferedFrames];
	if (oldest.pending)
		Resolve(oldest);

	++myFrameIndex;
}

void GpuProfiler::Resolve(Frame& f)
{
	rhi::IDevice* dev = DX11::Rhi();
	if (!dev)
		return;

	std::vector<ScopeResult> results;
	results.reserve(f.scopes.size());
	double total = 0.0;

	for (size_t i = 0; i < f.scopes.size(); ++i)
	{
		double ms = 0.0;
		if (!dev->GetTimestampMs(f.query[i], ms))
			return;   // not all scopes ready yet — retry next frame

		results.push_back({ f.scopes[i].name ? f.scopes[i].name : "?", ms, f.scopes[i].depth });
		if (f.scopes[i].depth == 0)
			total += ms;
	}

	f.pending = false;
	myResults = std::move(results);
	myFrameGpuMs = total;
	myReadyOnce = true;
}
