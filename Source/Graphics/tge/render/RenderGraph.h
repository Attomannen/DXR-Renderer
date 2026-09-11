#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Tga
{
	class RenderResourcePool;
	class GpuProfiler;

	// Thin linear render graph: a named, ordered list of passes. Each pass is
	// wrapped in a GPU debug marker and (optionally) a GpuProfiler scope, so
	// captures and per-pass timings are automatic. No auto-reordering or aliasing
	// analysis yet -- passes run in the order added. Transient targets come from
	// the RenderResourcePool; the graph calls ReleaseAll() after Execute().
	//
	//   RenderGraph rg(pool, &profiler);
	//   rg.AddPass("GBuffer",  [&](RenderGraph&){ ... });
	//   rg.AddPass("Lighting", [&](RenderGraph&){ ... });
	//   rg.Execute();
	class RenderGraph
	{
	public:
		using ExecuteFn = std::function<void(RenderGraph&)>;

		explicit RenderGraph(RenderResourcePool& aPool, GpuProfiler* aProfiler = nullptr);
		~RenderGraph();

		void AddPass(std::string aName, ExecuteFn aExecute);
		void Execute();

		RenderResourcePool& Pool() const { return myPool; }
		GpuProfiler* Profiler() const { return myProfiler; }

	private:
		struct Pass
		{
			std::string name;
			ExecuteFn execute;
		};

		RenderResourcePool& myPool;
		GpuProfiler* myProfiler;
		std::vector<Pass> myPasses;
	};
}
