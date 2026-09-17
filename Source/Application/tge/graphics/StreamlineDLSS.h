#pragma once

// Runtime-loaded Streamline bridge.  Keeping this dynamically linked avoids
// making the engine depend on DLSS on non-NVIDIA machines.
#include "../../../../Dependencies/StreamlineSdk/include/sl.h"
#include "../../../../Dependencies/StreamlineSdk/include/sl_dlss.h"
#include "../../../../Dependencies/StreamlineSdk/include/sl_dlss_d.h"

namespace Tga
{
	class StreamlineDLSS final
	{
	public:
		static StreamlineDLSS& Get();

		// Must run before the DXGI/D3D12 bootstrap.  Missing or unsupported
		// vendor DLLs leave the renderer on its native temporal resolve.
		void Initialize();
		void Shutdown();
		bool AttachD3D12Device(void* aDevice);
		bool IsAvailable() const { return myAvailable; }
		bool IsRayReconstructionAvailable() const { return myRayReconstructionAvailable; }

		// aMode: 1=DLAA, 2=Quality, 3=Balanced, 4=Performance, 5=Ultra Performance.
		// 0 default, 1 J, 2 K, 3 L, 4 M (see sl::DLSSPreset).
		void SetPreset(int aPreset) { myPreset = aPreset; }
		bool EvaluateDLSS(void* aCommandList, void* aColor, void* aOutput,
			void* aDepth, void* aMotion, uint32_t aRenderWidth, uint32_t aRenderHeight,
			uint32_t aOutputWidth, uint32_t aOutputHeight, int aMode,
			uint32_t aFrameIndex, const float* aViewToClip,
			const float* aClipToView, const float* aClipToPreviousClip,
			const float* aPreviousClipToClip, const float* aCameraTransform,
			float aNearPlane, float aFarPlane, float aJitterX, float aJitterY,
			bool aReset);
		bool EvaluateRayReconstruction(void* aCommandList, void* aColor, void* aOutput,
			void* aDepth, void* aMotion, void* aNormalRoughness, void* aDiffuseAlbedo,
			void* aSpecularAlbedo, uint32_t aWidth, uint32_t aHeight, uint32_t aFrameIndex,
			const float* aViewToClip, const float* aClipToView, const float* aWorldToView,
			const float* aViewToWorld, const float* aClipToPreviousClip,
			const float* aPreviousClipToClip, const float* aCameraTransform,
			float aNearPlane, float aFarPlane, float aJitterX, float aJitterY, bool aReset);

	private:
		StreamlineDLSS() = default;
		~StreamlineDLSS() = default;
		StreamlineDLSS(const StreamlineDLSS&) = delete;
		StreamlineDLSS& operator=(const StreamlineDLSS&) = delete;

		void* myModule = nullptr;
		bool myInitialized = false;
		bool myAvailable = false;
		bool myRayReconstructionAvailable = false;
		int myPreset = 0;
		PFun_slInit* myInit = nullptr;
		PFun_slShutdown* myShutdown = nullptr;
		PFun_slSetD3DDevice* mySetD3DDevice = nullptr;
		PFun_slGetFeatureFunction* myGetFeatureFunction = nullptr;
		PFun_slGetNewFrameToken* myGetNewFrameToken = nullptr;
		PFun_slSetConstants* mySetConstants = nullptr;
		PFun_slSetTagForFrame* mySetTagForFrame = nullptr;
		PFun_slEvaluateFeature* myEvaluateFeature = nullptr;
		PFun_slDLSSSetOptions* mySetOptions = nullptr;
		PFun_slDLSSDSetOptions* mySetRayReconstructionOptions = nullptr;
	};
}
