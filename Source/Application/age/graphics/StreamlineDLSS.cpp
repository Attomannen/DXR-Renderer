#include "stdafx.h"
#include "age/graphics/StreamlineDLSS.h"
#include "../../../../ThirdParty/Dependencies/StreamlineSdk/include/sl_helpers.h"
#include <age/log/Log.h>
#include <windows.h>
#include <d3d12.h>
#include <cstring>

namespace
{
	template <class T>
	T GetSlProc(HMODULE aModule, const char* aName)
	{
		return reinterpret_cast<T>(GetProcAddress(aModule, aName));
	}

	bool IsOk(sl::Result aResult) { return aResult == sl::Result::eOk; }
}

namespace Ag
{
	StreamlineDLSS& StreamlineDLSS::Get()
	{
		static StreamlineDLSS instance;
		return instance;
	}

	void StreamlineDLSS::Initialize()
	{
		if (myInitialized) return;
		myInitialized = true;
		// slInit + slSetD3DDevice cost ~4 s of startup (plugin signature checks,
		// NGX bring-up). AGE_STREAMLINE=0 skips them when DLSS isn't needed.
		if (const char* env = std::getenv("AGE_STREAMLINE"); env && env[0] == '0')
		{
			INFO_PRINT("Streamline: disabled by AGE_STREAMLINE=0; using native temporal resolve");
			return;
		}

		wchar_t modulePath[MAX_PATH] = {};
		if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) return;
		wchar_t* slash = wcsrchr(modulePath, L'\\');
		if (!slash) return;
		*(slash + 1) = L'\0';
		wchar_t pluginDirectory[MAX_PATH] = {};
		wcscpy_s(pluginDirectory, modulePath);
		wcscat_s(modulePath, L"sl.interposer.dll");
		HMODULE module = LoadLibraryW(modulePath);
		if (!module)
		{
			INFO_PRINT("Streamline DLAA: runtime not found; using native temporal resolve");
			return;
		}

		myInit = GetSlProc<PFun_slInit*>(module, "slInit");
		myShutdown = GetSlProc<PFun_slShutdown*>(module, "slShutdown");
		mySetD3DDevice = GetSlProc<PFun_slSetD3DDevice*>(module, "slSetD3DDevice");
		myGetFeatureFunction = GetSlProc<PFun_slGetFeatureFunction*>(module, "slGetFeatureFunction");
		myGetNewFrameToken = GetSlProc<PFun_slGetNewFrameToken*>(module, "slGetNewFrameToken");
		mySetConstants = GetSlProc<PFun_slSetConstants*>(module, "slSetConstants");
		mySetTagForFrame = GetSlProc<PFun_slSetTagForFrame*>(module, "slSetTagForFrame");
		myEvaluateFeature = GetSlProc<PFun_slEvaluateFeature*>(module, "slEvaluateFeature");
		if (!myInit || !myShutdown || !mySetD3DDevice || !myGetFeatureFunction ||
			!myGetNewFrameToken || !mySetConstants || !mySetTagForFrame || !myEvaluateFeature)
		{
			ERROR_PRINT("Streamline DLAA: incomplete interposer API; using native temporal resolve");
			FreeLibrary(module);
			return;
		}

