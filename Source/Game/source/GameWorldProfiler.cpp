#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: Bench report, feature cost sweep and the Profiler tab.

void GameWorld::Impl::WriteReport()
{
	if (reportWritten) return;
	reportWritten = true;

	auto stats = [](std::vector<double> v)
	{
		std::sort(v.begin(), v.end());
		const size_t n = v.size();
		double sum = std::accumulate(v.begin(), v.end(), 0.0);
		double mean = n ? sum / n : 0.0;
		double var = 0.0; for (double x : v) var += (x - mean) * (x - mean);
		var = n ? var / n : 0.0;
		auto pct = [&](double p) { return n ? v[std::min(n - 1, (size_t)(p * (n - 1) + 0.5))] : 0.0; };
		return std::tuple<double, double, double, double, double, double, double, double>(
			mean, std::sqrt(var), n ? v.front() : 0.0, n ? v.back() : 0.0,
			pct(0.01), pct(0.50), pct(0.95), pct(0.99));
	};

	auto [fMean, fStd, fMin, fMax, fP1, fP50, fP95, fP99] = stats(frameMs);
	auto [cMean, cStd, cMin, cMax, cP1, cP50, cP95, cP99] = stats(cpuMs);
	double dcMean = 0.0;
	if (!drawCalls.empty())
		dcMean = std::accumulate(drawCalls.begin(), drawCalls.end(), 0.0) / drawCalls.size();
	double visMean = 0.0;
	if (!visibleInstances.empty())
		visMean = std::accumulate(visibleInstances.begin(), visibleInstances.end(), 0.0) / visibleInstances.size();

	const Vector2ui res = Application::GetInstance()->GetRenderSize();
	size_t subMeshes = 0;
	for (auto& m : models) if (m.GetModel()) subMeshes += m.GetModel()->GetMeshCount();

	std::ofstream o(reportPath);
	o.setf(std::ios::fixed); o.precision(4);
	o << "{\n";
	o << "  \"scene\": \"" << currentScene << "\",\n";
	o << "  \"renderer\": \"" << (useDeferred ? "deferred" : "forward") << "\",\n";
	o << "  \"model_load_ms\": " << modelLoadMs << ",\n";
	o << "  \"first_frame_ms\": " << firstFrameMs << ",\n";
	o << "  \"resolution\": [" << res.x << ", " << res.y << "],\n";
	o << "  \"vsync\": " << (Settings::GetApplicationConfiguration().enableVSync ? "true" : "false") << ",\n";
	o << "  \"sponza_copies\": " << sponzaCopies << ",\n";
	o << "  \"model_instances\": " << models.size() << ",\n";
	o << "  \"frustum_cull\": " << (frustumCull ? "true" : "false") << ",\n";
	o << "  \"visible_instances_mean\": " << visMean << ",\n";
	o << "  \"sub_meshes_total\": " << subMeshes << ",\n";
	o << "  \"point_lights\": " << pointLights.size() << ",\n";
	o << "  \"frames_measured\": " << frameMs.size() << ",\n";
	o << "  \"warmup_frames\": " << warmupFrames << ",\n";
	o << "  \"frame_ms\":  { \"mean\": " << fMean << ", \"std\": " << fStd
	  << ", \"min\": " << fMin << ", \"max\": " << fMax
	  << ", \"p1\": " << fP1 << ", \"p50\": " << fP50 << ", \"p95\": " << fP95 << ", \"p99\": " << fP99 << " },\n";
	o << "  \"cpu_ms\":    { \"mean\": " << cMean << ", \"std\": " << cStd
	  << ", \"min\": " << cMin << ", \"max\": " << cMax
	  << ", \"p1\": " << cP1 << ", \"p50\": " << cP50 << ", \"p95\": " << cP95 << ", \"p99\": " << cP99 << " },\n";
	o << "  \"fps_mean\": " << (fMean > 0 ? 1000.0 / fMean : 0.0) << ",\n";
	o << "  \"draw_calls_mean\": " << dcMean << ",\n";

	double gpuFrame = 0.0;
	if (!gpuFrameMs.empty())
		gpuFrame = std::accumulate(gpuFrameMs.begin(), gpuFrameMs.end(), 0.0) / gpuFrameMs.size();
	o << "  \"gpu_ms\": { \"frame\": " << gpuFrame;
	for (const auto& [name, samples] : gpuScopeMs)
	{
		double m = samples.empty() ? 0.0
			: std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
		o << ", \"" << name << "\": " << m;
	}
	o << " },\n";
	if (!costResults.empty())
	{
		o << "  \"feature_costs_ms\": { \"baseline\": " << costBaselineMs;
		for (const CostResult& r : costResults)
			if (!r.skipped) o << ", \"" << r.name << "\": [" << r.deltaMs << ", " << r.spreadMs << "]";
		o << " },\n";
	}
	o << "  \"cpu_scopes_ms\": {";
	{
		bool first = true;
		for (const CpuProfiler::ScopeStats* st : CpuProfiler::Get().GetStats())
		{
			o << (first ? " " : ", ") << "\"" << std::string(st->depth * 2, ' ') << st->name << "\": " << st->Average();
			first = false;
		}
	}
	o << " }\n";
	o << "}\n";
	o.close();

	INFO_PRINT("=== Sponza bench: %zu frames | frame %.3f ms (%.1f fps) | cpu %.3f ms | %.0f draw calls | report -> %s",
		frameMs.size(), fMean, fMean > 0 ? 1000.0 / fMean : 0.0, cMean, dcMean, reportPath.c_str());
}

