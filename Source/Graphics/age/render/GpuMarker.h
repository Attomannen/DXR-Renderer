#pragma once

// Lightweight GPU debug-event markers (PIX / RenderDoc / Nsight). No-ops when no
// annotation interface is available. Pair every Begin with an End, or use the
// RAII scope / macro.

namespace Ag
{
	void GpuMarkerBegin(const char* aName);
	void GpuMarkerEnd();

	struct GpuMarkerScope
	{
		explicit GpuMarkerScope(const char* aName) { GpuMarkerBegin(aName); }
		~GpuMarkerScope() { GpuMarkerEnd(); }
		GpuMarkerScope(const GpuMarkerScope&) = delete;
		GpuMarkerScope& operator=(const GpuMarkerScope&) = delete;
	};
}

#define AG_GPU_MARKER_CAT2(a, b) a##b
#define AG_GPU_MARKER_CAT(a, b) AG_GPU_MARKER_CAT2(a, b)
#define AG_GPU_MARKER(name) Ag::GpuMarkerScope AG_GPU_MARKER_CAT(_gpuMarker_, __LINE__)(name)
