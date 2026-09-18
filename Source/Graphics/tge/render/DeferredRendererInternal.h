#pragma once

// Private to the DeferredRenderer implementation files: shared includes,
// constant-buffer layouts and small helpers. Not part of the public API.

#include <tge/graphics/StreamlineDLSS.h>
#include <tge/render/DeferredRenderer.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <tge/application.h>
#include <tge/graphics/DX11.h>
#include <tge/graphics/DepthBuffer.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/graphics/FullscreenEffect.h>
#include "tge/rhi/dx12/Dx12Device.h"
#include "tge/rhi/dx12/Dx12CommandContext.h"
#include "tge/render/NrdWrapper.h"
#include <tge/math/Photometry.h>
#include <tge/render/GpuProfiler.h>
#include "../../../../EngineAssets/Shaders/DxrLightingConstants.hlsli"
#include "../../../../EngineAssets/Shaders/SkyAtmosphereConstants.hlsli"
#include "../../../../EngineAssets/Shaders/CloudsConstants.hlsli"
#include <tge/graphics/Camera.h>
#include <tge/shaders/ModelShader.h>
#include <tge/render/RenderGraph.h>
#include <tge/render/RenderCommon.h>
#include <tge/render/RayTracingMaterialTable.h>
#include <tge/log/Log.h>

namespace
{
	// CS b0 / PS b7. Layout must match ClusterParams in ClusterCullCS.hlsl.
	struct ClusterCb
	{
		float projToView[16];
		float worldToView[16];
		uint32_t tileCountX, tileCountY, tilePx, zSlices;
		float nearP, farP;
		uint32_t lightCount, maxPerCluster;
		float screenX, screenY, pad0, pad1;
	};

	// b8. Layout must match SsaoParams in SSAOPS.hlsl.
	struct SsaoCb
	{
		float projToView[16];
		float viewToProj[16];
		float worldToView[16];
		float screenX, screenY, radius, bias;
		float intensity, power, pad0, pad1;
	};
	// b8. Layout must match SsrParams in SSRPS.hlsl.
	struct SsrCb
	{
		float viewToProj[16];
		float projToView[16];
		float worldToView[16];
		float screenX, screenY, maxDistance, thickness;
		float roughnessCutoff, strength;
		int32_t steps, refineSteps;
	};
	// b8. Layout must match SsaoBlurParams in SSAOBlurPS.hlsl.
	struct SsaoBlurCb
	{
		float texelX, texelY, depthSigma, pad0;
	};

	// b9. Layout must match ShadowParams in DeferredLightingPS.hlsl.
	struct ShadowCb
	{
		float cascadeViewProj[Tga::DeferredRenderer::kNumCascades][16];
		float cascadeSplits[4];
		float cascadeTexelWorld[4];
		float cascadeDepthRange[4];   // world units spanning NDC z 0..1 for this cascade
		float texel, depthBias, strength, enabled;
		float normalOffset, showCascades, contactLength, contactThickness;
		float dxrSunShadows, dxrSunShadowDebug, pad1, pad2;
	};
	struct DxrShadowCb { float origin[3], tanHalfFovY; float right[3], aspect; float up[3], pad0; float forward[3], pad1; float sunDir[3], bias; uint32_t sizeX,sizeY; float pad[2]; };

	// t20. Layout must match LocalShadow in DeferredLightingPS.hlsl.
	struct LocalShadowGpu
	{
		float viewProj[16];
		float tile[4];   // uv min x,y ; uv extent x,y
		float misc[4];   // x = valid(1/0), y = type (0 spot / 1 point face), z = NDC depth bias, w = pad
	};

