#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// Hierarchical CPU profiler for the main thread.
//
//   Ag::CpuProfiler::Get().BeginFrame();
//   { AG_CPU_SCOPE("Update"); ... }
//   Ag::CpuProfiler::Get().EndFrame();
//
// Scopes opened outside BeginFrame/EndFrame (startup, scene loading) are
// collected into a separate "load" list that PrintLoadReport() writes out, so
// the same markers explain both frame time and loading time.
//
// Every scope name must be a string literal (or otherwise outlive the
// profiler): only the pointer is stored.
namespace Ag
{
	class CpuProfiler
	{
	public:
		using Clock = std::chrono::steady_clock;
		static constexpr int kHistory = 240;

		struct ScopeResult
		{
			const char* name = nullptr;
			int depth = 0;
			double ms = 0.0;
		};

		// Rolling statistics per scope path, over the last kHistory frames.
		struct ScopeStats
		{
			const char* name = nullptr;
			int depth = 0;
			int order = 0;                         // first-seen order, for stable display
			std::array<float, kHistory> samples{};
			int count = 0;
			int head = 0;
			uint64_t lastFrame = 0;

			float Last() const { return count ? samples[(head + kHistory - 1) % kHistory] : 0.f; }
			float Average() const
			{
				double sum = 0.0;
				for (int i = 0; i < count; ++i) sum += samples[i];
				return count ? float(sum / count) : 0.f;
			}
			float Max() const
			{
				float m = 0.f;
				for (int i = 0; i < count; ++i) m = std::max(m, samples[i]);
				return m;
			}
		};

		static CpuProfiler& Get()
		{
			static CpuProfiler instance;
			return instance;
		}

		void SetEnabled(bool aEnabled) { myEnabled = aEnabled; }
		bool IsEnabled() const { return myEnabled; }

		void BeginFrame()
		{
			myInFrame = true;
			myCurrent.clear();
			myStack.clear();
			myFrameStart = Clock::now();
		}

		void EndFrame()
		{
			if (!myInFrame) return;
			myInFrame = false;
			myFrameMs = Ms(myFrameStart, Clock::now());
			myFrameHistory[myFrameHead] = float(myFrameMs);
			myFrameHead = (myFrameHead + 1) % kHistory;
			myFrameCount = std::min(myFrameCount + 1, kHistory);
			++myFrameIndex;

			// Aggregate by call path so the same name under different parents
			// stays separate. The path hash is built from parent hashes.
			for (const Open& s : myCurrent)
			{
				ScopeStats& st = myStats[s.pathHash];
				if (!st.name) { st.name = s.name; st.depth = s.depth; st.order = myNextOrder++; }
				if (st.lastFrame == myFrameIndex)
				{
					// Same path twice in one frame (a loop): accumulate.
					const int last = (st.head + kHistory - 1) % kHistory;
					st.samples[last] += float(s.ms);
				}
				else
				{
					st.samples[st.head] = float(s.ms);
					st.head = (st.head + 1) % kHistory;
					st.count = std::min(st.count + 1, kHistory);
					st.lastFrame = myFrameIndex;
				}
			}
			myLastFrame = myCurrent;
		}

		void Push(const char* aName)
		{
			if (!myEnabled) return;
			const uint64_t parent = myStack.empty() ? 1469598103934665603ull : myOpen(myStack.back()).pathHash;
			Open s;
			s.name = aName;
			s.depth = int(myStack.size());
			s.pathHash = (parent ^ uint64_t(std::hash<std::string_view>()(aName))) * 1099511628211ull;
			s.start = Clock::now();
			List().push_back(s);
			myStack.push_back(int(List().size()) - 1);
		}

		void Pop()
		{
			if (!myEnabled || myStack.empty()) return;
			Open& s = myOpen(myStack.back());
			s.ms = Ms(s.start, Clock::now());
			myStack.pop_back();
		}

		// Last completed frame, in call order.
		std::vector<ScopeResult> GetLastFrame() const
		{
			std::vector<ScopeResult> out;
			out.reserve(myLastFrame.size());
			for (const Open& s : myLastFrame) out.push_back({ s.name, s.depth, s.ms });
			return out;
		}

