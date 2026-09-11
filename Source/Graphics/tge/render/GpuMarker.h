#pragma once

// Lightweight GPU debug-event markers (PIX / RenderDoc / Nsight). No-ops when no
// annotation interface is available. Pair every Begin with an End, or use the
// RAII scope / macro.

namespace Tga
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

#define TGA_GPU_MARKER_CAT2(a, b) a##b
#define TGA_GPU_MARKER_CAT(a, b) TGA_GPU_MARKER_CAT2(a, b)
#define TGA_GPU_MARKER(name) Tga::GpuMarkerScope TGA_GPU_MARKER_CAT(_gpuMarker_, __LINE__)(name)