void GameWorld::Impl::BuildCostProbes()
{
	costProbes.clear();
	if (!deferred) return;
	DeferredRenderer::Tunables* t = &deferred->GetTunables();
	auto flag = [&](const char* name, bool* value)
	{
		const bool original = *value;
		costProbes.push_back({ name, [=]() { return original; }, [=](bool on) { *value = on ? original : false; } });
	};
	auto reduce = [&](const char* name, int* value, int reduced)
	{
		const int original = *value;
		costProbes.push_back({ name, [=]() { return original > reduced; }, [=](bool on) { *value = on ? original : reduced; } });
	};
	flag("Reflections", &t->dxrReflections);
	reduce("Reflection samples -> 1", &t->dxrReflectionSamples, 1);
	flag("Ambient occlusion", &t->dxrAmbientOcclusion);
	reduce("AO samples -> 1", &t->dxrAoSamples, 1);
	flag("Direct light + shadows", &t->dxrDirectLighting);
	flag("Indirect GI lookup", &t->dxrIndirectGi);
	flag("Environment light", &t->dxrEnvironmentLighting);
	flag("Texture filtering (ray cones)", &t->dxrTextureFiltering);
	flag("Specular AA", &t->specularAaEnabled);
	flag("NRD denoiser", &t->nrdEnabled);
	if (t->nrdEnabled)
	{
		const bool original = t->nrdCheckerboard;
		costProbes.push_back({ "Full-res AO + reflections (vs checkerboard)", [=]() { return !original; },
			[=](bool on) { t->nrdCheckerboard = on ? original : true; } });
	}
	reduce("Sun shadow rays -> 1", &t->dxrSunShadowSamples, 1);
	{
		const int original = t->volumetricResolution;
		costProbes.push_back({ "Fog volume at quarter res", [=]() { return original < 2; },
			[=](bool on) { t->volumetricResolution = on ? original : 2; } });
	}
	flag("TAA", &t->taaEnabled);
	flag("DLAA", &t->dlaaEnabled);
	flag("Fog", &t->fogEnabled);
	flag("Volumetric sunlight", &t->volumetricEnabled);
	flag("Bloom", &t->bloomEnabled);
	flag("GI probe updates", &giKeepUpdating);
	reduce("GI rays per probe -> 32", &giRTRayCount, 32);
}

void GameWorld::Impl::StartCostSweep()
{
	BuildCostProbes();
	costResults.clear();
	costSamples.clear();
	costOnMs.clear();
	costProbe = -1;
	costFrame = 0;
	costPhaseOff = false;
	AdvanceCostProbe();
}

void GameWorld::Impl::StopCostSweep()
{
	if (costProbe >= 0 && costProbe < (int)costProbes.size()) costProbes[costProbe].set(true);
	costProbe = -2;
}

float GameWorld::Impl::Percentile(std::vector<float> v, float q)
{
	if (v.empty()) return 0.f;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, size_t(q * float(v.size())))];
}