		// Stats for every path seen, sorted in first-seen (call) order.
		std::vector<const ScopeStats*> GetStats() const
		{
			std::vector<const ScopeStats*> out;
			out.reserve(myStats.size());
			for (const auto& [hash, st] : myStats)
				if (myFrameIndex - st.lastFrame < uint64_t(kHistory)) out.push_back(&st);
			std::sort(out.begin(), out.end(), [](const ScopeStats* a, const ScopeStats* b) { return a->order < b->order; });
			return out;
		}

		void ResetStats() { myStats.clear(); myNextOrder = 0; myFrameCount = 0; myFrameHead = 0; }

		double GetFrameMs() const { return myFrameMs; }
		const float* GetFrameHistory() const { return myFrameHistory.data(); }
		int GetFrameHistoryOffset() const { return myFrameHead; }
		int GetFrameHistoryCount() const { return myFrameCount; }

		// Startup / loading scopes (opened outside a frame).
		std::vector<ScopeResult> GetLoadScopes() const
		{
			std::vector<ScopeResult> out;
			for (const Open& s : myLoad) out.push_back({ s.name, s.depth, s.ms });
			return out;
		}

		// Repeated scopes under the same parent (e.g. one per shader or texture)
		// are merged into one line with a call count.
		struct LoadEntry { const char* name; int depth; double ms; int calls; };
		std::vector<LoadEntry> GetLoadReport() const
		{
			std::vector<LoadEntry> out;
			std::unordered_map<uint64_t, size_t> index;
			for (const Open& s : myLoad)
			{
				auto [it, inserted] = index.try_emplace(s.pathHash, out.size());
				if (inserted) out.push_back({ s.name, s.depth, 0.0, 0 });
				out[it->second].ms += s.ms;
				++out[it->second].calls;
			}
			return out;
		}

		void PrintLoadReport(double aMinMs = 1.0) const
		{
			std::printf("---- load profile (scopes >= %.1f ms) ----\n", aMinMs);
			for (const LoadEntry& e : GetLoadReport())
			{
				if (e.ms < aMinMs) continue;
				if (e.calls > 1)
					std::printf("%*s%-44s %9.1f ms  (%d calls)\n", e.depth * 2, "", e.name, e.ms, e.calls);
				else
					std::printf("%*s%-44s %9.1f ms\n", e.depth * 2, "", e.name, e.ms);
			}
			std::printf("-------------------------------------------\n");
			std::fflush(stdout);
		}

		void ClearLoadScopes() { myLoad.clear(); }

	private:
		struct Open
		{
			const char* name = nullptr;
			int depth = 0;
			uint64_t pathHash = 0;
			Clock::time_point start;
			double ms = 0.0;
		};

		static double Ms(Clock::time_point a, Clock::time_point b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		}

		std::vector<Open>& List() { return myInFrame ? myCurrent : myLoad; }
		Open& myOpen(int aIndex) { return List()[aIndex]; }

		bool myEnabled = true;
		bool myInFrame = false;
		std::vector<Open> myCurrent, myLastFrame, myLoad;
		std::vector<int> myStack;
		std::unordered_map<uint64_t, ScopeStats> myStats;
		int myNextOrder = 0;
		uint64_t myFrameIndex = 0;
		Clock::time_point myFrameStart;
		double myFrameMs = 0.0;
		std::array<float, kHistory> myFrameHistory{};
		int myFrameHead = 0, myFrameCount = 0;
	};

	struct CpuScope
	{
		explicit CpuScope(const char* aName) { CpuProfiler::Get().Push(aName); }
		~CpuScope() { CpuProfiler::Get().Pop(); }
		CpuScope(const CpuScope&) = delete;
		CpuScope& operator=(const CpuScope&) = delete;
	};
}

#define AG_CPU_SCOPE_CAT2(a, b) a##b
#define AG_CPU_SCOPE_CAT(a, b) AG_CPU_SCOPE_CAT2(a, b)
#define AG_CPU_SCOPE(name) Ag::CpuScope AG_CPU_SCOPE_CAT(_cpuScope_, __LINE__)(name)
