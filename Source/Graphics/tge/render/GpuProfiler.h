#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <tge/rhi/Handles.h>
#include <tge/debugging/CpuProfiler.h>

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

		// Rolling statistics per scope path over the last kHistory resolved frames.
		struct ScopeStats
		{
			std::string name;
			int depth = 0;
			int order = 0;
			std::array<float, 240> samples{};
			int count = 0, head = 0;
			uint64_t lastResolve = 0;
			float Last() const { return count ? samples[(head + 239) % 240] : 0.f; }
			float Average() const { double s = 0; for (int i = 0; i < count; ++i) s += samples[i]; return count ? float(s / count) : 0.f; }
			float Max() const { float m = 0; for (int i = 0; i < count; ++i) m = std::max(m, samples[i]); return m; }
		};

		bool Init(int aMaxScopesPerFrame = 96);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void Push(const char* aName);
		void Pop();

		const std::vector<ScopeResult>& GetResults() const { return myResults; }
		double GetFrameGpuMs() const { return myFrameGpuMs; }
		std::vector<const ScopeStats*> GetStats() const;
		const float* GetFrameHistory() const { return myFrameHistory.data(); }
		int GetFrameHistoryOffset() const { return myFrameHistoryHead; }
		int GetFrameHistoryCount() const { return myFrameHistoryCount; }
		void ResetStats() { myStats.clear(); myNextOrder = 0; myFrameHistoryCount = 0; }
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

		std::unordered_map<uint64_t, ScopeStats> myStats;
		int myNextOrder = 0;
		uint64_t myResolveCount = 0;
		std::array<float, 240> myFrameHistory{};
		int myFrameHistoryHead = 0, myFrameHistoryCount = 0;
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

namespace Tga
{
	// CPU + (optional) GPU timing for one block. A null profiler times the CPU
	// side only, so render code can be instrumented without knowing whether a
	// GPU profiler is attached this frame.
	struct ProfileScope
	{
		GpuProfiler* gpu;
		ProfileScope(GpuProfiler* aGpu, const char* aName) : gpu(aGpu)
		{
			CpuProfiler::Get().Push(aName);
			if (gpu) gpu->Push(aName);
		}
		~ProfileScope()
		{
			if (gpu) gpu->Pop();
			CpuProfiler::Get().Pop();
		}
		ProfileScope(const ProfileScope&) = delete;
		ProfileScope& operator=(const ProfileScope&) = delete;
	};
}

#define TGA_PROFILE_SCOPE(gpuProfiler, name) Tga::ProfileScope TGA_GPU_SCOPE_CAT(_profileScope_, __LINE__)((gpuProfiler), (name))