	inline Tga::Matrix4x4f MakeLookTransform(const Tga::Vector3f& pos, Tga::Vector3f fwd)
	{
		float l = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
		if (l > 1e-5f) fwd = { fwd.x/l, fwd.y/l, fwd.z/l };
		Tga::Vector3f up = (std::abs(fwd.y) > 0.99f) ? Tga::Vector3f{ 0,0,1 } : Tga::Vector3f{ 0,1,0 };
		Tga::Vector3f right{ up.y*fwd.z - up.z*fwd.y, up.z*fwd.x - up.x*fwd.z, up.x*fwd.y - up.y*fwd.x };
		l = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
		if (l > 1e-5f) right = { right.x/l, right.y/l, right.z/l };
		up = { fwd.y*right.z - fwd.z*right.y, fwd.z*right.x - fwd.x*right.z, fwd.x*right.y - fwd.y*right.x };
		Tga::Matrix4x4f m = Tga::Matrix4x4f::CreateIdentityMatrix();
		m(1,1)=right.x; m(1,2)=right.y; m(1,3)=right.z;
		m(2,1)=up.x;    m(2,2)=up.y;    m(2,3)=up.z;
		m(3,1)=fwd.x;   m(3,2)=fwd.y;   m(3,3)=fwd.z;
		m.SetPosition(pos);
		return m;
	}

	// NRD's internal thresholds (plane distance, blur radii, hit distances)
	// assume metres; the engine's world unit is the centimetre. Everything NRD
	// sees -- view matrices, viewZ, hit distances -- is converted.
	constexpr float kNrdMetersPerUnit = 0.01f;
	constexpr float kNrdHitDistanceA = 3.0f;   // REBLUR default, metres

	struct alignas(16) AtmosphereCb
	{
		float clipToWorld[16];
		float camera[3], density;
		float sunDirection[3], heightFalloff;
		float sunRadiance[3], baseHeight;
		float fogColor[3], startDistance;
		float maxDistance, volumeDistance, volumeStrength, anisotropy;
		uint32_t width, height, steps, affectSky;
		uint32_t volumeEnabled, debugView; float jitter[2];
		float sunDiskAngularRadius, sunDiskIntensity; uint32_t sunDiskEnabled; float preExposed;
	};
	static_assert(sizeof(AtmosphereCb) == 192);

	// b10. Layout must match PostFxParams in PostFxCommon.hlsli.
	struct PostFxCb
	{
		float texelSize[2];
		float bloomThreshold;
		float bloomKnee;

		float bloomIntensity;
		float manualEv100;
		float autoEvMin;
		float autoEvMax;

		float exposureAuto;
		float exposureComp;
		float adaptRate;
		float deltaTime;

		uint32_t tonemapper;
		uint32_t hdrPreExposed;
		float pad1[2];
	};
	static_assert(sizeof(PostFxCb) == 64);

	// CS b0 of the DXR lighting pass; the layout lives in the shared header.
	using DxrLightingConstants = Tga::DxrShared::DxrLightingConstants;
	static_assert(offsetof(DxrLightingConstants, gEnvironmentTint) == 128);
	static_assert(offsetof(DxrLightingConstants, gEnvironmentMip) == 140);
	static_assert(offsetof(DxrLightingConstants, gEnableDirectLighting) == 152);
	static_assert(offsetof(DxrLightingConstants, gEnableReflections) == 164);
	static_assert(offsetof(DxrLightingConstants, gEnableAmbientOcclusion) == 168);
	static_assert(offsetof(DxrLightingConstants, gBrdfLutValid) == 176);
	static_assert(offsetof(DxrLightingConstants, gWorldToClip) == 208);
	static_assert(offsetof(DxrLightingConstants, gSpecularAaStrength) == 340);
	static_assert(sizeof(DxrLightingConstants) == 448);   // +16 emissive light sampling, +64 gWorldToView
	struct alignas(16) TaaCb
	{
		uint32_t width, height, historyValid, debugView;
		float jitter[2], previousJitter[2];
		float nearPlane, farPlane, historyWeight; uint32_t stationaryCoverage;
	};
	static_assert(sizeof(TaaCb) == 48);