		const sl::Feature features[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_RR };
		const wchar_t* pluginPaths[] = { pluginDirectory };
		sl::Preferences pref{};
		pref.featuresToLoad = features;
		pref.numFeaturesToLoad = _countof(features);
		pref.pathsToPlugins = pluginPaths;
		pref.numPathsToPlugins = 1;
		pref.engine = sl::EngineType::eCustom;
		pref.engineVersion = "P5G3";
		// NGX uses the project-identity path when no NVIDIA production
		// application id is available.  Supplying only engineVersion leaves SL
		// on its temporary app id, which makes the DLSS plugin report the adapter
		// as unsupported even on an RTX-capable device. Replace this development
		// GUID with the NVIDIA-assigned application id for a shipping build.
		pref.projectId = "7c8f4f2d-5c0e-4db2-9f06-2b9ea4d4c6a1";
		pref.renderAPI = sl::RenderAPI::eD3D12;
		pref.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		const sl::Result initResult = myInit(pref, sl::kSDKVersion);
		if (!IsOk(initResult))
		{
			ERROR_PRINT("Streamline DLAA: slInit failed (%s); using native temporal resolve", sl::getResultAsStr(initResult));
			FreeLibrary(module);
			return;
		}
		myModule = module;
		INFO_PRINT("Streamline DLAA: initialized; waiting for a DX12 device");
	}

	void StreamlineDLSS::Shutdown()
	{
		if (myModule && myShutdown) myShutdown();
		if (myModule) FreeLibrary(static_cast<HMODULE>(myModule));
		myModule = nullptr;
		myAvailable = false;
		myRayReconstructionAvailable = false;
		myInitialized = false;
	}

	bool StreamlineDLSS::AttachD3D12Device(void* aDevice)
	{
		if (!myModule || !aDevice) return false;
		const sl::Result deviceResult = mySetD3DDevice(aDevice);
		if (!IsOk(deviceResult))
		{
			ERROR_PRINT("Streamline DLAA: slSetD3DDevice failed (%s)", sl::getResultAsStr(deviceResult));
			return false;
		}
		void* setOptions = nullptr;
		const sl::Result featureResult = myGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", setOptions);
		if (!IsOk(featureResult) || !setOptions)
		{
			INFO_PRINT("Streamline DLAA: DLSS unavailable (%s); using native temporal resolve", sl::getResultAsStr(featureResult));
			return false;
		}
		mySetOptions = reinterpret_cast<PFun_slDLSSSetOptions*>(setOptions);
		void* rrSetOptions = nullptr;
		if (IsOk(myGetFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", rrSetOptions)) && rrSetOptions)
		{
			mySetRayReconstructionOptions = reinterpret_cast<PFun_slDLSSDSetOptions*>(rrSetOptions);
			myRayReconstructionAvailable = true;
			INFO_PRINT("Streamline DLSS: Ray Reconstruction denoiser available");
		}
		myAvailable = true;
		INFO_PRINT("Streamline DLAA: DLSS feature available");
		return true;
	}

	bool StreamlineDLSS::EvaluateDLSS(void* aCommandList, void* aColor, void* aOutput,
		void* aDepth, void* aMotion, uint32_t aRenderWidth, uint32_t aRenderHeight,
		uint32_t aOutputWidth, uint32_t aOutputHeight, int aMode,
		uint32_t aFrameIndex, const float* aViewToClip, const float* aClipToView,
		const float* aClipToPreviousClip, const float* aPreviousClipToClip,
		const float* aCameraTransform, float aNearPlane, float aFarPlane,
		float aJitterX, float aJitterY, bool aReset)
	{
		if (!myAvailable || !aCommandList || !aColor || !aOutput || !aDepth || !aMotion) return false;
		const sl::ViewportHandle viewport(0);
		sl::FrameToken* token = nullptr;
		if (!IsOk(myGetNewFrameToken(token, &aFrameIndex)) || !token) return false;

		sl::DLSSOptions options{};
		switch (aMode) {
		case 2: options.mode = sl::DLSSMode::eMaxQuality; break;
		case 3: options.mode = sl::DLSSMode::eBalanced; break;
		case 4: options.mode = sl::DLSSMode::eMaxPerformance; break;
		case 5: options.mode = sl::DLSSMode::eUltraPerformance; break;
		default: options.mode = sl::DLSSMode::eDLAA; break;
		}
		options.outputWidth = aOutputWidth;
		options.outputHeight = aOutputHeight;
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.alphaUpscalingEnabled = sl::Boolean::eTrue;
		// The engine tags no exposure buffer and passes no preExposure, so
		// without this DLSS assumes an exposure of 1.0 over a buffer carrying
		// raw scene-unit radiance. Measured on the Bistro at DLSS Performance,
		// letting DLSS meter the input itself halved the number of pixels
		// changing by more than 30/255 between consecutive frames on a
		// completely static camera, from 90 to 44.
		options.useAutoExposure = sl::Boolean::eTrue;
		if (myPreset != 0)
		{
			// J, K, L, M: the model presets this Streamline version still offers.
			static constexpr sl::DLSSPreset kPresets[] = { sl::DLSSPreset::eDefault, sl::DLSSPreset::ePresetJ,
				sl::DLSSPreset::ePresetK, sl::DLSSPreset::ePresetL, sl::DLSSPreset::ePresetM };
			const sl::DLSSPreset preset = kPresets[std::clamp(myPreset, 0, 4)];
			options.dlaaPreset = options.qualityPreset = options.balancedPreset = options.performancePreset = options.ultraPerformancePreset = preset;
		}
		if (!IsOk(mySetOptions(viewport, options))) return false;

		sl::Constants constants{};
		std::memcpy(&constants.cameraViewToClip, aViewToClip, sizeof(constants.cameraViewToClip));
		std::memcpy(&constants.clipToCameraView, aClipToView, sizeof(constants.clipToCameraView));
		std::memcpy(&constants.clipToPrevClip, aClipToPreviousClip, sizeof(constants.clipToPrevClip));
		std::memcpy(&constants.prevClipToClip, aPreviousClipToClip, sizeof(constants.prevClipToClip));
		// The engine's jitter is the sample's offset from the pixel centre
		// (uv = pixel + 0.5 + jitter); DLSS wants the opposite sign. Re-measured
		// against both alternatives on a static camera at DLSS Quality, counting
		// pixels that change by more than 30/255 between consecutive frames:
		// this sign 56 per frame, the opposite sign 1185, a zero jitter 90 but
		// with visibly less reconstructed detail. The sign below is correct and
		// does not need testing again.
		// Motion vectors are (previous - current) in render-resolution pixels,
		// which is what Streamline, NRD and the native TAA resolve all want --
		// mvecScale only converts pixels to NDC. Negating it was tried and is
		// wrong: it makes DLSS reject history, which raises an edge-detail
		// metric (the image is per-frame sharp) while visibly aliasing and
		// crawling on curtain edges and trim. Judge this by eye, not by a
		// sharpness number, which rewards history rejection.
		constants.jitterOffset = { -aJitterX, -aJitterY };
		constants.mvecScale = { 1.0f / float(aRenderWidth), 1.0f / float(aRenderHeight) };
		constants.cameraPos = { aCameraTransform[12], aCameraTransform[13], aCameraTransform[14] };
		constants.cameraRight = { aCameraTransform[0], aCameraTransform[1], aCameraTransform[2] };
		constants.cameraUp = { aCameraTransform[4], aCameraTransform[5], aCameraTransform[6] };
		constants.cameraFwd = { aCameraTransform[8], aCameraTransform[9], aCameraTransform[10] };
		constants.cameraNear = aNearPlane;
		constants.cameraFar = aFarPlane;
		constants.cameraAspectRatio = float(aOutputWidth) / float(aOutputHeight);
		constants.depthInverted = sl::Boolean::eFalse;
		constants.cameraMotionIncluded = sl::Boolean::eTrue;
		constants.motionVectors3D = sl::Boolean::eFalse;
		constants.motionVectorsDilated = sl::Boolean::eFalse;   // render-resolution, per-pixel
		constants.reset = aReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		if (!IsOk(mySetConstants(constants, *token, viewport))) return false;

		sl::Resource color(sl::ResourceType::eTex2d, aColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource depth(sl::ResourceType::eTex2d, aDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource motion(sl::ResourceType::eTex2d, aMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource output(sl::ResourceType::eTex2d, aOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		sl::ResourceTag tags[] = {
			{ &color, sl::kBufferTypeScalingInputColor, sl::eValidUntilEvaluate },
			{ &depth, sl::kBufferTypeDepth, sl::eValidUntilEvaluate },
			{ &motion, sl::kBufferTypeMotionVectors, sl::eValidUntilEvaluate },
			{ &output, sl::kBufferTypeScalingOutputColor, sl::eValidUntilEvaluate },
		};
		if (!IsOk(mySetTagForFrame(*token, viewport, tags, _countof(tags), static_cast<sl::CommandBuffer*>(aCommandList)))) return false;
		const sl::BaseStructure* inputs[] = { &viewport };
		return IsOk(myEvaluateFeature(sl::kFeatureDLSS, *token, inputs, _countof(inputs), static_cast<sl::CommandBuffer*>(aCommandList)));
	}

	bool StreamlineDLSS::EvaluateRayReconstruction(void* aCommandList, void* aColor, void* aOutput,
		void* aDepth, void* aMotion, void* aNormalRoughness, void* aDiffuseAlbedo, void* aSpecularAlbedo,
		uint32_t aRenderWidth, uint32_t aRenderHeight, uint32_t aOutputWidth, uint32_t aOutputHeight,
		int aMode, uint32_t aFrameIndex, const float* aViewToClip,
		const float* aClipToView, const float* aWorldToView, const float* aViewToWorld,
		const float* aClipToPreviousClip, const float* aPreviousClipToClip, const float* aCameraTransform,
		float aNearPlane, float aFarPlane, float aJitterX, float aJitterY, bool aReset)
	{
		if (!myRayReconstructionAvailable || !aCommandList || !aColor || !aOutput || !aDepth || !aMotion || !aNormalRoughness || !aDiffuseAlbedo || !aSpecularAlbedo) return false;
		const sl::ViewportHandle viewport(0);
		sl::FrameToken* token = nullptr;
		if (!IsOk(myGetNewFrameToken(token, &aFrameIndex)) || !token) return false;
		sl::DLSSDOptions options{};
		// Ray Reconstruction replaces the denoiser AND the upscaler, so it runs
		// at whichever DLSS quality mode is selected: eDLAA is simply the 1:1
		// case, not a requirement.
		switch (aMode) {
		case 2: options.mode = sl::DLSSMode::eMaxQuality; break;
		case 3: options.mode = sl::DLSSMode::eBalanced; break;
		case 4: options.mode = sl::DLSSMode::eMaxPerformance; break;
		case 5: options.mode = sl::DLSSMode::eUltraPerformance; break;
		default: options.mode = sl::DLSSMode::eDLAA; break;
		}
		options.outputWidth = aOutputWidth; options.outputHeight = aOutputHeight;
		options.colorBuffersHDR = sl::Boolean::eTrue;
		options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
		options.alphaUpscalingEnabled = sl::Boolean::eFalse;
		std::memcpy(&options.worldToCameraView, aWorldToView, sizeof(options.worldToCameraView));
		std::memcpy(&options.cameraViewToWorld, aViewToWorld, sizeof(options.cameraViewToWorld));
		if (!IsOk(mySetRayReconstructionOptions(viewport, options))) return false;
		sl::Constants c{};
		std::memcpy(&c.cameraViewToClip, aViewToClip, sizeof(c.cameraViewToClip));
		std::memcpy(&c.clipToCameraView, aClipToView, sizeof(c.clipToCameraView));
		std::memcpy(&c.clipToPrevClip, aClipToPreviousClip, sizeof(c.clipToPrevClip));
		std::memcpy(&c.prevClipToClip, aPreviousClipToClip, sizeof(c.prevClipToClip));
		// Jitter sign: see EvaluateDLSS.
		// Motion vectors are written in render-resolution pixels.
		// Jitter and motion-vector sign: see EvaluateDLSS.
		c.jitterOffset = { -aJitterX, -aJitterY }; c.mvecScale = {1.f / float(aRenderWidth), 1.f / float(aRenderHeight)};
		c.cameraPos = {aCameraTransform[12], aCameraTransform[13], aCameraTransform[14]};
		c.cameraRight = {aCameraTransform[0], aCameraTransform[1], aCameraTransform[2]};
		c.cameraUp = {aCameraTransform[4], aCameraTransform[5], aCameraTransform[6]};
		c.cameraFwd = {aCameraTransform[8], aCameraTransform[9], aCameraTransform[10]};
		c.cameraNear = aNearPlane; c.cameraFar = aFarPlane; c.cameraAspectRatio = float(aOutputWidth) / float(aOutputHeight);
		c.depthInverted = sl::Boolean::eFalse; c.cameraMotionIncluded = sl::Boolean::eTrue;
		c.motionVectors3D = sl::Boolean::eFalse; c.motionVectorsDilated = sl::Boolean::eFalse;
		c.reset = aReset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		if (!IsOk(mySetConstants(c, *token, viewport))) return false;
		sl::Resource color(sl::ResourceType::eTex2d, aColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource depth(sl::ResourceType::eTex2d, aDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource motion(sl::ResourceType::eTex2d, aMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource normal(sl::ResourceType::eTex2d, aNormalRoughness, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource diffuse(sl::ResourceType::eTex2d, aDiffuseAlbedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource specular(sl::ResourceType::eTex2d, aSpecularAlbedo, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		sl::Resource output(sl::ResourceType::eTex2d, aOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		sl::ResourceTag tags[] = {{&color, sl::kBufferTypeScalingInputColor, sl::eValidUntilEvaluate}, {&depth, sl::kBufferTypeDepth, sl::eValidUntilEvaluate}, {&motion, sl::kBufferTypeMotionVectors, sl::eValidUntilEvaluate}, {&normal, sl::kBufferTypeNormalRoughness, sl::eValidUntilEvaluate}, {&diffuse, sl::kBufferTypeAlbedo, sl::eValidUntilEvaluate}, {&specular, sl::kBufferTypeSpecularAlbedo, sl::eValidUntilEvaluate}, {&output, sl::kBufferTypeScalingOutputColor, sl::eValidUntilEvaluate}};
		if (!IsOk(mySetTagForFrame(*token, viewport, tags, _countof(tags), static_cast<sl::CommandBuffer*>(aCommandList)))) return false;
		const sl::BaseStructure* inputs[] = {&viewport};
		return IsOk(myEvaluateFeature(sl::kFeatureDLSS_RR, *token, inputs, _countof(inputs), static_cast<sl::CommandBuffer*>(aCommandList)));
	}
}
