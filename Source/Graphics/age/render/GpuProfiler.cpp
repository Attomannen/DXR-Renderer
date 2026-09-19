#include "stdafx.h"
#include <age/render/GpuProfiler.h>

#include <age/graphics/DX11.h>
#include <age/rhi/Device.h>
#include <age/log/Log.h>

using namespace Ag;

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

	// Fold into rolling per-path stats (path = chain of names down to this scope).
	++myResolveCount;
	uint64_t pathAtDepth[64] = {};
	for (const ScopeResult& r : myResults)
	{
		const int d = std::clamp(r.depth, 0, 63);
		const uint64_t parent = d == 0 ? 1469598103934665603ull : pathAtDepth[d - 1];
		const uint64_t path = (parent ^ std::hash<std::string>()(r.name)) * 1099511628211ull;
		pathAtDepth[d] = path;
		ScopeStats& st = myStats[path];
		if (st.name.empty()) { st.name = r.name; st.depth = r.depth; st.order = myNextOrder++; }
		if (st.lastResolve == myResolveCount)
			st.samples[(st.head + 239) % 240] += float(r.ms);
		else
		{
			st.samples[st.head] = float(r.ms);
			st.head = (st.head + 1) % 240;
			st.count = std::min(st.count + 1, 240);
			st.lastResolve = myResolveCount;
		}
	}
	myFrameHistory[myFrameHistoryHead] = float(total);
	myFrameHistoryHead = (myFrameHistoryHead + 1) % 240;
	myFrameHistoryCount = std::min(myFrameHistoryCount + 1, 240);
}

std::vector<const GpuProfiler::ScopeStats*> GpuProfiler::GetStats() const
{
	std::vector<const ScopeStats*> out;
	for (const auto& [path, st] : myStats)
		if (myResolveCount - st.lastResolve < 240) out.push_back(&st);
	std::sort(out.begin(), out.end(), [](const ScopeStats* a, const ScopeStats* b) { return a->order < b->order; });
	return out;
}