void GameWorld::Impl::AdvanceCostProbe()
{
	for (++costProbe; costProbe < (int)costProbes.size(); ++costProbe)
	{
		if (costProbes[costProbe].isOn()) { costPhaseOff = false; costCycle = 0; costDeltas.clear(); return; }
		costResults.push_back({ costProbes[costProbe].name, 0.f, 0.f, true });
	}
	costProbe = -2;
	if (!costOnMs.empty()) costBaselineMs = Median(costOnMs);
	std::stable_sort(costResults.begin(), costResults.end(),
		[](const CostResult& a, const CostResult& b) { return a.deltaMs > b.deltaMs; });
}

void GameWorld::Impl::StepCostSweep()
{
	if (costProbe < 0 || !gpu.IsReady()) return;
	if (++costFrame > kCostWarmupFrames) costSamples.push_back(float(gpu.GetFrameGpuMs()));
	if (costFrame < kCostWarmupFrames + kCostMeasureFrames) return;

	// Lower quartile: the steady-state frame, ignoring intermittent spikes
	// such as GI probe batches or shader/PSO hitches.
	const float ms = Percentile(costSamples, 0.25f);
	costSamples.clear();
	costFrame = 0;
	CostProbe& probe = costProbes[costProbe];
	if (!costPhaseOff)
	{
		costCurrentOnMs = ms;
		costOnMs.push_back(ms);
		probe.set(false);
		costPhaseOff = true;
		return;
	}
	probe.set(true);
	costPhaseOff = false;
	costDeltas.push_back(costCurrentOnMs - ms);
	costCurrentOffMs = ms;
	if (++costCycle < kCostCycles) return;
	const auto [lo, hi] = std::minmax_element(costDeltas.begin(), costDeltas.end());
	costResults.push_back({ probe.name, costCurrentOffMs, Median(costDeltas), false, (*hi - *lo) * 0.5f });
	AdvanceCostProbe();
}

template <class Stats>
void GameWorld::Impl::DrawScopeTable(const char* id, const std::vector<const Stats*>& stats, float frameMs, std::string& report)
{
	if (!ImGui::BeginTable(id, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 3.0f);
	ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthStretch, 0.8f);
	ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthStretch, 0.8f);
	ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.8f);
	ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_WidthStretch, 1.4f);
	ImGui::TableHeadersRow();
	for (const Stats* st : stats)
	{
		const float avg = st->Average();
		const float frac = frameMs > 0.f ? std::clamp(avg / frameMs, 0.f, 1.f) : 0.f;
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Indent(float(st->depth) * 12.f + 0.01f);
		ImGui::TextUnformatted(ScopeName(st->name));
		ImGui::Unindent(float(st->depth) * 12.f + 0.01f);
		ImGui::TableNextColumn(); ImGui::Text("%.3f", st->Last());
		ImGui::TableNextColumn();
		const ImVec4 hot = avg > 2.0f ? ImVec4(1, .45f, .35f, 1) : avg > 0.5f ? ImVec4(1, .85f, .4f, 1) : ImVec4(.85f, .85f, .85f, 1);
		ImGui::TextColored(hot, "%.3f", avg);
		ImGui::TableNextColumn(); ImGui::Text("%.3f", st->Max());
		ImGui::TableNextColumn();
		char overlay[16];
		std::snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.f);
		ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);

		char line[256];
		std::snprintf(line, sizeof(line), "%*s%-40s last %8.3f  avg %8.3f  max %8.3f ms\n",
			st->depth * 2, "", ScopeName(st->name), st->Last(), avg, st->Max());
		report += line;
	}
	ImGui::EndTable();
}

void GameWorld::Impl::DrawHistory(const char* label, const float* history, int offset, int count, float target)
{
	if (count <= 0) return;
	float peak = target;
	for (int i = 0; i < count; ++i) peak = std::max(peak, history[i]);
	char overlay[64];
	std::snprintf(overlay, sizeof(overlay), "%s (max %.2f ms)", label, peak);
	ImGui::PlotLines("##history", history, count, count == CpuProfiler::kHistory ? offset : 0, overlay, 0.f, peak * 1.1f, ImVec2(-FLT_MIN, 64));
}

