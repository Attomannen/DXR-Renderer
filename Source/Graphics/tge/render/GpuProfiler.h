#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <tge/rhi/Handles.h>

namespace Tga
{
	// Lightweight D3D11 timestamp profiler. Records nested GPU scopes per frame and
	// reads them back a few frames later so it never stalls the pipeline.
	//
	//   profiler.BeginFrame();
	//   { TGA_GPU_SCOPE(profiler, "Geometry");  ...draw... }
	//   { TGA_GPU_SCOPE(profiler, "Lighting");  ...draw... }
	//   profiler.EndFrame();
	//   for (auto& r : profiler.GetResults()) ...   // most recently resolved frame
	class GpuProfiler
	{
	public:
		struct ScopeResult
		{
			std::string name;
			double      ms = 0.0;
			int         depth = 0;
		};

		bool Init(int aMaxScopesPerFrame = 48);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void Push(const char* aName);
		void Pop();

		const std::vector<ScopeResult>& GetResults() const { return myResults; }
		double GetFrameGpuMs() const { return myFrameGpuMs; }
		bool IsReady() const { return myReadyOnce; }

	private:
		static constexpr int kBufferedFrames = 5;

		struct Scope { const char* name = nullptr; int depth = 0; };
		struct Frame
		{
			std::vector<rhi::TimestampQueryHandle> query;   // one per possible scope
			std::vector<Scope> scopes;                      // recorded this frame
			bool pending = false;
		};

		void Resolve(Frame& aFrame);

		int   myMaxScopes = 0;
		Frame myFrames[kBufferedFrames];
		int   myWrite = 0;
		int   myDepth = 0;
		std::vector<int> myStack;              // indices into current frame's scopes
		uint64_t myFrameIndex = 0;

		std::vector<ScopeResult> myResults;
		double myFrameGpuMs = 0.0;
		bool myReadyOnce = false;
	};

	struct GpuScope
	{
		GpuProfiler& p;
		GpuScope(GpuProfiler& aProfiler, const char* aName) : p(aProfiler) { p.Push(aName); }
		~GpuScope() { p.Pop(); }
		GpuScope(const GpuScope&) = delete;
		GpuScope& operator=(const GpuScope&) = delete;
	};
}

#define TGA_GPU_SCOPE_CAT2(a, b) a##b
#define TGA_GPU_SCOPE_CAT(a, b) TGA_GPU_SCOPE_CAT2(a, b)
#define TGA_GPU_SCOPE(profiler, name) Tga::GpuScope TGA_GPU_SCOPE_CAT(_gpuScope_, __LINE__)((profiler), (name))
