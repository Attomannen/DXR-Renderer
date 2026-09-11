#include "stdafx.h"

#include <tge/render/RenderGraph.h>
#include <tge/render/RenderResourcePool.h>
#include <tge/render/GpuProfiler.h>
#include <tge/render/GpuMarker.h>

#include <unordered_set>

using namespace Tga;

namespace
{
	// The GpuProfiler stores the raw char* and reads it back several frames later,
	// so pass names must have process-lifetime storage. The set of distinct pass
	// names is tiny and bounded, so intern them.
	const char* InternName(const std::string& aName)
	{
		static std::unordered_set<std::string> locArena;
		return locArena.insert(aName).first->c_str();
	}
}

RenderGraph::RenderGraph(RenderResourcePool& aPool, GpuProfiler* aProfiler)
	: myPool(aPool), myProfiler(aProfiler)
{
}

RenderGraph::~RenderGraph() = default;

void RenderGraph::AddPass(std::string aName, ExecuteFn aExecute)
{
	myPasses.push_back({ std::move(aName), std::move(aExecute) });
}

void RenderGraph::Execute()
{
	for (Pass& pass : myPasses)
	{
		const char* name = InternName(pass.name);

		GpuMarkerScope marker(name);
		if (myProfiler)
			myProfiler->Push(name);

		pass.execute(*this);

		if (myProfiler)
			myProfiler->Pop();
	}

	myPasses.clear();
	myPool.ReleaseAll();
}