void GameWorld::Impl::DrawProfilerTab()
{
	std::string report;
	CpuProfiler& cpu = CpuProfiler::Get();
	auto historyAverage = [](const float* h, int count)
	{
		double sum = 0.0;
		for (int i = 0; i < count; ++i) sum += h[i];
		return count ? float(sum / count) : 0.f;
	};
	// The frame scope includes the time the CPU sits blocked on the GPU;
	// report CPU work without it.
	float gpuWaitMs = 0.f;
	for (const CpuProfiler::ScopeStats* st : cpu.GetStats())
		if (st->depth == 0 && std::string_view(st->name) == "Device begin frame (GPU wait)") gpuWaitMs = st->Average();
	const float cpuFrameMs = historyAverage(cpu.GetFrameHistory(), cpu.GetFrameHistoryCount());
	const float cpuWorkMs = std::max(cpuFrameMs - gpuWaitMs, 0.f);
	const float gpuMs = historyAverage(gpu.GetFrameHistory(), gpu.GetFrameHistoryCount());
	ImGui::Text("Frame %.2f ms (%.0f fps)   CPU work %.2f ms   GPU %.2f ms   (averages)",
		ImGui::GetIO().DeltaTime * 1000.f, ImGui::GetIO().Framerate, cpuWorkMs, gpuMs);
	{
		char line[160];
		std::snprintf(line, sizeof(line), "Frame %.2f ms | CPU %.2f ms | GPU %.2f ms\n", ImGui::GetIO().DeltaTime * 1000.f, cpuWorkMs, gpuMs);
		report += line;
	}
	if (rhi::IDevice* dev = DX11::Rhi())
	{
		uint64_t usage = 0, budget = 0;
		if (dev->QueryVideoMemory(usage, budget))
		{
			const float frac = budget ? float(double(usage) / double(budget)) : 0.f;
			char overlay[64];
			std::snprintf(overlay, sizeof(overlay), "VRAM %.2f / %.2f GB", usage / 1073741824.0, budget / 1073741824.0);
			ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
			if (frac > 0.9f) ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "Near the VRAM budget: expect paging stalls.");
			report += std::string(overlay) + "\n";
		}
	}
	if (deferred)
	{
		const DeferredRenderer::Tunables& t = deferred->GetTunables();
		ImGui::TextDisabled("%s | NRD %s | DLSS mode %d | DLAA %s | RR %s | reflections %d spp | AO %d spp | GI %d rays/probe",
			dxrRenderer ? "DXR" : "Raster", t.nrdEnabled ? "on" : "off", t.dlssMode, t.dlaaEnabled ? "on" : "off",
			t.rayReconstructionEnabled ? "on" : "off", t.dxrReflectionSamples, t.dxrAoSamples, giRTRayCount);
	}

	DrawHistory("CPU work", cpu.GetFrameHistory(), cpu.GetFrameHistoryOffset(), cpu.GetFrameHistoryCount(), 8.33f);
	DrawHistory("GPU", gpu.GetFrameHistory(), gpu.GetFrameHistoryOffset(), gpu.GetFrameHistoryCount(), 8.33f);
	bool enabled = cpu.IsEnabled();
	if (ImGui::Checkbox("Collect CPU scopes", &enabled)) cpu.SetEnabled(enabled);
	ImGui::SameLine();
	if (ImGui::Button("Reset stats")) { cpu.ResetStats(); gpu.ResetStats(); }
	ImGui::SameLine();
	const bool copy = ImGui::Button("Copy report");

	if (ImGui::CollapsingHeader("GPU scopes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		report += "-- GPU scopes --\n";
		DrawScopeTable("gpuScopes", gpu.GetStats(), gpuMs, report);
		ImGui::TextDisabled("\"Ray trace + shade\" is one dispatch; use the feature cost sweep to split it.");
	}
	if (ImGui::CollapsingHeader("CPU scopes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		report += "-- CPU scopes --\n";
		DrawScopeTable("cpuScopes", cpu.GetStats(), cpuFrameMs, report);
		ImGui::TextDisabled("\"Device begin frame (GPU wait)\" is the CPU blocked on the GPU, not CPU work.");
	}
	if (ImGui::CollapsingHeader("Feature cost sweep", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::TextWrapped("Toggles each enabled feature off and on %d times, lets the image settle, and compares steady-state GPU frame times. "
			"Keep the camera still while it runs (about %d s per feature at 60 fps).", kCostCycles, 2 * kCostCycles * (kCostWarmupFrames + kCostMeasureFrames) / 60);
		if (costProbe < 0)
		{
			if (ImGui::Button("Measure feature costs")) StartCostSweep();
		}
		else
		{
			const float window = float(costFrame) / float(kCostWarmupFrames + kCostMeasureFrames);
			const float phase = (float(costCycle) + (costPhaseOff ? 0.5f : 0.f) + 0.5f * window) / float(kCostCycles);
			const float progress = (float(costProbe) + phase) / float(std::max<size_t>(costProbes.size(), 1));
			char overlay[96];
			std::snprintf(overlay, sizeof(overlay), "%s (%s)", costProbes[costProbe].name, costPhaseOff ? "off" : "on");
			ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0), overlay);
			if (ImGui::Button("Stop")) StopCostSweep();
		}
		if (!costResults.empty() && ImGui::BeginTable("costs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			report += "-- Feature costs (baseline " + std::to_string(costBaselineMs) + " ms) --\n";
			ImGui::TableSetupColumn("Feature (turned off)");
			ImGui::TableSetupColumn("GPU ms without");
			ImGui::TableSetupColumn("Cost");
			ImGui::TableSetupColumn("+/- spread");
			ImGui::TableHeadersRow();
			for (const CostResult& r : costResults)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name);
				if (r.skipped)
				{
					ImGui::TableNextColumn(); ImGui::TextDisabled("already off");
					ImGui::TableNextColumn();
					ImGui::TableNextColumn();
					continue;
				}
				ImGui::TableNextColumn(); ImGui::Text("%.2f", r.frameMs);
				ImGui::TableNextColumn();
				const ImVec4 hot = r.deltaMs > 2.f ? ImVec4(1, .45f, .35f, 1) : r.deltaMs > 0.5f ? ImVec4(1, .85f, .4f, 1) : ImVec4(.7f, .9f, .7f, 1);
				const bool withinNoise = std::abs(r.deltaMs) <= r.spreadMs;
				ImGui::TextColored(withinNoise ? ImVec4(.6f, .6f, .6f, 1) : hot, "%+.2f ms", r.deltaMs);
				ImGui::TableNextColumn(); ImGui::TextDisabled("%.2f", r.spreadMs);
				char line[128];
				std::snprintf(line, sizeof(line), "%-34s %+7.2f ms  (+/- %.2f)\n", r.name, r.deltaMs, r.spreadMs);
				report += line;
			}
			ImGui::EndTable();
			ImGui::TextDisabled("Frame with everything on: %.2f ms. Grey costs are within the measurement spread. Costs overlap (reflections include their own GI lookups), so they need not sum to it.", costBaselineMs);
		}
	}
	if (ImGui::CollapsingHeader("Startup / load profile"))
	{
		report += "-- Load profile --\n";
		for (const CpuProfiler::LoadEntry& e : cpu.GetLoadReport())
		{
			if (e.ms < 1.0) continue;
			if (e.calls > 1) ImGui::Text("%*s%-40s %9.1f ms (%d calls)", e.depth * 2, "", e.name, e.ms, e.calls);
			else ImGui::Text("%*s%-40s %9.1f ms", e.depth * 2, "", e.name, e.ms);
			char line[160];
			std::snprintf(line, sizeof(line), "%*s%-40s %9.1f ms (%d)\n", e.depth * 2, "", e.name, e.ms, e.calls);
			report += line;
		}
	}
	if (copy) ImGui::SetClipboardText(report.c_str());
}

void GameWorld::Impl::DrawPerfOverlayImpl()
{
#ifndef _RETAIL
	if (!showPerfOverlay) return;
	ImGui::SetNextWindowPos({ 8.f, 8.f }, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.55f);
	const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
		| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoFocusOnAppearing;
	if (ImGui::Begin("##perf", nullptr, f))
	{
		const float dtMs = Application::GetInstance()->GetDeltaTime() * 1000.f;
		ImGui::Text("CPU  %6.2f ms   %4.0f fps", dtMs, dtMs > 0.f ? 1000.f / dtMs : 0.f);
		if (gpu.IsReady())
		{
			ImGui::Text("GPU  %6.2f ms", gpu.GetFrameGpuMs());
			ImGui::Separator();
			for (const GpuProfiler::ScopeResult& r : gpu.GetResults())
				ImGui::Text("%*s%-13s %6.3f", r.depth * 2, "", r.name.c_str(), r.ms);
		}
		else ImGui::TextDisabled("GPU  (warming up)");
	}
	ImGui::End();
#endif
}