	// CS b0. Layout must match GiTraceParams in GiTraceInlineCS.hlsl.
	// Everything here is shared by every probe in one GiProjectProbeBatchRT
	// dispatch. The per-probe position/index that used to lead this struct now
	// live in the t6 batch buffer, indexed by SV_GroupID -- keep this in sync
	// with GiTraceInlineCS.hlsl's GiTraceParams.
	struct GiTraceCb
	{
		float hysteresis; uint32_t rayCount; uint32_t lightCount; uint32_t textureFiltering;
		float sunDirToLight[3]; float specularAaStrength;
		float sunRadiance[3]; float ambientIntensity;
		float environmentTint[3]; float environmentDiffuseMip;
		uint32_t sampleSequence; float fireflyClamp; float infiniteBounce; float _giTracePad;
	};
	static_assert(offsetof(GiTraceCb, textureFiltering) == 12);
	static_assert(offsetof(GiTraceCb, specularAaStrength) == 28);
	static_assert(sizeof(GiTraceCb) == 80);

	// b11, shared by every sky-LUT compute pass; the layout lives in the
	// shared header so the C++ side and all four consuming shaders agree.
	using SkyAtmosphereConstants = Tga::SkyShared::SkyAtmosphereConstants;
	static_assert(offsetof(SkyAtmosphereConstants, gRayleighScattering) == 32);
	static_assert(offsetof(SkyAtmosphereConstants, gOzoneAbsorption) == 80);
	static_assert(offsetof(SkyAtmosphereConstants, gSkyViewLutWidth) == 112);
	static_assert(sizeof(SkyAtmosphereConstants) == 128);

	// b12. Layout must match SkyCubemapFaceCb in SkyCubemapPS.hlsl.
	struct alignas(16) SkyCubemapFaceCb { uint32_t faceIndex; float nightSkyIntensity; float pad[2]; };
	static_assert(sizeof(SkyCubemapFaceCb) == 16);

	// b13, shared by the cloud noise bake, the hero raymarch, the sky
	// cubemap's coarse cloud contribution and the fog-shaft cloud-shadow
	// lookup -- same shared-header pattern as SkyAtmosphereConstants above.
	using CloudsConstants = Tga::CloudsShared::CloudsConstants;
	static_assert(offsetof(CloudsConstants, gCloudTopAltitude) == 16);
	static_assert(offsetof(CloudsConstants, gTime) == 32);
	static_assert(offsetof(CloudsConstants, gCloudPrevWorldToClip) == 48);
	static_assert(offsetof(CloudsConstants, gCloudCamRight) == 112);
	static_assert(sizeof(CloudsConstants) == 160);

	inline Tga::Vector3f Lerp(const Tga::Vector3f& a, const Tga::Vector3f& b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	// Radical inverse in `base` -- the standard Halton low-discrepancy sequence,
	// which is what both DLSS and the native temporal resolve expect to be
	// driving the sub-pixel sample offset. Index is 1-based; index 0 returns 0
	// for every base, which would waste a phase on the un-jittered grid.
	inline float RadicalInverse(uint32_t aIndex, uint32_t aBase)
	{
		float result = 0.f, fraction = 1.f;
		for (uint32_t i = aIndex; i > 0u; i /= aBase)
		{
			fraction /= float(aBase);
			result += fraction * float(i % aBase);
		}
		return result;
	}

	// Bind up to 8 render targets (+ optional depth) and a 0,0-origin viewport in
	// one call -- collapses the repeated OMSetRenderTargets + D3D11_VIEWPORT +
	// RSSetViewports triple.
	inline void SetTargets(Tga::rhi::ICommandContext& aCtx,
	                std::initializer_list<Tga::rhi::RtvHandle> aRtvs,
	                Tga::rhi::DsvHandle aDsv, Tga::Vector2ui aSize)
	{
		Tga::rhi::RtvHandle arr[8] = {};
		uint32_t n = 0;
		for (Tga::rhi::RtvHandle h : aRtvs) if (n < 8) arr[n++] = h;
		aCtx.SetRenderTargets(n, arr, aDsv);
		aCtx.SetViewport(0.f, 0.f, (float)aSize.x, (float)aSize.y);
	}
}
