#include "stdafx.h"
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

	Tga::Matrix4x4f MakeLookTransform(const Tga::Vector3f& pos, Tga::Vector3f fwd)
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
		float sunDiskAngularRadius, sunDiskIntensity; uint32_t sunDiskEnabled; float pad;
	};
	static_assert(sizeof(AtmosphereCb) == 192);

	// b10. Layout must match PostFxParams in PostFxCommon.hlsli.
	struct PostFxCb
	{
		float texelSize[2];
		float bloomThreshold;
		float bloomKnee;

		float bloomIntensity;
		float exposureKeyOrManual;
		float exposureMin;
		float exposureMax;

		float exposureAuto;
		float exposureComp;
		float adaptRate;
		float deltaTime;
	};

	// CS b0. Layout must match SmokeTestCB in DxrSmokeTestCS.hlsl.
	struct alignas(16) DxrSmokeCb
	{
		float origin[3];  float tanHalfFovY;
		float right[3];   float aspect;
		float up[3];      float pad0;
		float forward[3]; float pad1;
		uint32_t sizeX, sizeY;
		float pad2[2];
		float sunDirToLight[3]; float pad3;   // normalized, surface -> light
		uint32_t lightCount; float sunRadiance[3];
		float ambientIntensity; float reflectionRoughnessCutoff;
		// HLSL cannot fit the following float3 into this row's two remaining
		// components. Match its next-row alignment explicitly.
		float pad4[2];
		float environmentTint[3]; float environmentMip;
		float aoDistance; float aoStrength;
		uint32_t enableDirectLighting, enableEnvironmentLighting, enableIndirectGi, enableReflections;
		uint32_t enableAmbientOcclusion, pad6, pad5[3];
		uint32_t pad7;
		uint32_t reflectionSamples;
		// Claimed out of the old reflectionPad[3], so the whole layout stays
		// byte-identical and every offset assert below still holds.
		uint32_t aoSamples;
		uint32_t reflectionPad[2];
		float worldToClip[16], previousWorldToClip[16];
		uint32_t temporalHistoryValid; float specularAaStrength; uint32_t reflectionFrameIndex, nrdEnabled;
		float jitter[2], previousJitter[2];
	};
	static_assert(offsetof(DxrSmokeCb, environmentTint) == 128);
	static_assert(offsetof(DxrSmokeCb, environmentMip) == 140);
	static_assert(offsetof(DxrSmokeCb, enableDirectLighting) == 152);
	static_assert(offsetof(DxrSmokeCb, enableReflections) == 164);
	static_assert(offsetof(DxrSmokeCb, enableAmbientOcclusion) == 168);
	static_assert(offsetof(DxrSmokeCb, pad5) == 176);
	static_assert(offsetof(DxrSmokeCb, worldToClip) == 208);
	static_assert(offsetof(DxrSmokeCb, specularAaStrength) == 340);
	static_assert(sizeof(DxrSmokeCb) == 368);
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

	Tga::Vector3f Lerp(const Tga::Vector3f& a, const Tga::Vector3f& b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	// Radical inverse in `base` -- the standard Halton low-discrepancy sequence,
	// which is what both DLSS and the native temporal resolve expect to be
	// driving the sub-pixel sample offset. Index is 1-based; index 0 returns 0
	// for every base, which would waste a phase on the un-jittered grid.
	float RadicalInverse(uint32_t aIndex, uint32_t aBase)
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
	void SetTargets(Tga::rhi::ICommandContext& aCtx,
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

using namespace Tga;

DeferredRenderer::DeferredRenderer() = default;
DeferredRenderer::~DeferredRenderer() = default;

bool DeferredRenderer::CreateTargets(Vector2ui aResolution)
{
	myResolution = aResolution;
	myAlbedo   = RenderTarget::Create(aResolution, rhi::Format::R8G8B8A8_Typeless, rhi::Format::R8G8B8A8_UNorm_sRGB, rhi::Format::R8G8B8A8_UNorm_sRGB);
	myNormal   = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	myMaterial = RenderTarget::Create(aResolution, rhi::Format::R8G8B8A8_UNorm);
	myEmissive = RenderTarget::Create(aResolution, rhi::Format::R11G11B10_Float);
	myHdr      = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	myOpaqueHdr = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	myAoRaw    = RenderTarget::Create(aResolution, rhi::Format::R8_UNorm);
	myAo       = RenderTarget::Create(aResolution, rhi::Format::R8_UNorm);
	// SSR marches + stores at half res; the resolve bilinearly upsamples it.
	mySsrRes   = { std::max(1u, aResolution.x / 2u), std::max(1u, aResolution.y / 2u) };
	mySsrTex    = RenderTarget::Create(mySsrRes, rhi::Format::R16G16B16A16_Float);
	myIblSpecTex = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	return true;
}

bool DeferredRenderer::Init(Vector2ui aResolution)
{
	CreateTargets(aResolution);

	myGeometryShader = std::make_unique<ModelShader>();
	if (!myGeometryShader->Init("Shaders/PbrModelShaderVS", "Shaders/GBufferPS"))
	{
		ERROR_PRINT("DeferredRenderer: failed to build geometry shader");
		return false;
	}

	myDebugMatShader = std::make_unique<ModelShader>();
	if (!myDebugMatShader->Init("Shaders/PbrModelShaderVS", "Shaders/GBufferDebugMatPS"))
	{
		ERROR_PRINT("DeferredRenderer: debug material shader failed; material preview disabled");
		myDebugMatShader.reset();
	}
	else
	{
		myDebugMatCb.Create(*DX11::Rhi(), 48, rhi::ShaderStage::Pixel, 11, "DebugMatCb");   // 3 x float4
		if (!myDebugMatCb.IsValid()) myDebugMatShader.reset();
	}

	myGlassShader = std::make_unique<ModelShader>();
	if (!myGlassShader->Init("Shaders/PbrModelShaderVS", "Shaders/GlassModelShaderPS"))
	{
		ERROR_PRINT("DeferredRenderer: glass shader failed; transparent meshes use the legacy forward shader");
		myGlassShader.reset();
	}
	myFullscreenVs = DX11::LoadVertexShader("Shaders/PostprocessVS");
	myLightingPs   = DX11::LoadPixelShader("Shaders/DeferredLightingPS");
	myDebugPs      = DX11::LoadPixelShader("Shaders/DeferredDebugPS");
	mySceneCopyPs  = DX11::LoadPixelShader("Shaders/PostprocessCopyPS");
	if (!myFullscreenVs || !myLightingPs || !myDebugPs || !mySceneCopyPs)
	{
		ERROR_PRINT("DeferredRenderer: failed to load fullscreen shaders");
		return false;
	}

	mySsaoPs     = DX11::LoadPixelShader("Shaders/SSAOPS");
	mySsaoBlurPs = DX11::LoadPixelShader("Shaders/SSAOBlurPS");
	if (!mySsaoPs || !mySsaoBlurPs)
		ERROR_PRINT("DeferredRenderer: SSAO shaders failed to load; SSAO disabled");
	else
	{
		mySsaoCb.Create(*DX11::Rhi(), sizeof(SsaoCb), rhi::ShaderStage::Pixel, 8, "SsaoCb");
		mySsaoBlurCb.Create(*DX11::Rhi(), 16, rhi::ShaderStage::Pixel, 8, "SsaoBlurCb");
		if (!mySsaoCb.IsValid() || !mySsaoBlurCb.IsValid()) { mySsaoPs = nullptr; mySsaoBlurPs = nullptr; }
	}

	mySsrPs      = DX11::LoadPixelShader("Shaders/SSRPS");
	mySsrApplyPs = DX11::LoadPixelShader("Shaders/SSRApplyPS");
	if (!mySsrPs || !mySsrApplyPs)
		ERROR_PRINT("DeferredRenderer: SSR shaders failed to load; SSR disabled");
	else
	{
		mySsrCb.Create(*DX11::Rhi(), sizeof(SsrCb), rhi::ShaderStage::Pixel, 8, "SsrCb");
		if (!mySsrCb.IsValid()) { mySsrPs = nullptr; mySsrApplyPs = nullptr; }
	}

	myProbeCb.Create(*DX11::Rhi(), 32, rhi::ShaderStage::Pixel, 12, "ReflectionProbeCb");   // 2 x float4

	// --- emissive-GI irradiance volume ---
	myGiProjectCS = DX11::LoadComputeShader("Shaders/GiProjectSHCS");
	if (!myGiProjectCS)
	{
		ERROR_PRINT("DeferredRenderer: GiProjectSHCS failed to load; emissive GI disabled");
	}
	else
	{
		myGiShBuffer.Create(*DX11::Rhi(), sizeof(float) * 4, 9 * kMaxGiProbes,
		                   /*withUav*/ true, /*cpuUpdatable*/ false, "GiShBuffer");
		myGiShPreviousBuffer.Create(*DX11::Rhi(), sizeof(float) * 4, 9 * kMaxGiProbes,
		                   /*withUav*/ true, /*cpuUpdatable*/ false, "GiShPreviousBuffer");
		myGiVisibilityBuffer.Create(*DX11::Rhi(), sizeof(float) * 4, 64 * kMaxGiProbes,
			/*withUav*/ true, /*cpuUpdatable*/ false, "GiVisibilityMoments");
		myGiVisibilityPreviousBuffer.Create(*DX11::Rhi(), sizeof(float) * 4, 64 * kMaxGiProbes,
			/*withUav*/ true, /*cpuUpdatable*/ false, "GiVisibilityPrevious");
		if (!myGiShBuffer.IsValid() || !myGiShPreviousBuffer.IsValid() || !myGiVisibilityPreviousBuffer.IsValid())
		{
			myGiProjectCS = nullptr;
		}
		else
		{
			myGiVolumeCb.Create(*DX11::Rhi(), 48, rhi::ShaderStage::Pixel, 13, "GiVolumeCb");   // 3 x float4
			myGiProjectCb.Create(*DX11::Rhi(), 16, rhi::ShaderStage::Compute, 0, "GiProjectCb");

			{
				rhi::SamplerDesc sd2;
				sd2.filter = rhi::FilterMode::Trilinear;
				sd2.address = rhi::AddressMode::Clamp;
				myGiLinearSampler = DX11::Rhi()->CreateSampler(sd2);
			}

			if (!myGiShBuffer.IsValid() || !myGiVisibilityBuffer.IsValid() || !myGiVolumeCb.IsValid() || !myGiProjectCb.IsValid() || !myGiLinearSampler.IsValid())
				myGiProjectCS = nullptr;
			else
				INFO_PRINT("DeferredRenderer: emissive-GI volume ready (max %d probes)", kMaxGiProbes);
		}
	}

	{
		rhi::SamplerDesc sd2;
		sd2.filter = rhi::FilterMode::Point;
		sd2.address = rhi::AddressMode::Clamp;
		myPointSampler = DX11::Rhi()->CreateSampler(sd2);
		if (!myPointSampler.IsValid())
		{
			ERROR_PRINT("DeferredRenderer: failed to create point sampler");
			return false;
		}
	}

	// --- DXR Stage-3: RayQuery smoke test (opt-in, DXR-1.1-only) ---
	if (DX11::Rhi()->SupportsRaytracingTier11())
	{
		myDxrShadowCS = DX11::LoadComputeShaderDxil("Shaders/DxrDirectionalShadowCS");
		if (myDxrShadowCS)
		{
			myDxrShadowCb.Create(*DX11::Rhi(), sizeof(DxrShadowCb), rhi::ShaderStage::Compute, 0, "DxrShadowCb");
			rhi::TextureDesc td{}; td.width=aResolution.x; td.height=aResolution.y; td.format=rhi::Format::R8_UNorm; td.bind=rhi::TextureBind::ShaderResource|rhi::TextureBind::UnorderedAccess; td.debugName="DxrSunShadow";
			myDxrShadowTex=DX11::Rhi()->CreateTexture(td); myDxrShadowSrv=DX11::Rhi()->CreateSrv(myDxrShadowTex,{}); myDxrShadowUav=DX11::Rhi()->CreateUav(myDxrShadowTex,{});
			if (!myDxrShadowCb.IsValid()||!myDxrShadowSrv.IsValid()||!myDxrShadowUav.IsValid()) myDxrShadowCS=nullptr;
		}
		myDxrSmokeCS = DX11::LoadComputeShaderDxil("Shaders/DxrSmokeTestCS");
		myDxrBrdfLutCS = DX11::LoadComputeShaderDxil("Shaders/DxrBrdfLutCS");
		if (!myDxrSmokeCS)
		{
			ERROR_PRINT("DeferredRenderer: DxrSmokeTestCS failed to load; DXR smoke test disabled");
		}
		else
		{
			myDxrSmokeCb.Create(*DX11::Rhi(), sizeof(DxrSmokeCb), rhi::ShaderStage::Compute, 0, "DxrSmokeCb");
			{
				rhi::SamplerDesc sd2;
				sd2.filter = rhi::FilterMode::Anisotropic;
				sd2.maxAnisotropy = 16;
				sd2.address = rhi::AddressMode::Wrap;
				myDxrSmokeSampler = DX11::Rhi()->CreateSampler(sd2);
			}
			myDxrSmokeCopyPs = DX11::LoadPixelShader("Shaders/PostprocessCopyPS");
			myTaaCS = DX11::LoadComputeShaderDxil("Shaders/TemporalResolveCS");
			myNrdCompositeCS = DX11::LoadComputeShaderDxil("Shaders/NrdCompositeCS");
			myTaaCb.Create(*DX11::Rhi(), sizeof(TaaCb), rhi::ShaderStage::Compute, 0, "TaaCb");
			if (!myTaaCS || !myTaaCb.IsValid()) myTunables.taaEnabled = false;
			if (!myDxrSmokeCb.IsValid() || !myDxrSmokeSampler.IsValid() || !myDxrSmokeCopyPs ||
				!CreateDxrSmokeTarget(aResolution))
				myDxrSmokeCS = nullptr;
			else if (!CreateDxrBrdfLut())
				ERROR_PRINT("DeferredRenderer: BRDF LUT generation failed; using the analytic IBL fallback.");
			else
				INFO_PRINT("DeferredRenderer: DXR smoke test ready (toggle with SetDxrSmokeTest)");
		}

		// Ray-traced probe capture -- reuses myGiShBuffer (created above, if
		// GiProjectSHCS loaded) and myDxrSmokeSampler (created just above) for
		// the material albedo/orm/normal fetch inside DecodeHit.
		if (myGiShBuffer.IsValid())
		{
			myGiTraceCS = DX11::LoadComputeShaderDxil("Shaders/GiTraceInlineCS");
			if (!myGiTraceCS)
			{
				ERROR_PRINT("DeferredRenderer: GiTraceInlineCS failed to load; ray-traced GI capture disabled");
			}
			else
			{
				myGiTraceCb.Create(*DX11::Rhi(), sizeof(GiTraceCb), rhi::ShaderStage::Compute, 0, "GiTraceCb");
				myGiProbeBatchBuffer.Create(*DX11::Rhi(), 16, kMaxGiProbeBatch,
				                            /*withUav*/ false, /*cpuUpdatable*/ true, "GiProbeBatch");
				if (!myGiTraceCb.IsValid() || !myGiProbeBatchBuffer.IsValid())
					myGiTraceCS = nullptr;
				else
					INFO_PRINT("DeferredRenderer: ray-traced GI probe capture ready (GiProjectProbeBatchRT, up to %d probes/dispatch)", kMaxGiProbeBatch);
			}
		}
	}

	// Structured light buffer (t15) + params cbuffer (b6) for the deferred resolve.
	{
		myLightBuffer.Create(*DX11::Rhi(), sizeof(DeferredLight), kMaxLights,
		                     /*withUav*/ false, /*cpuUpdatable*/ true, "LightBuffer");
		if (!myLightBuffer.IsValid())
		{
			ERROR_PRINT("DeferredRenderer: failed to create light buffer");
			return false;
		}
		myLightParamsCb.Create(*DX11::Rhi(), 16, rhi::ShaderStage::Pixel, 6, "LightParamsCb");   // uint count + pad
		if (!myLightParamsCb.IsValid())
		{
			ERROR_PRINT("DeferredRenderer: failed to create light params cb");
			return false;
		}
	}

	// --- cascaded shadow maps ---
	myShadowShader = std::make_unique<ModelShader>();
	if (!myShadowShader->Init("Shaders/PbrModelShaderVS", "Shaders/ShadowPS"))
	{
		ERROR_PRINT("DeferredRenderer: shadow shader failed; shadows disabled");
		myShadowShader.reset();
	}
	else if (!CreateShadowMaps())
	{
		myShadowShader.reset();
	}
	else
	{
		{
			rhi::SamplerDesc sd2;
			sd2.filter = rhi::FilterMode::ComparisonBilinear;
			sd2.address = rhi::AddressMode::Border;
			sd2.comparison = true;
			sd2.borderColor[0] = sd2.borderColor[1] = sd2.borderColor[2] = sd2.borderColor[3] = 1.0f;
			myShadowCmpSampler = DX11::Rhi()->CreateSampler(sd2);
		}

		myShadowCb.Create(*DX11::Rhi(), sizeof(ShadowCb), rhi::ShaderStage::Pixel, 9, "ShadowCb");
		if (!myShadowCmpSampler.IsValid() || !myShadowCb.IsValid()) myShadowShader.reset();
	}

	// --- point / spot light shadow atlas (reuses myShadowShader + s2 cmp sampler) ---
	if (myShadowShader && !CreateLocalShadowAtlas())
	{
		if (myLocalAtlasSrv.IsValid()) { DX11::Rhi()->Destroy(myLocalAtlasSrv); myLocalAtlasSrv = {}; }
		ERROR_PRINT("DeferredRenderer: local shadow atlas failed; point/spot shadows disabled");
	}

	// --- clustered light culling ---
	myClusterCS = DX11::LoadComputeShader("Shaders/ClusterCullCS");
	if (!myClusterCS)
	{
		ERROR_PRINT("DeferredRenderer: ClusterCullCS failed to load; clustering disabled (brute-force lighting)");
	}
	else
	{
		myClusterCb.Create(*DX11::Rhi(), sizeof(ClusterCb), rhi::ShaderStage::Compute, 0, "ClusterCb");
		if (!myClusterCb.IsValid())
		{
			ERROR_PRINT("DeferredRenderer: failed to create cluster cb; clustering disabled");
			myClusterCS = nullptr;
		}
		else if (!CreateClusterBuffers(aResolution))
		{
			myClusterCS = nullptr;
		}
	}

	// --- post fx: bloom + auto exposure ---
	myBloomPrefilterPs = DX11::LoadPixelShader("Shaders/BloomPrefilterPS");
	myBloomDownPs      = DX11::LoadPixelShader("Shaders/BloomDownPS");
	myBloomUpPs        = DX11::LoadPixelShader("Shaders/BloomUpPS");
	myExposureLumaPs   = DX11::LoadPixelShader("Shaders/ExposureLumaPS");
	myExposureDownPs   = DX11::LoadPixelShader("Shaders/ExposureDownPS");
	myExposureAdaptPs  = DX11::LoadPixelShader("Shaders/ExposureAdaptPS");
	myCompositePs      = DX11::LoadPixelShader("Shaders/DeferredCompositePS");
	if (!myBloomPrefilterPs || !myBloomDownPs || !myBloomUpPs || !myExposureLumaPs ||
	    !myExposureDownPs || !myExposureAdaptPs || !myCompositePs)
	{
		ERROR_PRINT("DeferredRenderer: post-fx shaders failed to load; using plain tonemap");
		myCompositePs = nullptr;
	}
	else
	{
		{
			rhi::SamplerDesc sd2;
			sd2.filter = rhi::FilterMode::Trilinear;
			sd2.address = rhi::AddressMode::Clamp;
			myLinearSampler = DX11::Rhi()->CreateSampler(sd2);
		}

		myPostFxCb.Create(*DX11::Rhi(), sizeof(PostFxCb), rhi::ShaderStage::Pixel, 10, "PostFxCb");

		CreatePostFxTargets(aResolution);
		if (!myLinearSampler.IsValid() || !myPostFxCb.IsValid()) myCompositePs = nullptr;
	}

	myAtmospherePs = DX11::LoadPixelShader("Shaders/AtmosphereCompositePS");
	myVolumeCS = DX11::LoadComputeShader("Shaders/AtmosphereVolumeCS");
	if (DX11::Rhi()->SupportsRaytracingTier11())
		myVolumeRTCS = DX11::LoadComputeShaderDxil("Shaders/AtmosphereVolumeRTCS");
	myAtmosphereCb.Create(*DX11::Rhi(), sizeof(AtmosphereCb), rhi::ShaderStage::Compute, 8, "AtmosphereCb");
	myAtmosphereShadowCameraCb.Create(*DX11::Rhi(), 64, rhi::ShaderStage::Compute, 7, "AtmosphereShadowCameraCb");
	if (!myAtmospherePs || !myAtmosphereCb.IsValid() || !CreateAtmosphereTargets(aResolution))
		myTunables.fogEnabled = false;

	myReady = true;
	INFO_PRINT("DeferredRenderer: %ux%u G-buffer ready", aResolution.x, aResolution.y);
	return true;
}

void DeferredRenderer::UploadLights(const DeferredLight* aLights, int aCount)
{
	const int newCount = aCount < 0 ? 0 : (aCount > kMaxLights ? kMaxLights : aCount);
	// `shadowSlot` is renderer-owned and patched after every upload, so exclude
	// it from the comparison.  Any authored local-light edit otherwise has no
	// screen-space motion vector and would leave stale direct-light history.
	bool changed = newCount != myLightCount || myLights.size() != (size_t)newCount;
	if (!changed)
	{
		for (int i = 0; i < newCount; ++i)
		{
			if (std::memcmp(&myLights[i], &aLights[i], 13u * sizeof(float)) != 0)
			{
				changed = true;
				break;
			}
		}
	}
	myTaaLightingChanged = myTaaLightingChanged || changed;
	myLightCount = newCount;

	// Keep a CPU copy so RenderLocalShadows can pick casters and patch shadowSlot.
	myLights.assign(aLights, aLights + myLightCount);

	if (myLightCount > 0 && myLightBuffer.IsValid())
		myLightBuffer.Update(DX11::Rhi()->GetContext(), aLights, sizeof(DeferredLight) * myLightCount);

	if (myLightParamsCb.IsValid())
	{
		uint32_t v[4] = { (uint32_t)myLightCount, IsSSAO() ? 1u : 0u, 0, 0 };
		myLightParamsCb.Update(DX11::Rhi()->GetContext(), &v, sizeof(v));
	}
}

void DeferredRenderer::RenderSSAO()
{
	if (!IsSSAO()) return;

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	// --- pass 1: occlusion -> myAoRaw ---
	{
		{
			SsaoCb c{};
			memcpy(c.projToView,  myProjToView.GetDataPtr(),  sizeof(c.projToView));
			memcpy(c.viewToProj,  myViewToProj.GetDataPtr(),  sizeof(c.viewToProj));
			memcpy(c.worldToView, myWorldToView.GetDataPtr(), sizeof(c.worldToView));
			c.screenX = (float)myResolution.x; c.screenY = (float)myResolution.y;
			c.radius = myTunables.ssaoRadius; c.bias = myTunables.ssaoBias;
			c.intensity = myTunables.ssaoIntensity; c.power = myTunables.ssaoPower;
			mySsaoCb.Update(DX11::Rhi()->GetContext(), c);
		}

		SetTargets(ctx, { myAoRaw.GetRtv() }, {}, myResolution);

		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 11, myNormal.GetSrv());
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, DX11::DepthBuffer->GetSrv());
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		mySsaoCb.Bind(ctx);

		BindFullscreen(mySsaoPs);
		ctx.Draw(3, 0);
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 11, {});
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, {});
	}

	// --- pass 2: depth-aware blur myAoRaw -> myAo ---
	{
		{
			SsaoBlurCb c{};
			c.texelX = 1.0f / (float)myResolution.x;
			c.texelY = 1.0f / (float)myResolution.y;
			c.depthSigma = 0.0009f;
			mySsaoBlurCb.Update(DX11::Rhi()->GetContext(), c);
		}

		SetTargets(ctx, { myAo.GetRtv() }, {}, myResolution);

		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 18, myAoRaw.GetSrv());
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, DX11::DepthBuffer->GetSrv());
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		mySsaoBlurCb.Bind(ctx);

		BindFullscreen(mySsaoBlurPs);
		ctx.Draw(3, 0);
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 18, {});
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, {});
	}
}

void DeferredRenderer::SetReflectionProbeBox(const Vector3f& c, const Vector3f& h, bool enabled)
{
	myProbeBox[0] = c.x; myProbeBox[1] = c.y; myProbeBox[2] = c.z; myProbeBox[3] = enabled ? 1.f : 0.f;
	myProbeBox[4] = h.x; myProbeBox[5] = h.y; myProbeBox[6] = h.z; myProbeBox[7] = 0.f;
	if (!myProbeCb.IsValid()) return;
	myProbeCb.Update(DX11::Rhi()->GetContext(), myProbeBox, sizeof(myProbeBox));
}

void DeferredRenderer::SetGiVolume(const Vector3f& o, const Vector3f& s, int cx, int cy, int cz, float intensity, bool enabled, float autoSealStrength)
{
	if (!myGiVolumeCb.IsValid()) return;
	float d[12] = {
		o.x, o.y, o.z, enabled ? 1.f : 0.f,
		s.x, s.y, s.z, intensity,
		(float)0, (float)0, (float)0, autoSealStrength   // d[11] read as asfloat(gGiCounts.w)
	};
	// int4 counts -- reinterpret the first three of the last row's bits (d[11] stays float)
	int32_t* ci = (int32_t*)(d + 8);
	ci[0] = cx; ci[1] = cy; ci[2] = cz;
	myGiVolumeCb.Update(DX11::Rhi()->GetContext(), d, sizeof(d));
}

void DeferredRenderer::SetGiEnvironment(rhi::SrvHandle aSrv, const Vector3f& aTint, bool aEnabled)
{
	myGiEnvironmentSrv = aSrv;
	myGiEnvironmentTint = aTint;
	myGiEnvironmentEnabled = aEnabled && aSrv.IsValid();
}

void DeferredRenderer::ClearGi()
{
	if (!myGiShBuffer.Uav().IsValid()) return;
	const float z[4] = { 0.f, 0.f, 0.f, 0.f };
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiShBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiShPreviousBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiVisibilityBuffer.Uav(), z);
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiVisibilityPreviousBuffer.Uav(), z);
}

void DeferredRenderer::GiProjectProbe(rhi::SrvHandle aCubeSrv, int aProbeIndex, float aHysteresis, int aFaceRes)
{
	if (!HasGi() || !aCubeSrv.IsValid() || aProbeIndex < 0 || aProbeIndex >= kMaxGiProbes) return;

	{
		struct { uint32_t idx; float hyst; uint32_t faceRes; float pad; } p{
			(uint32_t)aProbeIndex, aHysteresis, (uint32_t)std::max(aFaceRes, 1), 0.f };
		myGiProjectCb.Update(DX11::Rhi()->GetContext(), p);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	rhi::ComputePipelineDesc pd;
	pd.cs = myGiProjectCS->module;
	ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, aCubeSrv);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myGiLinearSampler);
	myGiProjectCb.Bind(ctx);
	ctx.SetUnorderedAccess(0, myGiShBuffer.Uav());
	ctx.SetUnorderedAccess(1, myGiVisibilityBuffer.Uav());

	ctx.Dispatch(1, 1, 1);

	ctx.SetUnorderedAccess(0, {});
	ctx.SetUnorderedAccess(1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::GiProjectProbeRT(const Vector3f& aProbePos, int aProbeIndex, float aHysteresis, int aRayCount, float aFireflyClamp)
{
	const GiProbeBatchEntry entry{ aProbePos, aProbeIndex };
	GiProjectProbeBatchRT(&entry, 1, aHysteresis, aRayCount, aFireflyClamp);
}

void DeferredRenderer::GiProjectProbeBatchRT(const GiProbeBatchEntry* aEntries, int aCount,
	float aHysteresis, int aRayCount, float aFireflyClamp)
{
	if (!HasGiRT() || !aEntries || aCount <= 0) return;

	// More probes than one batch buffer holds: split instead of dropping the
	// tail, so a large giPrimeBatch still refreshes everything it asked for.
	// Bounded by StructuredBuffer's 4-updates-per-frame budget; beyond that the
	// buffer would be rewritten under a dispatch already in the command list.
	if (aCount > kMaxGiProbeBatch)
	{
		constexpr int kMaxChunks = 4;
		const int chunks = std::min(kMaxChunks, (aCount + kMaxGiProbeBatch - 1) / kMaxGiProbeBatch);
		for (int c = 0; c < chunks; ++c)
		{
			const int offset = c * kMaxGiProbeBatch;
			GiProjectProbeBatchRT(aEntries + offset, std::min(kMaxGiProbeBatch, aCount - offset),
			                      aHysteresis, aRayCount, aFireflyClamp);
		}
		return;
	}

	rhi::IDevice* dev = DX11::Rhi();
	rhi::ICommandContext& ctx = dev->GetContext();
	// GameWorld builds the TLAS after BeginFrame. Refresh the two root SRVs
	// now, and skip this batch when the current scene has no instances.
	if (!dev->BindRaytracingSceneForCompute()) return;

	// Compact out-of-range probes rather than dispatching groups for them: the
	// shader indexes GiSH/GiVisibility straight from this entry, so a bad index
	// would be an out-of-bounds UAV write.
	float batch[kMaxGiProbeBatch * 4];
	int groups = 0;
	for (int i = 0; i < aCount && groups < kMaxGiProbeBatch; ++i)
	{
		if (aEntries[i].index < 0 || aEntries[i].index >= kMaxGiProbes) continue;
		batch[groups * 4 + 0] = aEntries[i].position.x;
		batch[groups * 4 + 1] = aEntries[i].position.y;
		batch[groups * 4 + 2] = aEntries[i].position.z;
		// Bit-cast, not a float conversion: the shader reads this back with
		// asuint() and uses it directly as the probe's volume index.
		const uint32_t index = (uint32_t)aEntries[i].index;
		std::memcpy(&batch[groups * 4 + 3], &index, sizeof(index));
		++groups;
	}
	if (groups == 0) return;
	myGiProbeBatchBuffer.Update(ctx, batch, (uint32_t)(groups * 4 * sizeof(float)));

	{
		GiTraceCb cb{};
		cb.hysteresis = aHysteresis;
		cb.rayCount = (uint32_t)std::max(aRayCount, 1);
		cb.textureFiltering = myTunables.dxrTextureFiltering ? 1u : 0u;
		cb.specularAaStrength = myTunables.specularAaEnabled ? myTunables.specularAaStrength : 0.f;
		cb.lightCount = (uint32_t)myLightCount;
		// myShadowLightDir is the direction the sun's rays TRAVEL; shading
		// wants the opposite -- surface (or here, probe) toward the light.
		// Same convention as RenderDxrSmokeTest's sunDirToLight.
		const float lx = -myShadowLightDir.x, ly = -myShadowLightDir.y, lz = -myShadowLightDir.z;
		const float lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
		if (lLen > 1e-5f) { cb.sunDirToLight[0] = lx / lLen; cb.sunDirToLight[1] = ly / lLen; cb.sunDirToLight[2] = lz / lLen; }
		else { cb.sunDirToLight[0] = 0.f; cb.sunDirToLight[1] = 1.f; cb.sunDirToLight[2] = 0.f; }
		cb.sunRadiance[0] = myTunables.dxrSunTint[0] * myTunables.dxrSunIntensity;
		cb.sunRadiance[1] = myTunables.dxrSunTint[1] * myTunables.dxrSunIntensity;
		cb.sunRadiance[2] = myTunables.dxrSunTint[2] * myTunables.dxrSunIntensity;
		cb.ambientIntensity = myTunables.dxrAmbientIntensity;
	// Use the same environment energy for primary lighting and sky bounce.
	constexpr float kDxrEnvironmentScale = 1.0f;
	cb.environmentTint[0] = myGiEnvironmentTint.x * kDxrEnvironmentScale;
	cb.environmentTint[1] = myGiEnvironmentTint.y * kDxrEnvironmentScale;
	cb.environmentTint[2] = myGiEnvironmentTint.z * kDxrEnvironmentScale;
		// Generated probe maps have four diffuse-tail mips after four specular
		// mips; LOD 6 is a stable diffuse approximation for both those maps and
		// the shipped fallback environment assets. -1 disables the term.
		cb.environmentDiffuseMip = myGiEnvironmentEnabled ? 6.f : -1.f;
		cb.sampleSequence = myGiTraceSequence++;
		cb.fireflyClamp = std::max(aFireflyClamp, 0.01f);
		cb.infiniteBounce = std::max(myTunables.dxrGiInfiniteBounce, 0.f);
		myGiTraceCb.Update(ctx, cb);
	}

	// Share the current frame's material version with other ray passes.
	const rhi::SrvHandle materialSrv = RayTracingMaterialTable::Upload(*dev, ctx);

	rhi::ComputePipelineDesc pd;
	pd.cs = myGiTraceCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myGiTraceCb.Bind(ctx);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, materialSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myLightBuffer.Srv());
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myGiEnvironmentSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, myGiProbeBatchBuffer.Srv());
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myDxrSmokeSampler);
	ctx.SetUnorderedAccess(0, myGiShBuffer.Uav());
	ctx.SetUnorderedAccess(1, myGiVisibilityBuffer.Uav());

	// One 64-thread group per probe, all probes in one dispatch. Each group
	// writes only its own probe's 9 SH slots and 64 moment bins, so groups
	// never touch each other's storage and need no ordering between them --
	// see GiProjectProbeBatchRT's declaration for the one precondition that
	// keeps that true.
	ctx.Dispatch((uint32_t)groups, 1, 1);
	// Still needed once per batch: the camera pass (and the next batch) read
	// GiSH as an SRV, so the writes above must be visible before then.
	ctx.UavBarrier(myGiShBuffer.Handle());

	ctx.SetUnorderedAccess(0, {});
	ctx.SetUnorderedAccess(1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, {});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::BindDebugMaterial(const DebugMaterial& m)
{
	if (!myDebugMatCb.IsValid()) return;
	float d[12];
	d[0] = m.baseColor[0]; d[1] = m.baseColor[1]; d[2] = m.baseColor[2]; d[3] = 0.f;
	d[4] = m.roughness; d[5] = m.metalness; d[6] = m.ao; d[7] = m.emissiveStrength;
	d[8] = m.emissiveColor[0]; d[9] = m.emissiveColor[1]; d[10] = m.emissiveColor[2]; d[11] = 0.f;
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	myDebugMatCb.Update(ctx, d, sizeof(d));
	myDebugMatCb.Bind(ctx);
}

void DeferredRenderer::RenderSSR()
{
	if (!IsSSR()) return;

	const Tunables& t = myTunables;
	{
		SsrCb c{};
		memcpy(c.viewToProj,  myViewToProj.GetDataPtr(),  sizeof(c.viewToProj));
		memcpy(c.projToView,  myProjToView.GetDataPtr(),  sizeof(c.projToView));
		memcpy(c.worldToView, myWorldToView.GetDataPtr(), sizeof(c.worldToView));
		c.screenX = (float)mySsrRes.x; c.screenY = (float)mySsrRes.y;
		c.maxDistance = t.ssrMaxDistance; c.thickness = t.ssrThickness;
		c.roughnessCutoff = std::max(t.ssrRoughnessCutoff, 0.01f);
		c.strength = t.ssrStrength;
		c.steps = std::max(t.ssrSteps, 8);
		c.refineSteps = std::max(t.ssrRefineSteps, 0);
		mySsrCb.Update(DX11::Rhi()->GetContext(), c);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	rhi::SamplerHandle lin = myLinearSampler.IsValid() ? myLinearSampler : myPointSampler;

	// --- pass 1: half-res ray-march -> mySsrTex ---
	{
		const float clr[4] = { 0.f, 0.f, 0.f, 0.f };
		ctx.ClearRenderTarget(mySsrTex.GetRtv(), clr);
		SetTargets(ctx, { mySsrTex.GetRtv() }, {}, mySsrRes);

		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, myHdr.GetSrv());
		const rhi::SrvHandle gb[3] = { myAlbedo.GetSrv(), myNormal.GetSrv(), myMaterial.GetSrv() };
		ctx.SetShaderResources(rhi::ShaderStage::Pixel, 10, 3, gb);
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, DX11::DepthBuffer->GetSrv());
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 3, lin);
		mySsrCb.Bind(ctx);

		BindFullscreen(mySsrPs);
		ctx.Draw(3, 0);

		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, {});
		const rhi::SrvHandle gbNull[3] = {};
		ctx.SetShaderResources(rhi::ShaderStage::Pixel, 10, 3, gbNull);
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 14, {});
	}

	// --- pass 2: resolve SSR over the probe IBL, additively into HDR ---
	// SSRApplyPS outputs conf * (ssrRadiance - iblSpecular); additive blend then
	// does hdr += that, i.e. lerp(probe, ssr, conf) for the specular term.
	{
		auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
		gss.SetBlendState(BlendState::AdditiveBlend);

		SetTargets(ctx, { myHdr.GetRtv() }, {}, myResolution);

		const rhi::SrvHandle srvs2[2] = { mySsrTex.GetSrv(), myIblSpecTex.GetSrv() };
		ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 2, srvs2);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 3, lin);   // bilinear upsample of the half-res SSR

		BindFullscreen(mySsrApplyPs);
		ctx.Draw(3, 0);

		const rhi::SrvHandle srvs2Null[2] = {};
		ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 2, srvs2Null);
		gss.SetBlendState(BlendState::Disabled);
	}
}

bool DeferredRenderer::OnResize(Vector2ui aResolution)
{
	if (aResolution.x == 0 || aResolution.y == 0) return false;
	if (aResolution.x == myResolution.x && aResolution.y == myResolution.y) return true;
	// No history buffer is valid across a resolution change.  This covers both
	// raster and DXR modes; CreateDxrSmokeTarget also resets it, but may be
	// absent when the DXR path is disabled.
	ResetTemporalHistory();
	if (!CreateTargets(aResolution)) return false;
	if (myClusterCS) CreateClusterBuffers(aResolution);
	if (myCompositePs) CreatePostFxTargets(aResolution);
	if (myAtmospherePs && !CreateAtmosphereTargets(aResolution)) return false;
	if (myDxrSmokeCS && !CreateDxrSmokeTarget(aResolution)) return false;
	return true;
}

bool DeferredRenderer::CreateDxrSmokeTarget(Vector2ui aResolution)
{
	ResetTemporalHistory();
	rhi::IDevice* dev = DX11::Rhi();
	// DLSS SR owns the reconstruction from this smaller input to the display
	// resolution.  Keep DLAA/RR at 1:1; RR's guide textures share this input.
	float renderScale = 1.f;
	if (StreamlineDLSS::Get().IsAvailable())
	{
		switch (myTunables.dlssMode) {
		case 2: renderScale = 2.f / 3.f; break; // Quality
		case 3: renderScale = 0.58f; break;     // Balanced
		case 4: renderScale = 0.50f; break;     // Performance
		case 5: renderScale = 1.f / 3.f; break; // Ultra Performance
		default: break;
		}
	}
	myDxrRenderResolution = { std::max(1u, uint32_t(std::lround(float(aResolution.x) * renderScale))),
		std::max(1u, uint32_t(std::lround(float(aResolution.y) * renderScale))) };

	// NRD preallocates its history at a fixed size; recreate it on next use.
	myNrd.reset();
	myNrdFailed = false;

	// Resize can replace this target while the renderer stays alive. Release
	// the old views first, then their texture, so each resize owns exactly one
	// DXR output resource rather than leaking permanent descriptor slots.
	if (myDxrSmokeUav.IsValid()) dev->Destroy(myDxrSmokeUav);
	if (myDxrSmokeTestSrv.IsValid()) dev->Destroy(myDxrSmokeTestSrv);
	if (myDxrSmokeTex.IsValid()) dev->Destroy(myDxrSmokeTex);
	myDxrSmokeUav = {};
	myDxrSmokeTestSrv = {};
	myDxrSmokeTex = {};

	rhi::TextureDesc td = {};
	td.width = myDxrRenderResolution.x;
	td.height = myDxrRenderResolution.y;
	td.mipLevels = 1;
	td.dimension = rhi::TextureDimension::Tex2D;
	// RayQuery direct light and reflection radiance are physically HDR.  An
	// UNorm UAV clamps them before the composite pass can tone-map, hiding
	// bright sun/specular response and making DXR unlike the raster HDR path.
	td.format = rhi::Format::R16G16B16A16_Float;
	td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	td.debugName = "DxrSmokeTestTex";
	myDxrSmokeTex = dev->CreateTexture(td);
	if (!myDxrSmokeTex.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: DXR smoke-test texture creation failed");
		return false;
	}

	myDxrSmokeTestSrv = dev->CreateSrv(myDxrSmokeTex, rhi::SrvDesc{});
	myDxrSmokeUav = dev->CreateUav(myDxrSmokeTex, rhi::UavDesc{});
	const rhi::Format temporalFormats[] = { rhi::Format::R16G16_Float, rhi::Format::R32_Float, rhi::Format::R32G32_Float, rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float };
	const char* temporalNames[] = { "MotionVectorsPixels", "TemporalDeviceDepth", "MotionValidity", "TemporalSurfaceGuide", "DlssDiffuseAlbedo", "DlssSpecularAlbedo" };
	for (size_t i = 0; i < myTemporalTex.size(); ++i) {
		if (myTemporalUav[i].IsValid()) dev->Destroy(myTemporalUav[i]);
		if (myTemporalSrv[i].IsValid()) dev->Destroy(myTemporalSrv[i]);
		if (myTemporalTex[i].IsValid()) dev->Destroy(myTemporalTex[i]);
		td.format = temporalFormats[i]; td.debugName = temporalNames[i];
		myTemporalTex[i] = dev->CreateTexture(td);
		myTemporalSrv[i] = dev->CreateSrv(myTemporalTex[i], rhi::SrvDesc{});
		myTemporalUav[i] = dev->CreateUav(myTemporalTex[i], rhi::UavDesc{});
		if (!myTemporalTex[i].IsValid() || !myTemporalSrv[i].IsValid() || !myTemporalUav[i].IsValid()) return false;
	}
	{
		// Always allocated: DxrSmokeTestCS declares u7..u9 unconditionally.
		const rhi::Format nrdFormats[] = { rhi::Format::R32_Float, rhi::Format::R10G10B10A2_UNorm, rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float };
		const char* nrdNames[] = { "NrdViewZ", "NrdNormalRoughness", "NrdDiffuseNoisy", "NrdDiffuseDenoised" };
		for (size_t i = 0; i < myNrdTex.size(); ++i) {
			if (myNrdUav[i].IsValid()) dev->Destroy(myNrdUav[i]);
			if (myNrdSrv[i].IsValid()) dev->Destroy(myNrdSrv[i]);
			if (myNrdTex[i].IsValid()) dev->Destroy(myNrdTex[i]);
			td.format = nrdFormats[i]; td.debugName = nrdNames[i];
			myNrdTex[i] = dev->CreateTexture(td);
			myNrdSrv[i] = dev->CreateSrv(myNrdTex[i], rhi::SrvDesc{});
			myNrdUav[i] = dev->CreateUav(myNrdTex[i], rhi::UavDesc{});
			if (!myNrdTex[i].IsValid() || !myNrdSrv[i].IsValid() || !myNrdUav[i].IsValid()) return false;
		}
	}
	for (size_t i = 0; i < myTaaTex.size(); ++i) {
		if (myTaaUav[i].IsValid()) dev->Destroy(myTaaUav[i]);
		if (myTaaSrv[i].IsValid()) dev->Destroy(myTaaSrv[i]);
		if (myTaaTex[i].IsValid()) dev->Destroy(myTaaTex[i]);
		td.format = i == 1 || i == 3 ? rhi::Format::R32_Float : rhi::Format::R16G16B16A16_Float;
		td.debugName = "TaaHistory";
		myTaaTex[i] = dev->CreateTexture(td);
		myTaaSrv[i] = dev->CreateSrv(myTaaTex[i], {});
		myTaaUav[i] = dev->CreateUav(myTaaTex[i], {});
		if (!myTaaTex[i].IsValid() || !myTaaSrv[i].IsValid() || !myTaaUav[i].IsValid()) return false;
	}
	if (myDlaaUav.IsValid()) dev->Destroy(myDlaaUav);
	if (myDlaaSrv.IsValid()) dev->Destroy(myDlaaSrv);
	if (myDlaaTex.IsValid()) dev->Destroy(myDlaaTex);
	// DLSS always produces a display-resolution HDR image.
	td.width = aResolution.x; td.height = aResolution.y;
	td.format = rhi::Format::R16G16B16A16_Float;
	td.debugName = "StreamlineDLSSOutput";
	myDlaaTex = dev->CreateTexture(td);
	myDlaaSrv = dev->CreateSrv(myDlaaTex, {});
	myDlaaUav = dev->CreateUav(myDlaaTex, {});
	if (!myDlaaTex.IsValid() || !myDlaaSrv.IsValid() || !myDlaaUav.IsValid()) return false;
	if (!myDxrSmokeTestSrv.IsValid() || !myDxrSmokeUav.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: DXR smoke-test view creation failed");
		return false;
	}
	// Fog has to be applied in the render-resolution domain when DLSS upscales
	// (see ResolveDxrSmokeToHdr), and every fog input -- ray depth, the ray
	// output -- is at render resolution. Only pay for these while upscaling.
	if (myDxrRenderResolution.x != aResolution.x || myDxrRenderResolution.y != aResolution.y)
	{
		if (!CreateAtmosphereTargetSet(myDxrRenderResolution, myAtmosphereRender))
			ERROR_PRINT("DeferredRenderer: render-resolution fog targets failed; fog will be skipped under DLSS upscaling");
	}
	else
	{
		ReleaseAtmosphereTargetSet(myAtmosphereRender);
	}
	return true;
}

bool DeferredRenderer::CreateDxrBrdfLut()
{
	if (!myDxrBrdfLutCS) return false;
	rhi::IDevice* dev = DX11::Rhi();
	constexpr uint32_t kLutSize = 256;
	constexpr uint32_t kSampleCount = 1024;

	rhi::TextureDesc td{};
	td.width = kLutSize;
	td.height = kLutSize;
	td.mipLevels = 1;
	td.dimension = rhi::TextureDimension::Tex2D;
	td.format = rhi::Format::R16G16_Float;
	td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	td.debugName = "DxrBrdfLut";
	myDxrBrdfLutTex = dev->CreateTexture(td);
	myDxrBrdfLutSrv = dev->CreateSrv(myDxrBrdfLutTex, {});
	myDxrBrdfLutUav = dev->CreateUav(myDxrBrdfLutTex, {});
	myDxrBrdfLutCb.Create(*dev, 16, rhi::ShaderStage::Compute, 0, "DxrBrdfLutCb");
	if (!myDxrBrdfLutTex.IsValid() || !myDxrBrdfLutSrv.IsValid() || !myDxrBrdfLutUav.IsValid() || !myDxrBrdfLutCb.IsValid())
		return false;

	struct BrdfLutCb { uint32_t size, samples, pad0, pad1; } cb{ kLutSize, kSampleCount, 0, 0 };
	rhi::ICommandContext& ctx = dev->GetContext();
	myDxrBrdfLutCb.Update(ctx, cb);
	rhi::ComputePipelineDesc pd{};
	pd.cs = myDxrBrdfLutCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myDxrBrdfLutCb.Bind(ctx);
	ctx.SetUnorderedAccess(0, myDxrBrdfLutUav);
	ctx.Dispatch((kLutSize + 7) / 8, (kLutSize + 7) / 8, 1);
	ctx.SetUnorderedAccess(0, {});
	ctx.SetComputePipeline({});
	INFO_PRINT("DeferredRenderer: generated %ux%u BRDF integration LUT (%u samples/texel)", kLutSize, kLutSize, kSampleCount);
	return true;
}

bool DeferredRenderer::CreateClusterBuffers(Vector2ui aResolution)
{
	myTileCount = { (aResolution.x + kTilePx - 1) / kTilePx,
	                (aResolution.y + kTilePx - 1) / kTilePx };
	myNumClusters = (int)(myTileCount.x * myTileCount.y * kZSlices);

	auto makeStructured = [](rhi::StructuredBuffer& sb, UINT elements, const char* name) -> bool
	{
		sb.Create(*DX11::Rhi(), sizeof(uint32_t), elements, /*withUav*/ true, /*cpuUpdatable*/ false, name);
		return sb.IsValid();
	};

	if (!makeStructured(myClusterIndexBuffer, (UINT)myNumClusters * kMaxPerCluster, "ClusterIndexBuffer") ||
	    !makeStructured(myClusterCountBuffer, (UINT)myNumClusters, "ClusterCountBuffer"))
	{
		ERROR_PRINT("DeferredRenderer: cluster buffer creation failed; clustering disabled");
		myClusterCS = nullptr;
		return false;
	}

	INFO_PRINT("DeferredRenderer: cluster grid %ux%ux%d = %d clusters (%d max lights/cluster)",
		myTileCount.x, myTileCount.y, kZSlices, myNumClusters, kMaxPerCluster);
	return true;
}

bool DeferredRenderer::CreateShadowMaps()
{
	rhi::IDevice* dev = DX11::Rhi();

	rhi::TextureDesc td = {};
	td.width = td.height = kShadowRes;
	td.mipLevels = 1;
	td.depthOrArraySize = kNumCascades;
	td.dimension = rhi::TextureDimension::Tex2DArray;
	td.format = rhi::Format::D32_Float;   // typeless resource; DSV=D32_Float, SRV=R32_Float
	td.bind = rhi::TextureBind::DepthStencil | rhi::TextureBind::ShaderResource;
	td.debugName = "ShadowCascadeTex";
	myShadowTex = dev->CreateTexture(td);
	if (!myShadowTex.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: shadow texture creation failed");
		return false;
	}

	myShadowSrv = dev->CreateSrv(myShadowTex, rhi::SrvDesc{});
	if (!myShadowSrv.IsValid())
		return false;

	for (int i = 0; i < kNumCascades; ++i)
	{
		rhi::DsvDesc dd = {};
		dd.firstArraySlice = i;
		dd.arraySize = 1;
		myShadowDsvs[i] = dev->CreateDsv(myShadowTex, dd);
		if (!myShadowDsvs[i].IsValid())
			return false;
	}
	return true;
}

bool DeferredRenderer::CreateLocalShadowAtlas()
{
	rhi::IDevice* dev = DX11::Rhi();

	rhi::TextureDesc td = {};
	td.width = td.height = kLocalAtlasRes;
	td.mipLevels = 1;
	td.dimension = rhi::TextureDimension::Tex2D;
	td.format = rhi::Format::D32_Float;   // typeless resource; DSV=D32_Float, SRV=R32_Float
	td.bind = rhi::TextureBind::DepthStencil | rhi::TextureBind::ShaderResource;
	td.debugName = "LocalShadowAtlasTex";
	myLocalAtlasTex = dev->CreateTexture(td);
	if (!myLocalAtlasTex.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: local shadow atlas texture failed");
		return false;
	}

	myLocalAtlasDsv = dev->CreateDsv(myLocalAtlasTex, rhi::DsvDesc{});
	if (!myLocalAtlasDsv.IsValid())
		return false;

	myLocalAtlasSrv = dev->CreateSrv(myLocalAtlasTex, rhi::SrvDesc{});
	if (!myLocalAtlasSrv.IsValid())
		return false;

	myLocalShadowBuffer.Create(*DX11::Rhi(), sizeof(LocalShadowGpu), kLocalTileCount,
	                          /*withUav*/ false, /*cpuUpdatable*/ true, "LocalShadowBuffer");
	if (!myLocalShadowBuffer.IsValid())
		return false;

	INFO_PRINT("DeferredRenderer: local shadow atlas %dx%d, %d tiles of %dpx",
		kLocalAtlasRes, kLocalAtlasRes, kLocalTileCount, kLocalTilePx);
	return true;
}

bool DeferredRenderer::CreatePostFxTargets(Vector2ui aResolution)
{
	Vector2ui s{ std::max(1u, aResolution.x / 2u), std::max(1u, aResolution.y / 2u) };
	for (int i = 0; i < kBloomMips; ++i)
	{
		myBloomSize[i] = s;
		myBloomMip[i]  = RenderTarget::Create(s, rhi::Format::R11G11B10_Float);
		s = { std::max(1u, s.x / 2u), std::max(1u, s.y / 2u) };
	}

	const unsigned expSizes[7] = { 64, 32, 16, 8, 4, 2, 1 };
	for (int i = 0; i < 7; ++i)
		myExpMip[i] = RenderTarget::Create({ expSizes[i], expSizes[i] }, rhi::Format::R16_Float);

	// Persistent 1x1 exposure ping-pong -- created once, survives resize.
	if (!myExposure[0].GetShaderResourceView())
	{
		myExposure[0] = RenderTarget::Create({ 1, 1 }, rhi::Format::R32_Float);
		myExposure[1] = RenderTarget::Create({ 1, 1 }, rhi::Format::R32_Float);
		myExposureCleared = false;
	}
	return true;
}

void DeferredRenderer::SetShadowLight(const Vector3f& aLightDir, const Vector3f& aSceneCenter, float aSceneRadius)
{
	// A sun rotation changes visibility across otherwise stationary pixels.
	// Motion/depth reprojection cannot detect that, so reject this frame's TAA
	// history rather than blending the previous shadow into the new one.
	const float oldLenSq = myShadowLightDir.LengthSqr();
	const float newLenSq = aLightDir.LengthSqr();
	if (oldLenSq > 1e-8f && newLenSq > 1e-8f)
	{
		const float alignment = myShadowLightDir.Dot(aLightDir) / std::sqrt(oldLenSq * newLenSq);
		if (alignment < 0.999999f)
			myTaaLightingChanged = true;
	}
	myShadowLightDir = aLightDir;
	myShadowSceneCenter = aSceneCenter;
	myShadowSceneRadius = aSceneRadius > 1.f ? aSceneRadius : 1.f;
}

void DeferredRenderer::RenderShadows(const std::function<void(const Camera&)>& aDrawShadowCasters)
{
	if (!IsShadows() || !aDrawShadowCasters) return;

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	const Camera savedCam = gss.GetCamera();

	// View camera clip -> world (perspective), for frustum-corner unprojection.
	const Matrix4x4f camToWorld = Matrix4x4f::GetFastInverse(myWorldToView);
	const Matrix4x4f clipToWorld = myProjToView * camToWorld;
	auto unproject = [&](float nx, float ny, float nz) -> Vector3f {
		Vector4f w = Vector4f(nx, ny, nz, 1.f) * clipToWorld;
		return { w.x / w.w, w.y / w.w, w.z / w.w };
	};

	const float pn = myNear, pf = myFar;
	// Cover only the near slice of the view — far cascades at a 90 deg FOV blow up
	// fast, and interior shadows don't need reach. 1.3 x the scene diagonal keeps
	// even the last cascade tight enough to stay crisp from any camera distance.
	const float shadowFar = std::min(pf, myShadowSceneRadius * 1.3f);
	// Lower lambda = splits lean linear = the first cascade covers more ground
	// (less "snap in only when you're right on top of something").
	const float lambda = 0.22f;
	float splits[kNumCascades + 1];
	splits[0] = pn;
	for (int i = 1; i <= kNumCascades; ++i)
	{
		const float p = (float)i / (float)kNumCascades;
		const float logv = pn * std::pow(shadowFar / pn, p);
		const float linv = pn + (shadowFar - pn) * p;
		splits[i] = lambda * logv + (1.f - lambda) * linv;
	}

	// Light forward is the direction sunlight travels. The shadow camera
	// looks from the sun toward the scene along this same direction.
	Vector3f fwd = myShadowLightDir;
	{ float l = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
	  if (l > 1e-5f) fwd = { fwd.x/l, fwd.y/l, fwd.z/l }; }

	// world-space corner rays (near NDC z=0 .. far NDC z=1) for x,y in {-1,1}
	Vector3f nearC[4], farC[4];
	const float cx[4] = { -1,  1, -1,  1 };
	const float cy[4] = { -1, -1,  1,  1 };
	for (int k = 0; k < 4; ++k) { nearC[k] = unproject(cx[k], cy[k], 0.f); farC[k] = unproject(cx[k], cy[k], 1.f); }

	ShadowCb cb{};
	for (int c = 0; c < kNumCascades; ++c)
	{
		const float fracN = (splits[c]     - pn) / (pf - pn);
		const float fracF = (splits[c + 1] - pn) / (pf - pn);
		Vector3f corners[8];
		for (int k = 0; k < 4; ++k)
		{
			corners[k]     = Lerp(nearC[k], farC[k], fracN);
			corners[k + 4] = Lerp(nearC[k], farC[k], fracF);
		}
		Vector3f center{ 0,0,0 };
		for (auto& p : corners) center = { center.x + p.x, center.y + p.y, center.z + p.z };
		center = { center.x / 8.f, center.y / 8.f, center.z / 8.f };
		float radius = 0.f;
		for (auto& p : corners)
		{
			const Vector3f d{ p.x - center.x, p.y - center.y, p.z - center.z };
			radius = std::max(radius, std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z));
		}
		radius = std::ceil(radius);

		// Light basis (rotation only), forward = -L.
		Vector3f up = (std::abs(fwd.y) > 0.99f) ? Vector3f{ 0,0,1 } : Vector3f{ 0,1,0 };
		Vector3f right{ up.y*fwd.z - up.z*fwd.y, up.z*fwd.x - up.x*fwd.z, up.x*fwd.y - up.y*fwd.x };
		{ float l = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z); right = { right.x/l, right.y/l, right.z/l }; }
		up = { fwd.y*right.z - fwd.z*right.y, fwd.z*right.x - fwd.x*right.z, fwd.x*right.y - fwd.y*right.x };

		Matrix4x4f lrot = Matrix4x4f::CreateIdentityMatrix();
		lrot(1,1)=right.x; lrot(1,2)=right.y; lrot(1,3)=right.z;
		lrot(2,1)=up.x;    lrot(2,2)=up.y;    lrot(2,3)=up.z;
		lrot(3,1)=fwd.x;   lrot(3,2)=fwd.y;   lrot(3,3)=fwd.z;

		// Texel-snap the cascade centre in light space so shadow edges don't
		// crawl as the camera moves.
		const float texelWorld = (2.f * radius) / (float)kShadowRes;
		{
			const Matrix4x4f worldToLight = Matrix4x4f::GetFastInverse(lrot);
			Vector4f cls = Vector4f(center.x, center.y, center.z, 1.f) * worldToLight;
			cls.x = std::floor(cls.x / texelWorld) * texelWorld;
			cls.y = std::floor(cls.y / texelWorld) * texelWorld;
			Vector4f cw = cls * lrot;
			center = { cw.x, cw.y, cw.z };
		}

		// Pull the light back just far enough to catch casters above the slice,
		// and give the ortho a depth range that spans from there through the slice
		// plus headroom for tall casters (towers) directly overhead.
		const float castHeadroom = std::min(myShadowSceneRadius, radius * 4.f + 400.f);
		const float backoff = radius + castHeadroom;
		const float orthoDepth = backoff + radius + 200.f;
		const Vector3f lightPos{ center.x - fwd.x * backoff,
		                         center.y - fwd.y * backoff,
		                         center.z - fwd.z * backoff };

		Matrix4x4f lw = lrot;
		lw.SetPosition(lightPos);

		myCascadeCam[c].SetTransform(lw);
		myCascadeCam[c].SetOrtographicProjection(2.f*radius, 2.f*radius, orthoDepth);
		myCascadeViewProj[c] = Matrix4x4f::GetFastInverse(lw) * myCascadeCam[c].GetProjection();
		myCascadeSplit[c] = splits[c + 1];

		std::memcpy(cb.cascadeViewProj[c], myCascadeViewProj[c].GetDataPtr(), sizeof(float) * 16);
		cb.cascadeSplits[c] = splits[c + 1];
		cb.cascadeTexelWorld[c] = texelWorld;
		cb.cascadeDepthRange[c] = orthoDepth;

		// --- render this cascade ---
		ctx.ClearDepthStencil(myShadowDsvs[c], 1.0f, 0);
		SetTargets(ctx, {}, myShadowDsvs[c], Vector2ui{ (uint32_t)kShadowRes, (uint32_t)kShadowRes });

		gss.SetCamera(myCascadeCam[c]);
		gss.UpdateGpuStates(true);
		aDrawShadowCasters(myCascadeCam[c]);
	}

	cb.texel = 1.0f / (float)kShadowRes;
	cb.depthBias = myTunables.shadowDepthBias;
	cb.strength = myTunables.shadowStrength;
	cb.enabled = 1.0f;
	cb.normalOffset = myTunables.shadowNormalOffset;
	cb.showCascades = myTunables.giViz ? 4.0f : (myTunables.localShadowViz ? 3.0f
		: (myTunables.contactViz ? 2.0f : (myTunables.shadowShowCascades ? 1.0f : 0.0f)));
	cb.contactLength = myTunables.contactShadows ? myTunables.contactLength : 0.0f;
	cb.contactThickness = myTunables.contactThickness;
	cb.dxrSunShadows = IsDxrSunShadows() ? 1.f : 0.f;
	cb.dxrSunShadowDebug = myTunables.dxrSunShadowDebug ? 1.f : 0.f;
	myShadowCb.Update(DX11::Rhi()->GetContext(), cb);

	// restore the view camera for the rest of the frame
	gss.SetCamera(savedCam);
	gss.UpdateGpuStates(true);
}

void DeferredRenderer::RenderLocalShadows(const std::function<void(const Camera&)>& aDrawShadowCasters)
{
	// Reset every light's slot; only the picked casters get one below. Re-upload the
	// light buffer at the end so the lighting resolve sees the patched slots.
	for (DeferredLight& L : myLights) L.shadowSlot = -1.0f;

	auto reupload = [&]()
	{
		if (myLights.empty() || !myLightBuffer.IsValid()) return;
		myLightBuffer.Update(DX11::Rhi()->GetContext(), myLights.data(), (uint32_t)(sizeof(DeferredLight) * myLights.size()));
	};

	if (!IsLocalShadows() || !aDrawShadowCasters || myLights.empty()) { reupload(); return; }

	// --- rank candidates: spot = 1 tile, point/area = 6 cube-face tiles ---
	//
	// Shadow-slot ownership must be stable in world space.  The old score was
	// divided by the light's distance from myCameraPos, so merely walking the
	// camera caused lights around the budget cutoff to trade atlas slots.  A
	// light that lost its slot immediately changed from shadowed to unshadowed,
	// which made the shadows appear to follow the player and pop out vertically.
	// Rank by the light's camera-independent scene influence instead.  The index
	// tie-break keeps the assignment deterministic for equal authored lights.
	struct Cand { int idx; float score; int tiles; bool spot; };
	std::vector<Cand> cands;
	cands.reserve(myLights.size());
	for (int i = 0; i < (int)myLights.size(); ++i)
	{
		const DeferredLight& L = myLights[i];
		if (L._pad[0] > 0.5f) continue;   // light opted out of shadow casting
		const bool spot = L.spotCosOuter > 0.0f;
		const float lum = 0.2126f*L.color[0] + 0.7152f*L.color[1] + 0.0722f*L.color[2];
		if (lum <= 1e-4f) continue;
		const float score = lum * std::max(L.range * L.range, 1.0f);
		cands.push_back({ i, score, spot ? 1 : 6, spot });
	}
	std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b)
	{
		return a.score != b.score ? a.score > b.score : a.idx < b.idx;
	});

	std::vector<LocalShadowGpu> entries((size_t)kLocalTileCount);
	memset(entries.data(), 0, entries.size() * sizeof(LocalShadowGpu));

	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	const Camera savedCam = gss.GetCamera();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.ClearDepthStencil(myLocalAtlasDsv, 1.0f, 0);
	ctx.SetRenderTargets(0, nullptr, myLocalAtlasDsv);

	// Render one atlas tile: fill its transform entry + draw the casters into its viewport.
	auto renderTile = [&](int tile, const Vector3f& pos, const Vector3f& fwd,
	                      float fovDeg, float nearP, float farP, float typeFlag)
	{
		const Matrix4x4f lw = MakeLookTransform(pos, fwd);
		Camera cam;
		cam.SetTransform(lw);
		cam.SetPerspectiveProjection(fovDeg, { 1.0f, 1.0f }, nearP, farP);
		const Matrix4x4f vp = Matrix4x4f::GetFastInverse(lw) * cam.GetProjection();

		const int tx = tile % kLocalTilesRow;
		const int ty = tile / kLocalTilesRow;
		LocalShadowGpu& e = entries[tile];
		memcpy(e.viewProj, vp.GetDataPtr(), sizeof(float) * 16);
		e.tile[0] = (float)tx / (float)kLocalTilesRow;
		e.tile[1] = (float)ty / (float)kLocalTilesRow;
		e.tile[2] = 1.0f / (float)kLocalTilesRow;
		e.tile[3] = 1.0f / (float)kLocalTilesRow;
		e.misc[0] = 1.0f;
		e.misc[1] = typeFlag;   // 0 = spot, 1 = point cube face
		e.misc[2] = 0.0022f;
		e.misc[3] = 0.0f;

		ctx.SetViewport((float)(tx * kLocalTilePx), (float)(ty * kLocalTilePx),
		               (float)kLocalTilePx, (float)kLocalTilePx);
		gss.SetCamera(cam);
		gss.UpdateGpuStates(true);
		// The callback frustum-culls sub-meshes to this tile's light view --
		// a point light's 6 faces each see ~1/6 of the scene.
		aDrawShadowCasters(cam);
	};

	// +X, -X, +Y, -Y, +Z, -Z -- must match CubeFace() in DeferredLightingPS.
	static const Vector3f kCubeFwd[6] = {
		{ 1,0,0 }, { -1,0,0 }, { 0,1,0 }, { 0,-1,0 }, { 0,0,1 }, { 0,0,-1 } };

	// A point light costs 6x a spot (cube). Budget from the tunables, and drop any
	// caster far dimmer than the brightest -- fill lights don't need a shadow map.
	const float bestScore = cands.empty() ? 0.0f : cands.front().score;
	const int maxCasters = std::clamp(myTunables.localShadowMaxCasters, 0, kMaxShadowLights);
	const int maxPoints  = std::clamp(myTunables.localShadowMaxPoints, 0, maxCasters);

	int tileNext = 0, lightsUsed = 0, pointsUsed = 0;
	for (const Cand& c : cands)
	{
		if (lightsUsed >= maxCasters) break;
		if (c.score < bestScore * 0.25f) break;            // sorted -> rest are dimmer still
		if (!c.spot && pointsUsed >= maxPoints) continue;
		if (tileNext + c.tiles > kLocalTileCount) continue;

		DeferredLight& L = myLights[c.idx];
		const Vector3f pos{ L.position[0], L.position[1], L.position[2] };
		const float farP  = std::max(L.range, 50.0f);
		const float nearP = std::max(farP * 0.03f, 5.0f);

		if (c.spot)
		{
			const Vector3f dir{ L.spotDir[0], L.spotDir[1], L.spotDir[2] };
			const float outer = std::acos(std::clamp(L.spotCosOuter, -1.0f, 1.0f));
			const float fovDeg = std::min(outer * 2.0f * 57.29578f + 8.0f, 170.0f);
			renderTile(tileNext, pos, dir, fovDeg, std::max(farP * 0.05f, 5.0f), farP, 0.0f);
		}
		else
		{
			// 100 deg (not 90) so a caster straddling a face boundary is still in
			// this face's render+cull frustum; the shader picks faces by 90 deg
			// quadrants and projects by this wider matrix (stays in-bounds).
			for (int f = 0; f < 6; ++f)
				renderTile(tileNext + f, pos, kCubeFwd[f], 100.0f, nearP, farP, 1.0f);
		}

		L.shadowSlot = (float)tileNext;
		tileNext += c.tiles;
		++lightsUsed;
		if (!c.spot) ++pointsUsed;
	}

	// upload the per-tile transforms
	if (myLocalShadowBuffer.IsValid())
		myLocalShadowBuffer.Update(DX11::Rhi()->GetContext(), entries.data(), (uint32_t)(entries.size() * sizeof(LocalShadowGpu)));
	reupload();

	gss.SetCamera(savedCam);
	gss.UpdateGpuStates(true);
}

void DeferredRenderer::SetCamera(const Camera& aCamera)
{
	if (myTemporalHistoryValid) {
		const Vector3f delta = aCamera.GetTransform().GetPosition() - myCameraTransform.GetPosition();
		const Vector3f oldForward = myCameraTransform.GetForward(), newForward = aCamera.GetTransform().GetForward();
		const float alignment = oldForward.x*newForward.x + oldForward.y*newForward.y + oldForward.z*newForward.z;
		if (delta.Length() > 500.f || alignment < 0.7f || aCamera.GetProjection() != myViewToProj) ResetTemporalHistory();
	}
	myViewToProj  = aCamera.GetProjection();
	myProjToView  = aCamera.GetProjection().GetInverse();
	myWorldToView = Matrix4x4f::GetFastInverse(aCamera.GetTransform());
	aCamera.GetProjectionPlanes(myNear, myFar);
	myCameraPos = aCamera.GetTransform().GetPosition();
	myCameraTransform = aCamera.GetTransform();   // DXR smoke test needs position + basis directly
}

void DeferredRenderer::RenderDxrSunShadows()
{
	if (!IsDxrSunShadows() || !DX11::Rhi()->BindRaytracingSceneForCompute()) return;
	rhi::ICommandContext& ctx=DX11::Rhi()->GetContext(); DxrShadowCb cb{};
	const Vector3f pos=myCameraTransform.GetPosition(), right=myCameraTransform.GetRight(), up=myCameraTransform.GetUp(), forward=myCameraTransform.GetForward();
	cb.origin[0]=pos.x; cb.origin[1]=pos.y; cb.origin[2]=pos.z; cb.right[0]=right.x; cb.right[1]=right.y; cb.right[2]=right.z; cb.up[0]=up.x; cb.up[1]=up.y; cb.up[2]=up.z; cb.forward[0]=forward.x; cb.forward[1]=forward.y; cb.forward[2]=forward.z;
	const float m22=myViewToProj.GetDataPtr()[5]; cb.tanHalfFovY=m22!=0.f?1.f/m22:1.f; cb.aspect=myResolution.y?(float)myResolution.x/myResolution.y:1.f;
	float x=-myShadowLightDir.x,y=-myShadowLightDir.y,z=-myShadowLightDir.z,l=std::sqrt(x*x+y*y+z*z); cb.sunDir[0]=x/l; cb.sunDir[1]=y/l; cb.sunDir[2]=z/l; cb.bias=2.0f; cb.sizeX=myResolution.x; cb.sizeY=myResolution.y;
	myDxrShadowCb.Update(ctx,cb); rhi::ComputePipelineDesc pd; pd.cs=myDxrShadowCS->module; ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd)); myDxrShadowCb.Bind(ctx);
	ctx.SetUnorderedAccess(0,myDxrShadowUav);
	ctx.Dispatch((myResolution.x+7)/8,(myResolution.y+7)/8,1); ctx.SetUnorderedAccess(0,{}); ctx.SetComputePipeline({});
}

void DeferredRenderer::RenderDxrSmokeTest()
{
	if (!IsDxrSmokeTest()) return;

	rhi::IDevice* dev = DX11::Rhi();
	rhi::ICommandContext& ctx = dev->GetContext();
	// See GiProjectProbeRT: a begin-frame root bind can predate this frame's
	// TLAS build, and an empty scene must never trace the previous frame.
	if (!dev->BindRaytracingSceneForCompute()) { ResetTemporalHistory(); return; }
	const bool dlssActive = IsDxrRenderer() && (myTunables.dlssMode > 0 || myTunables.dlaaEnabled || myTunables.rayReconstructionEnabled) && StreamlineDLSS::Get().IsAvailable() && myTunables.dxrLightingView == 0;
	const bool taaActive = IsDxrRenderer() && (myTunables.taaEnabled || dlssActive) && myTunables.dxrLightingView == 0 && myTaaCS && myTaaCb.IsValid();
	const bool lightingChanged = myTaaLightingChanged;
	if (taaActive != myTaaWasEnabled) ResetTemporalHistory();
	myTaaWasEnabled = taaActive;
	myTaaCameraStationary = myTemporalHistoryValid && (myWorldToView * myViewToProj) == myPreviousWorldToClip;
	// Sub-pixel sample offset, in RENDER-resolution pixels, on [-0.5, 0.5].
	//
	// The three representations this has to agree with are now consistent, which
	// is what previously kept it pinned at zero:
	//   1. Ray generation offsets the sample by +gJitter (DxrSmokeTestCS::main).
	//   2. Motion vectors are measured from the pixel centre, so they are
	//      jitter-free by construction (see WriteTemporal) -- which is what both
	//      TemporalResolveCS and DLSS document that they want.
	//   3. The resolve reconstructs the fixed output grid by fetching current
	//      samples at `p + 0.5 - Jitter`, and the fog volume applies the same
	//      -jitter when it reads the ray depth.
	// The projection matrices are deliberately left unjittered: nothing samples
	// through them, they only project hit points for motion, and keeping them
	// clean is what makes (2) hold.
	//
	// Only jitter when something downstream actually resolves it. With neither
	// TAA nor DLSS running, this would just wobble the image. During a light
	// edit, fall back to the fixed grid for one frame: rejecting history while
	// still jittering would blend a just-moved shadow against no valid old
	// sample, which reads as a visibly jumping sample.
	myTaaJitter = {0,0};
	if (taaActive && !lightingChanged && myTunables.taaJitter)
	{
		// DLSS asks for its phase count to scale with the upscale ratio so the
		// extra samples actually cover the pixels being reconstructed; 8 is the
		// baseline at 1:1 (DLAA / native temporal).
		const float ratio = myDxrRenderResolution.x > 0
			? float(myResolution.x) / float(myDxrRenderResolution.x) : 1.f;
		const uint32_t phases = std::clamp(uint32_t(std::lround(8.f * ratio * ratio)), 8u, 64u);
		const uint32_t index = (myTaaFrameIndex % phases) + 1u;
		myTaaJitter = { RadicalInverse(index, 2u) - 0.5f, RadicalInverse(index, 3u) - 0.5f };
		// The rays are jittered through gJitter. Do not also jitter
		// myViewToProj: SetCamera compares it against the camera's projection
		// and would reset temporal history every frame, and motion vectors
		// would pick up the jitter delta.
	}

	{
		DxrSmokeCb c{};
		const Vector3f pos = myCameraTransform.GetPosition();
		const Vector3f right = myCameraTransform.GetRight();
		const Vector3f up = myCameraTransform.GetUp();
		const Vector3f fwd = myCameraTransform.GetForward();
		c.origin[0] = pos.x;   c.origin[1] = pos.y;   c.origin[2] = pos.z;
		c.right[0]  = right.x; c.right[1]  = right.y; c.right[2]  = right.z;
		c.up[0]     = up.x;    c.up[1]     = up.y;    c.up[2]     = up.z;
		c.forward[0] = fwd.x;  c.forward[1] = fwd.y;  c.forward[2] = fwd.z;
		// CreatePerspectiveMatrixFovX: persp[5] (m22) = xScale*aspectRatio = 1/tan(halfFovY).
		const float m22 = myViewToProj.GetDataPtr()[5];
		c.tanHalfFovY = m22 != 0.f ? 1.f / m22 : 1.f;
		c.aspect = myResolution.y != 0 ? (float)myResolution.x / (float)myResolution.y : 1.f;
		c.sizeX = myDxrRenderResolution.x;
		c.sizeY = myDxrRenderResolution.y;
		// myShadowLightDir is the direction the sun's rays TRAVEL (matches
		// SetShadowLight's cascade-camera convention); shading wants the
		// opposite -- the direction FROM a surface TOWARD the light.
		const float lx = -myShadowLightDir.x, ly = -myShadowLightDir.y, lz = -myShadowLightDir.z;
		const float lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
		if (lLen > 1e-5f) { c.sunDirToLight[0] = lx / lLen; c.sunDirToLight[1] = ly / lLen; c.sunDirToLight[2] = lz / lLen; }
		else { c.sunDirToLight[0] = 0.f; c.sunDirToLight[1] = 1.f; c.sunDirToLight[2] = 0.f; }
		c.lightCount = (uint32_t)myLightCount;
		c.sunRadiance[0] = myTunables.dxrSunTint[0] * myTunables.dxrSunIntensity;
		c.sunRadiance[1] = myTunables.dxrSunTint[1] * myTunables.dxrSunIntensity;
		c.sunRadiance[2] = myTunables.dxrSunTint[2] * myTunables.dxrSunIntensity;
		c.ambientIntensity = myTunables.dxrAmbientIntensity;
		c.reflectionRoughnessCutoff = myTunables.dxrReflectionRoughnessCutoff;
		constexpr float kDxrEnvironmentScale = 1.0f;
		c.environmentTint[0] = myGiEnvironmentTint.x * kDxrEnvironmentScale;
		c.environmentTint[1] = myGiEnvironmentTint.y * kDxrEnvironmentScale;
		c.environmentTint[2] = myGiEnvironmentTint.z * kDxrEnvironmentScale;
		c.environmentMip = myGiEnvironmentEnabled ? 0.f : -1.f;
		c.aoDistance = myTunables.dxrAoDistance;
		c.aoStrength = myTunables.dxrAoStrength;
		c.enableDirectLighting = myTunables.dxrDirectLighting ? 1u : 0u;
		c.enableEnvironmentLighting = myTunables.dxrEnvironmentLighting ? 1u : 0u;
		c.enableIndirectGi = myTunables.dxrIndirectGi ? 1u : 0u;
		c.enableReflections = myTunables.dxrReflections ? 1u : 0u;
		c.enableAmbientOcclusion = myTunables.dxrAmbientOcclusion ? 1u : 0u;
		c.pad6 = (uint32_t)myTunables.dxrLightingView;
		c.pad5[0] = myDxrBrdfLutSrv.IsValid() ? 1u : 0u;
		c.pad7 = myTunables.dxrTextureFiltering ? 1u : 0u;
		c.reflectionSamples = uint32_t(std::clamp(myTunables.dxrReflectionSamples, 1, 4));
		c.aoSamples = uint32_t(std::clamp(myTunables.dxrAoSamples, 1, 8));
		const Matrix4x4f worldToClip = myWorldToView * myViewToProj;
		memcpy(c.worldToClip, worldToClip.GetDataPtr(), sizeof(c.worldToClip));
		memcpy(c.previousWorldToClip, myPreviousWorldToClip.GetDataPtr(), sizeof(c.previousWorldToClip));
		c.temporalHistoryValid = (myTemporalHistoryValid && !lightingChanged) ? 1u : 0u;
		c.specularAaStrength = myTunables.specularAaEnabled ? myTunables.specularAaStrength : 0.f;
		c.reflectionFrameIndex = myDxrFrameIndex++;
		c.jitter[0] = myTaaJitter.x; c.jitter[1] = myTaaJitter.y;
		c.previousJitter[0] = myPreviousTaaJitter.x; c.previousJitter[1] = myPreviousTaaJitter.y;
		c.nrdEnabled = NrdActive() ? 1u : 0u;
		myDxrSmokeCb.Update(ctx, c);
	}

	// Rebuilding a few dozen material records every frame is trivial next to
	// an actual ray-traced pass -- see RayTracingMaterialTable::Upload.
	const rhi::SrvHandle materialSrv = RayTracingMaterialTable::Upload(*dev, ctx);

	rhi::ComputePipelineDesc pd;
	pd.cs = myDxrSmokeCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myDxrSmokeCb.Bind(ctx);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, materialSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myLightBuffer.Srv());
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, HasGi() ? myGiShBuffer.Srv() : rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myGiEnvironmentSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, myDxrBrdfLutSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 5, myGiVisibilityBuffer.Srv());
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myDxrSmokeSampler);
	myGiVolumeCb.Bind(ctx, rhi::ShaderStage::Compute, 13);
	ctx.SetUnorderedAccess(0, myDxrSmokeUav);
	for (uint32_t i = 0; i < myTemporalUav.size(); ++i) ctx.SetUnorderedAccess(i + 1, myTemporalUav[i]);
	const uint32_t nrdFirstUav = uint32_t(myTemporalUav.size()) + 1;
	for (uint32_t i = 0; i < 3; ++i) ctx.SetUnorderedAccess(nrdFirstUav + i, myNrdUav[i]);

	ctx.Dispatch((myDxrRenderResolution.x + 7) / 8, (myDxrRenderResolution.y + 7) / 8, 1);

	ctx.SetUnorderedAccess(0, {});
	for (uint32_t i = 0; i < myTemporalUav.size(); ++i) ctx.SetUnorderedAccess(i + 1, {});
	for (uint32_t i = 0; i < 3; ++i) ctx.SetUnorderedAccess(nrdFirstUav + i, {});
	myPreviousWorldToClip = myWorldToView * myViewToProj;
	myTemporalHistoryValid = true;
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, rhi::SrvHandle{});
	ctx.SetComputePipeline({});

	if (NrdActive())
		DenoiseDxrDiffuse(ctx);
}

bool DeferredRenderer::NrdActive() const
{
	// Mutually exclusive with Ray Reconstruction, which denoises the composed
	// frame itself and reads gDiffuseAlbedo.a as plain alpha.
	return myTunables.nrdEnabled && !myNrdFailed && myNrdCompositeCS && myNrdTex[3].IsValid()
		&& !myTunables.rayReconstructionEnabled && myTunables.dxrLightingView == 0
		&& dynamic_cast<rhi::dx12::Dx12CommandContext*>(&DX11::Rhi()->GetContext()) != nullptr;
}

void DeferredRenderer::DenoiseDxrDiffuse(rhi::ICommandContext& ctx)
{
	rhi::IDevice* dev = DX11::Rhi();
	auto* dx12Ctx = static_cast<rhi::dx12::Dx12CommandContext*>(&ctx);
	if (!myNrd)
	{
		myNrd = std::make_unique<rhi::dx12::NrdWrapper>();
		if (!myNrd->Initialize(static_cast<rhi::dx12::Dx12Device*>(dev), myDxrRenderResolution.x, myDxrRenderResolution.y))
		{
			myNrd.reset();
			myNrdFailed = true;
			return;
		}
		myNrdHistoryValid = false;
	}

	nrd::CommonSettings settings = {};
	// Both engine and NRD store matrices for row vectors in row-major order,
	// which is byte-identical to NRD's column-major, column-vector layout.
	// NRD wants the unjittered projection; jitter is passed separately.
	memcpy(settings.viewToClipMatrix, myViewToProj.GetDataPtr(), sizeof(settings.viewToClipMatrix));
	memcpy(settings.worldToViewMatrix, myWorldToView.GetDataPtr(), sizeof(settings.worldToViewMatrix));
	const bool history = myNrdHistoryValid;
	memcpy(settings.viewToClipMatrixPrev, (history ? myNrdPrevViewToClip : myViewToProj).GetDataPtr(), sizeof(settings.viewToClipMatrixPrev));
	memcpy(settings.worldToViewMatrixPrev, (history ? myNrdPrevWorldToView : myWorldToView).GetDataPtr(), sizeof(settings.worldToViewMatrixPrev));
	// IN_MV holds pixel deltas; NRD wants UV deltas.
	settings.motionVectorScale[0] = 1.f / float(myDxrRenderResolution.x);
	settings.motionVectorScale[1] = 1.f / float(myDxrRenderResolution.y);
	settings.motionVectorScale[2] = 0.f;
	settings.isMotionVectorInWorldSpace = false;
	settings.cameraJitter[0] = myTaaJitter.x;
	settings.cameraJitter[1] = myTaaJitter.y;
	settings.cameraJitterPrev[0] = myPreviousTaaJitter.x;
	settings.cameraJitterPrev[1] = myPreviousTaaJitter.y;
	settings.resourceSize[0] = settings.resourceSizePrev[0] = settings.rectSize[0] = settings.rectSizePrev[0] = uint16_t(myDxrRenderResolution.x);
	settings.resourceSize[1] = settings.resourceSizePrev[1] = settings.rectSize[1] = settings.rectSizePrev[1] = uint16_t(myDxrRenderResolution.y);
	settings.denoisingRange = 1e6f; // sky writes 1e7
	settings.accumulationMode = history ? nrd::AccumulationMode::CONTINUE : nrd::AccumulationMode::CLEAR_AND_RESTART;
	myNrdPrevViewToClip = myViewToProj;
	myNrdPrevWorldToView = myWorldToView;
	myNrdHistoryValid = true;

	// NRD restores exactly these states, so the RHI's tracking stays valid.
	ctx.TransitionResource(myNrdTex[0], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myNrdTex[1], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myNrdTex[2], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myTemporalTex[0], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myNrdTex[3], rhi::ResourceState::UnorderedAccess);
	dx12Ctx->FlushBarriers();

	rhi::dx12::NrdWrapper::Inputs in;
	in.viewZ = static_cast<ID3D12Resource*>(dev->GetNativeTexture(myNrdTex[0]));
	in.normalRoughness = static_cast<ID3D12Resource*>(dev->GetNativeTexture(myNrdTex[1]));
	in.diffuse = static_cast<ID3D12Resource*>(dev->GetNativeTexture(myNrdTex[2]));
	in.output = static_cast<ID3D12Resource*>(dev->GetNativeTexture(myNrdTex[3]));
	in.motion = static_cast<ID3D12Resource*>(dev->GetNativeTexture(myTemporalTex[0]));
	dx12Ctx->PushMarker("NRD RELAX diffuse");
	myNrd->Denoise(*dx12Ctx, in, settings);
	dx12Ctx->PopMarker();

	rhi::ComputePipelineDesc pd;
	pd.cs = myNrdCompositeCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myNrdSrv[3]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myTemporalSrv[4]);
	ctx.SetUnorderedAccess(0, myDxrSmokeUav);
	ctx.Dispatch((myDxrRenderResolution.x + 7) / 8, (myDxrRenderResolution.y + 7) / 8, 1);
	ctx.SetUnorderedAccess(0, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, rhi::SrvHandle{});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::CullClusters()
{
	if (!IsClustered() || myNumClusters == 0) return;

	// fill params
	{
		ClusterCb c{};
		memcpy(c.projToView,  myProjToView.GetDataPtr(),  sizeof(c.projToView));
		memcpy(c.worldToView, myWorldToView.GetDataPtr(), sizeof(c.worldToView));
		c.tileCountX = myTileCount.x; c.tileCountY = myTileCount.y;
		c.tilePx = kTilePx; c.zSlices = kZSlices;
		c.nearP = myNear; c.farP = myFar;
		c.lightCount = (uint32_t)myLightCount; c.maxPerCluster = kMaxPerCluster;
		c.screenX = (float)myResolution.x; c.screenY = (float)myResolution.y;
		myClusterCb.Update(DX11::Rhi()->GetContext(), c);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	{
		rhi::ComputePipelineDesc pd;
		pd.cs = myClusterCS->module;
		ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
	}
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myLightBuffer.Srv());
	myClusterCb.Bind(ctx);   // CS b0
	const rhi::UavHandle uavs[2] = { myClusterIndexBuffer.Uav(), myClusterCountBuffer.Uav() };
	ctx.SetUnorderedAccesses(0, 2, uavs);

	ctx.Dispatch((myNumClusters + 63) / 64, 1, 1);

	const rhi::UavHandle nullUavs[2] = {};
	ctx.SetUnorderedAccesses(0, 2, nullUavs);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::BuildFrame(RenderGraph& aGraph,
                                 const std::function<void()>& aDrawOpaque,
                                 const std::function<void()>& aDrawTransparent,
                                 const std::function<void(const Camera&)>& aDrawShadowCasters,
                                 int aDebugChannel)
{
	// Full DXR renderer: the camera RayQuery pass is the sole producer of HDR
	// radiance. Do this before any G-buffer, CSM, local-shadow-atlas, SSAO, SSR,
	// or transparent raster work is scheduled, so no raster lighting input can
	// silently influence the image.
	if (IsDxrRenderer())
	{
		aGraph.AddPass("dxrRenderer", [this](RenderGraph&) { RenderDxrSmokeTest(); });
		aGraph.AddPass("dxrRendererToHdr", [this](RenderGraph&) { ResolveDxrSmokeToHdr(); });
		aGraph.AddPass("atmosphere", [this](RenderGraph&) { RenderAtmosphere(); });
		if (IsPostFx())
			aGraph.AddPass("postfx", [this](RenderGraph&) { RenderPostFx(); });
		aGraph.AddPass("composite", [this](RenderGraph&)
		{
			DX11::BackBuffer->SetAsActiveTarget();
			Composite();
		});
		return;
	}

	ResetTemporalHistory(); // raster frames do not produce the DXR temporal inputs
	if (IsShadows() && aDrawShadowCasters && aDebugChannel == 0)
	{
		aGraph.AddPass("shadows", [this, aDrawShadowCasters](RenderGraph&)
		{
			RenderShadows(aDrawShadowCasters);
		});
	}

	if (aDrawShadowCasters && (aDebugChannel == 0 || aDebugChannel == 9))
	{
		aGraph.AddPass("localShadows", [this, aDrawShadowCasters](RenderGraph&)
		{
			RenderLocalShadows(aDrawShadowCasters);
		});
	}

	aGraph.AddPass("geometry", [this, aDrawOpaque](RenderGraph&)
	{
		BeginGeometryPass();
		if (aDrawOpaque) aDrawOpaque();
	});
	// DXR produces its opaque scene directly into HDR, but authored transparent
	// meshes still need the same post-ray forward composite as the raster path.
	// Keep this in one helper so the two frame paths cannot silently diverge.
	const auto addTransparentPass = [this, aDrawTransparent, &aGraph]()
	{
		if (!aDrawTransparent) return;
		aGraph.AddPass("transparent", [this, aDrawTransparent](RenderGraph&)
		{
			// Glass must never sample the HDR texture currently bound as its render
			// target. Snapshot the opaque HDR scene, then blend over it while using
			// the opaque depth buffer as a read-only visibility test.
			rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
			SetTargets(ctx, { myOpaqueHdr.GetRtv() }, {}, myResolution);
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, myHdr.GetSrv());
			BindFullscreen(mySceneCopyPs);
			ctx.Draw(3, 0);
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, {});
			SetTargets(ctx, { myHdr.GetRtv() }, DX11::DepthBuffer->GetDsv(), myResolution);
			auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
			gss.SetBlendState(BlendState::AlphaBlend);
			gss.SetDepthStencilState(DepthStencilState::ReadOnlyLessOrEqual);
			gss.SetCustomShaderParameters({ myTunables.glassIor, myTunables.glassRefractionScale,
				myTunables.glassThickness, myTunables.glassAbsorption });
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, myOpaqueHdr.GetSrv());
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 6, DX11::DepthBuffer->GetSrv());
			aDrawTransparent();
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, {});
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 6, {});
			gss.SetCustomShaderParameters({ 0.f, 0.f, 0.f, 0.f });
			gss.SetBlendState(BlendState::Disabled);
			gss.SetDepthStencilState(DepthStencilState::WriteLess);
		});
	};
	if (IsDxrSunShadows()) aGraph.AddPass("dxrSunShadows", [this](RenderGraph&) { RenderDxrSunShadows(); });

	// DXR Stage-3 validation: independent of the debug-channel system (reads
	// no G-buffer channel, writes its own diagnostic texture) -- runs
	// whenever toggled on, debug channel or not. See SetDxrSmokeTest's
	// comment for why this always sees the CURRENT frame's TLAS.
	if (IsDxrSmokeTest())
	{
		aGraph.AddPass("dxrSmokeTest", [this](RenderGraph&) { RenderDxrSmokeTest(); });

		if (IsDxrSmokeFullscreen())
		{
			aGraph.AddPass("dxrSmokeToHdr", [this](RenderGraph&)
			{
				ResolveDxrSmokeToHdr();
			});
			addTransparentPass();
			aGraph.AddPass("atmosphere", [this](RenderGraph&) { RenderAtmosphere(); });
			if (IsPostFx())
				aGraph.AddPass("postfx", [this](RenderGraph&) { RenderPostFx(); });
			aGraph.AddPass("composite", [this](RenderGraph&)
			{
				DX11::BackBuffer->SetAsActiveTarget();
				Composite();
			});
			return;
		}
	}

	// SSAO produces the AO texture that both lighting and the debug channel 8 read.
	if (IsSSAO() && (aDebugChannel == 0 || aDebugChannel == 8))
	{
		aGraph.AddPass("ssao", [this](RenderGraph&) { RenderSSAO(); });
	}

	if (aDebugChannel > 0)
	{
		aGraph.AddPass("gbufDebug", [this, aDebugChannel](RenderGraph&)
		{
			DX11::BackBuffer->SetAsActiveTarget();
			DebugBlit(aDebugChannel);
		});
		return;
	}

	if (IsClustered())
	{
		aGraph.AddPass("clusters", [this](RenderGraph&) { CullClusters(); });
	}

	aGraph.AddPass("lighting", [this](RenderGraph&)
	{
		ResolveLighting();
	});

	if (IsSSR())
	{
		aGraph.AddPass("ssr", [this](RenderGraph&) { RenderSSR(); });
	}

	addTransparentPass();

	aGraph.AddPass("atmosphere", [this](RenderGraph&) { RenderAtmosphere(); });
	if (IsPostFx())
	{
		aGraph.AddPass("postfx", [this](RenderGraph&) { RenderPostFx(); });
	}

	aGraph.AddPass("composite", [this](RenderGraph&)
	{
		DX11::BackBuffer->SetAsActiveTarget();
		Composite();
	});
}

void DeferredRenderer::BeginGeometryPass()
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	const rhi::RtvHandle rtvs[4] = {
		myAlbedo.GetRtv(), myNormal.GetRtv(), myMaterial.GetRtv(), myEmissive.GetRtv(),
	};

	const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
	for (rhi::RtvHandle rtv : rtvs)
		ctx.ClearRenderTarget(rtv, clear);
	DX11::DepthBuffer->Clear();

	SetTargets(ctx, { rtvs[0], rtvs[1], rtvs[2], rtvs[3] }, DX11::DepthBuffer->GetDsv(), myResolution);
}

void DeferredRenderer::BindGBufferSrvs()
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	const rhi::SrvHandle srvs[5] = {
		myAlbedo.GetSrv(), myNormal.GetSrv(), myMaterial.GetSrv(), myEmissive.GetSrv(),
		DX11::DepthBuffer->GetSrv(),
	};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 10, 5, srvs);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);

	// Deferred structured light buffer (t15) + count (b6).
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 15, myLightBuffer.Srv());
	myLightParamsCb.Bind(ctx);   // b6

	// Clustered light grid (t16 indices, t17 counts) + params (b7).
	const bool clustered = IsClustered() && myClusterIndexBuffer.IsValid() && myClusterCountBuffer.IsValid();
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 16, clustered ? myClusterIndexBuffer.Srv() : rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 17, clustered ? myClusterCountBuffer.Srv() : rhi::SrvHandle{});
	myClusterCb.Bind(ctx, rhi::ShaderStage::Pixel, 7);   // also bound at CS b0

	// Blurred SSAO (t18). gSsaoEnabled in b6 tells the shader whether to read it.
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 18, IsSSAO() ? myAo.GetSrv() : rhi::SrvHandle{});

	// Shadow cascades (t19, cmp sampler s2, params b9). gShadowEnabled gates use.
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 19, IsShadows() ? myShadowSrv : rhi::SrvHandle{});
	if (myShadowCmpSampler.IsValid())
		ctx.SetSampler(rhi::ShaderStage::Pixel, 2, myShadowCmpSampler);
	myShadowCb.Bind(ctx);   // b9

	// Box-projected reflection probe (b12), read by EvaluateAmbiance.
	myProbeCb.Bind(ctx);   // b12

	// Emissive-GI irradiance volume (b13 params, t22 SH buffer).
	myGiVolumeCb.Bind(ctx);   // b13
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 22, HasGi() ? myGiShBuffer.Srv() : rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 23, IsDxrSunShadows() ? myDxrShadowSrv : rhi::SrvHandle{});

	// Point / spot shadow atlas (t20 transforms, t21 depth). Shared s2 cmp sampler.
	const bool localSh = IsLocalShadows();
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 20, localSh ? myLocalShadowBuffer.Srv() : rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 21, localSh ? myLocalAtlasSrv : rhi::SrvHandle{});
}

void DeferredRenderer::UnbindGBufferSrvs()
{
	const rhi::SrvHandle nulls[14] = {};
	DX11::Rhi()->GetContext().SetShaderResources(rhi::ShaderStage::Pixel, 10, 14, nulls);   // t10..t23
}

void DeferredRenderer::BindFullscreen(const PixelShader* aPixelShader)
{
	GraphicsEngine::GetInstance()->GetGraphicsStateStack().UpdateGpuStates();

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.SetPrimitiveTopology(rhi::Topology::TriangleList);
	ctx.SetInputLayout({}, nullptr, 0);
	ctx.SetVertexBuffer(0, {}, 0, 0);
	ctx.SetIndexBuffer({}, rhi::Format::R32_UInt, 0);
	ctx.SetVertexShader(myFullscreenVs->module);
	ctx.SetPixelShader(aPixelShader->module);
}

void DeferredRenderer::ResolveLighting()
{
	// MRT0 = HDR radiance, MRT1 = probe IBL specular (SSR resolve subtracts it).
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	const float zero[4] = { 0.f, 0.f, 0.f, 0.f };
	ctx.ClearRenderTarget(myIblSpecTex.GetRtv(), zero);
	SetTargets(ctx, { myHdr.GetRtv(), myIblSpecTex.GetRtv() }, {}, myResolution);

	BindGBufferSrvs();
	BindFullscreen(myLightingPs);
	ctx.Draw(3, 0);
	UnbindGBufferSrvs();

	ctx.SetRenderTargets(0, nullptr, {});
}

void DeferredRenderer::PostFxFullscreen(const PixelShader* aPs, RenderTarget& aDst, Vector2ui aDstSize,
                                       const rhi::SrvHandle* aSrvs, int aSrvCount,
                                       Vector2f aSrcTexel, bool aAdditive)
{
	const Tunables& t = myTunables;
	{
		PostFxCb c{};
		c.texelSize[0] = aSrcTexel.x; c.texelSize[1] = aSrcTexel.y;
		c.bloomThreshold = t.bloomThreshold;
		c.bloomKnee = t.bloomKnee;
		c.bloomIntensity = t.bloomIntensity;
		c.exposureKeyOrManual = t.exposureAuto ? t.exposureKey : t.manualExposure;
		c.exposureMin = t.exposureMin;
		c.exposureMax = t.exposureMax;
		c.exposureAuto = t.exposureAuto ? 1.f : 0.f;
		c.exposureComp = t.exposureComp;
		c.adaptRate = t.exposureSpeed;
		c.deltaTime = std::min(Application::GetInstance()->GetDeltaTime(), 0.1f);
		myPostFxCb.Update(DX11::Rhi()->GetContext(), c);
	}

	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(aAdditive ? BlendState::AdditiveBlend : BlendState::Disabled);

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	SetTargets(ctx, { aDst.GetRtv() }, {}, aDstSize);

	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, (uint32_t)aSrvCount, aSrvs);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(ctx);   // b10

	BindFullscreen(aPs);
	ctx.Draw(3, 0);

	const rhi::SrvHandle nulls[4] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, (uint32_t)(aSrvCount < 4 ? aSrvCount : 4), nulls);
	gss.SetBlendState(BlendState::Disabled);
}

bool DeferredRenderer::CreateAtmosphereTargetSet(Vector2ui aResolution, AtmosphereTargetSet& aSet)
{
	ReleaseAtmosphereTargetSet(aSet);
	auto* dev = DX11::Rhi();
	aSet.hdr = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	rhi::TextureDesc td{};
	// The volume shader and the composite both derive the volume size as
	// ceil(target / 2), so it must match the target it is paired with exactly.
	td.width = (aResolution.x + 1) / 2; td.height = (aResolution.y + 1) / 2;
	td.format = rhi::Format::R16G16B16A16_Float;
	td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	td.debugName = "AtmosphereSunlightRender";
	aSet.volumeTex = dev->CreateTexture(td);
	if (aSet.volumeTex.IsValid())
	{
		aSet.volumeSrv = dev->CreateSrv(aSet.volumeTex, rhi::SrvDesc{});
		aSet.volumeUav = dev->CreateUav(aSet.volumeTex, rhi::UavDesc{});
	}
	aSet.size = aResolution;
	return aSet.IsValid();
}

void DeferredRenderer::ReleaseAtmosphereTargetSet(AtmosphereTargetSet& aSet)
{
	auto* dev = DX11::Rhi();
	if (aSet.volumeUav.IsValid()) dev->Destroy(aSet.volumeUav);
	if (aSet.volumeSrv.IsValid()) dev->Destroy(aSet.volumeSrv);
	if (aSet.volumeTex.IsValid()) dev->Destroy(aSet.volumeTex);
	aSet = AtmosphereTargetSet{};
}

bool DeferredRenderer::CreateAtmosphereTargets(Vector2ui resolution)
{
	auto* dev = DX11::Rhi();
	myAtmosphereHdr = RenderTarget::Create(resolution, rhi::Format::R16G16B16A16_Float);
	if (myVolumeUav.IsValid()) dev->Destroy(myVolumeUav);
	if (myVolumeSrv.IsValid()) dev->Destroy(myVolumeSrv);
	if (myVolumeTex.IsValid()) dev->Destroy(myVolumeTex);
	myVolumeUav = {}; myVolumeSrv = {}; myVolumeTex = {};
	rhi::TextureDesc td{};
	td.width = (resolution.x + 1) / 2; td.height = (resolution.y + 1) / 2;
	td.format = rhi::Format::R16G16B16A16_Float;
	td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	td.debugName = "AtmosphereSunlight";
	myVolumeTex = dev->CreateTexture(td);
	if (myVolumeTex.IsValid()) {
		myVolumeSrv = dev->CreateSrv(myVolumeTex, rhi::SrvDesc{});
		myVolumeUav = dev->CreateUav(myVolumeTex, rhi::UavDesc{});
	}
	return myAtmosphereHdr.GetSrv().IsValid() && myVolumeSrv.IsValid() && myVolumeUav.IsValid();
}

bool DeferredRenderer::RenderAtmosphere(bool beforeTemporal, bool aRenderResolution)
{
	const auto& t = myTunables;
	// Two output domains. Display resolution writes into myAtmosphereHdr and
	// swaps it into myHdr, as before. Render resolution (DLSS upscaling) writes
	// into myAtmosphereRender, whose image then becomes DLSS's colour input;
	// every fog input in that mode (ray depth, the ray output) is already at
	// render resolution, and the shaders derive all their pixel maths from
	// FogWidth/FogHeight, so the only thing that changes is the extent.
	const Vector2ui extent = aRenderResolution ? myDxrRenderResolution : myResolution;
	const bool targetsValid = aRenderResolution ? myAtmosphereRender.IsValid() : myAtmosphereHdr.GetSrv().IsValid();
	// Diagnostic images must remain unmodified. Fog has no temporal history of its own.
	if (!t.fogEnabled || (t.fogDensity <= 0.f && t.atmosphereDebugView == 0) || !myAtmospherePs || !myAtmosphereCb.IsValid() || !targetsValid
		|| (IsDxrSmokeFullscreen() && t.dxrLightingView != 0)
		|| (myTaaWasEnabled && t.taaDebugView != 0)) return false;
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	const bool rayDepth = IsDxrRenderer() || IsDxrSmokeFullscreen();
	if (rayDepth && !beforeTemporal) return false;
	if (aRenderResolution && !rayDepth) return false; // only the DXR path renders below display resolution
	const rhi::UavHandle volumeUav = aRenderResolution ? myAtmosphereRender.volumeUav : myVolumeUav;
	const rhi::SrvHandle volumeSrv = aRenderResolution ? myAtmosphereRender.volumeSrv : myVolumeSrv;
	const bool rayVolume = rayDepth && myVolumeRTCS && dev->BindRaytracingSceneForCompute();
	const ComputeShader* volume = rayDepth ? myVolumeRTCS : myVolumeCS;
	const bool volumeActive = t.volumetricEnabled && t.volumetricStrength > 0 && t.fogDensity > 0 && volume
		&& t.dxrSunIntensity * (t.dxrSunTint[0] + t.dxrSunTint[1] + t.dxrSunTint[2]) > 0.f
		&& volumeUav.IsValid() && (rayDepth ? rayVolume : (IsShadows() && myAtmosphereShadowCameraCb.IsValid()));
	AtmosphereCb cb{};
	const Matrix4x4f clipToWorld = myProjToView * Matrix4x4f::GetFastInverse(myWorldToView);
	std::memcpy(cb.clipToWorld, clipToWorld.GetDataPtr(), 64);
	cb.camera[0]=myCameraPos.x; cb.camera[1]=myCameraPos.y; cb.camera[2]=myCameraPos.z;
	const float sunLength = std::max(0.00001f, myShadowLightDir.Length());
	cb.sunDirection[0]=-myShadowLightDir.x/sunLength;
	cb.sunDirection[1]=-myShadowLightDir.y/sunLength;
	cb.sunDirection[2]=-myShadowLightDir.z/sunLength;
	for (int i=0;i<3;++i) {
		cb.sunRadiance[i]=std::max(0.f,t.dxrSunTint[i]*t.dxrSunIntensity);
		cb.fogColor[i]=std::max(0.f,t.fogColor[i]);
	}
	cb.density=std::clamp(t.fogDensity,0.f,0.05f);
	cb.heightFalloff=std::clamp(t.fogHeightFalloff,0.f,0.2f);
	cb.baseHeight=t.fogBaseHeight; cb.startDistance=std::max(0.f,t.fogStartDistance);
	cb.maxDistance=std::clamp(t.fogMaxDistance,1.f,1000.f);
	cb.volumeDistance=std::clamp(t.volumetricDistance,1.f,300.f);
	cb.volumeStrength=std::clamp(t.volumetricStrength,0.f,2.f);
	cb.anisotropy=std::clamp(t.volumetricAnisotropy,0.f,0.8f);
	cb.width=extent.x; cb.height=extent.y;
	cb.steps=uint32_t(std::clamp(t.volumetricSteps,8,64));
	cb.affectSky=t.fogAffectSky ? 1u:0u;
	cb.volumeEnabled=volumeActive ? 1u:0u; cb.debugView=uint32_t(t.atmosphereDebugView);
	cb.sunDiskAngularRadius=std::clamp(t.sunDiskAngularRadius, 0.001f, 0.08f);
	cb.sunDiskIntensity=std::max(0.f,t.sunDiskIntensity);
	cb.sunDiskEnabled=t.sunDiskEnabled ? 1u : 0u;
	if (rayDepth && myTaaWasEnabled) { cb.jitter[0]=-myTaaJitter.x; cb.jitter[1]=-myTaaJitter.y; }
	myAtmosphereCb.Update(ctx,cb);
	const rhi::SrvHandle depth = rayDepth ? myTemporalSrv[1] : DX11::DepthBuffer->GetSrv();
	ctx.SetRenderTargets(0,nullptr,{});
	if (volumeActive) {
		rhi::ComputePipelineDesc pd; pd.cs=volume->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		myAtmosphereCb.Bind(ctx);
		ctx.SetShaderResource(rhi::ShaderStage::Compute,4,depth);
		if (rayDepth) {
			ctx.SetShaderResource(rhi::ShaderStage::Compute,0,RayTracingMaterialTable::Upload(*dev,ctx));
			ctx.SetSampler(rhi::ShaderStage::Compute,0,myDxrSmokeSampler);
		}
		if (!rayDepth) {
			myAtmosphereShadowCameraCb.Update(ctx,myWorldToView.GetDataPtr(),64);
			myAtmosphereShadowCameraCb.Bind(ctx);
			myShadowCb.Bind(ctx,rhi::ShaderStage::Compute,9);
			ctx.SetShaderResource(rhi::ShaderStage::Compute,1,myShadowSrv);
			ctx.SetSampler(rhi::ShaderStage::Compute,2,myShadowCmpSampler);
		}
		ctx.SetUnorderedAccess(0,volumeUav);
		ctx.Dispatch(((extent.x+1)/2+7)/8,((extent.y+1)/2+7)/8,1);
		ctx.SetUnorderedAccess(0,{});
		const rhi::SrvHandle nulls[5]={}; ctx.SetShaderResources(rhi::ShaderStage::Compute,0,5,nulls);
		ctx.SetComputePipeline({});
	}
	auto& gss=GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(BlendState::Disabled);
	SetTargets(ctx,{aRenderResolution ? myAtmosphereRender.hdr.GetRtv() : myAtmosphereHdr.GetRtv()},{},extent);
	const rhi::SrvHandle inputs[]={beforeTemporal ? myDxrSmokeTestSrv : myHdr.GetSrv(),volumeActive ? volumeSrv : rhi::SrvHandle{}};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel,1,2,inputs);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel,4,depth);
	ctx.SetSampler(rhi::ShaderStage::Pixel,3,myLinearSampler);
	myAtmosphereCb.Bind(ctx,rhi::ShaderStage::Pixel,8);
	BindFullscreen(myAtmospherePs); ctx.Draw(3,0);
	const rhi::SrvHandle nulls[5]={}; ctx.SetShaderResources(rhi::ShaderStage::Pixel,0,5,nulls);
	ctx.SetRenderTargets(0,nullptr,{});
	if (!aRenderResolution) std::swap(myHdr,myAtmosphereHdr);
	return true;
}

void DeferredRenderer::RenderPostFx()
{
	if (!IsPostFx()) return;

	if (!myExposureCleared)
	{
		myExposure[0].Clear({ 0, 0, 0, 0 });
		myExposure[1].Clear({ 0, 0, 0, 0 });
		myExposureCleared = true;
	}

	const Vector2f hdrTexel{ 1.f / (float)myResolution.x, 1.f / (float)myResolution.y };

	// --- bloom: prefilter HDR -> mip[0], downsample chain, additive tent upsample ---
	if (myTunables.bloomEnabled)
	{
		rhi::SrvHandle src = myHdr.GetSrv();
		PostFxFullscreen(myBloomPrefilterPs, myBloomMip[0], myBloomSize[0], &src, 1, hdrTexel);

		for (int i = 1; i < kBloomMips; ++i)
		{
			rhi::SrvHandle s = myBloomMip[i - 1].GetSrv();
			const Vector2f texel{ 1.f / (float)myBloomSize[i - 1].x, 1.f / (float)myBloomSize[i - 1].y };
			PostFxFullscreen(myBloomDownPs, myBloomMip[i], myBloomSize[i], &s, 1, texel);
		}

		for (int i = kBloomMips - 2; i >= 0; --i)
		{
			rhi::SrvHandle s = myBloomMip[i + 1].GetSrv();
			const Vector2f texel{ 1.f / (float)myBloomSize[i + 1].x, 1.f / (float)myBloomSize[i + 1].y };
			PostFxFullscreen(myBloomUpPs, myBloomMip[i], myBloomSize[i], &s, 1, texel, /*additive*/ true);
		}
	}
	else
	{
		myBloomMip[0].Clear({ 0, 0, 0, 0 });
	}

	// --- auto exposure: HDR -> 64x64 log-luma -> ... -> 1x1 -> temporal adapt ---
	if (myTunables.exposureAuto)
	{
		const unsigned expSizes[7] = { 64, 32, 16, 8, 4, 2, 1 };
		rhi::SrvHandle hdrSrv = myHdr.GetSrv();
		PostFxFullscreen(myExposureLumaPs, myExpMip[0], { 64, 64 }, &hdrSrv, 1, hdrTexel);
		for (int i = 1; i < 7; ++i)
		{
			rhi::SrvHandle s = myExpMip[i - 1].GetSrv();
			const float p = 1.f / (float)expSizes[i - 1];
			PostFxFullscreen(myExposureDownPs, myExpMip[i], { expSizes[i], expSizes[i] }, &s, 1, { p, p });
		}

		const int dst = 1 - myExposureSrc;
		const rhi::SrvHandle adaptSrvs[2] = {
			myExpMip[6].GetSrv(),
			myExposure[myExposureSrc].GetSrv(),
		};
		PostFxFullscreen(myExposureAdaptPs, myExposure[dst], { 1, 1 }, adaptSrvs, 2, { 1.f, 1.f });
		myExposureSrc = dst;
	}
}

void DeferredRenderer::Composite()
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();

	if (!IsPostFx())
	{
		// Fallback: HDR -> engine tonemap -> currently bound target (samples t1).
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, myHdr.GetSrv());
		GraphicsEngine::GetInstance()->GetFullscreenEffectTonemap().Render();
		ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, {});
		return;
	}

	// HDR * exposure + bloom, then ACES tonemap -> backbuffer (already bound).
	{
		const Tunables& t = myTunables;
		PostFxCb c{};
		c.texelSize[0] = 1.f / (float)myResolution.x;
		c.texelSize[1] = 1.f / (float)myResolution.y;
		c.bloomThreshold = t.bloomThreshold;
		c.bloomKnee = t.bloomKnee;
		c.bloomIntensity = t.bloomEnabled ? t.bloomIntensity : 0.f;
		c.exposureKeyOrManual = t.exposureAuto ? t.exposureKey : t.manualExposure;
		c.exposureMin = t.exposureMin;
		c.exposureMax = t.exposureMax;
		c.exposureAuto = t.exposureAuto ? 1.f : 0.f;
		c.exposureComp = t.exposureComp;
		c.adaptRate = t.exposureSpeed;
		c.deltaTime = std::min(Application::GetInstance()->GetDeltaTime(), 0.1f);
		myPostFxCb.Update(DX11::Rhi()->GetContext(), c);
	}

	const rhi::SrvHandle srvs[3] = {
		myHdr.GetSrv(),
		myBloomMip[0].GetSrv(),
		myExposure[myExposureSrc].GetSrv(),
	};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 3, srvs);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(ctx);   // b10

	BindFullscreen(myCompositePs);
	ctx.Draw(3, 0);

	const rhi::SrvHandle nulls[3] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 3, nulls);
}

void DeferredRenderer::ResolveDxrSmokeToHdr()
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	const bool superResolution = myTunables.dlssMode >= 2 && StreamlineDLSS::Get().IsAvailable();
	// Fog is applied before the temporal/DLSS resolve in both modes. When DLSS
	// upscales, it is applied at render resolution and becomes part of the image
	// DLSS reconstructs. This used to be skipped entirely under upscaling on the
	// grounds that fog belongs after DLSS at display resolution -- but the pass
	// meant to do that returns immediately for ray depth, so fog was simply
	// never drawn in any DLSS super-resolution mode.
	const bool atmosphereApplied = RenderAtmosphere(true, superResolution);
	const RenderTarget& foggedTarget = superResolution ? myAtmosphereRender.hdr : myHdr;
	rhi::SrvHandle resolvedSrv = atmosphereApplied ? foggedTarget.GetSrv() : myDxrSmokeTestSrv;
	const bool dlaaRequested = (myTunables.dlssMode > 0 || myTunables.dlaaEnabled || myTunables.rayReconstructionEnabled) && myTunables.dxrLightingView == 0 && StreamlineDLSS::Get().IsAvailable();
	const bool rrRequested = myTunables.rayReconstructionEnabled && !superResolution && StreamlineDLSS::Get().IsRayReconstructionAvailable();
	bool dlaaResolved = false;
	if (dlaaRequested && myDlaaTex.IsValid())
	{
		const rhi::TextureHandle color = atmosphereApplied ? foggedTarget.GetTextureHandle() : myDxrSmokeTex;
		// Streamline validates every supplied state.  Make the RHI perform the
		// transitions, then pass those exact D3D12 states in the resource tags.
		ctx.TransitionResource(color, rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myTemporalTex[1], rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myTemporalTex[0], rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myTemporalTex[3], rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myTemporalTex[4], rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myTemporalTex[5], rhi::ResourceState::NonPixelShaderResource);
		ctx.TransitionResource(myDlaaTex, rhi::ResourceState::UnorderedAccess);

		const Matrix4x4f worldToClip = myWorldToView * myViewToProj;
		const Matrix4x4f previousToWorld = myPreviousWorldToClip.GetInverse();
		const Matrix4x4f clipToPrevious = worldToClip.GetInverse() * myPreviousWorldToClip;
		const Matrix4x4f previousToClip = previousToWorld * worldToClip;
		if (rrRequested)
		{
			const Matrix4x4f viewToWorld = myWorldToView.GetInverse();
			dlaaResolved = StreamlineDLSS::Get().EvaluateRayReconstruction(
				DX11::Rhi()->GetNativeCommandList(), DX11::Rhi()->GetNativeTexture(color), DX11::Rhi()->GetNativeTexture(myDlaaTex),
				DX11::Rhi()->GetNativeTexture(myTemporalTex[1]), DX11::Rhi()->GetNativeTexture(myTemporalTex[0]), DX11::Rhi()->GetNativeTexture(myTemporalTex[3]),
				DX11::Rhi()->GetNativeTexture(myTemporalTex[4]), DX11::Rhi()->GetNativeTexture(myTemporalTex[5]), myResolution.x, myResolution.y,
				myTaaFrameIndex, myViewToProj.GetDataPtr(), myProjToView.GetDataPtr(), myWorldToView.GetDataPtr(), viewToWorld.GetDataPtr(),
				clipToPrevious.GetDataPtr(), previousToClip.GetDataPtr(), myCameraTransform.GetDataPtr(), myNear, myFar, myTaaJitter.x, myTaaJitter.y,
				!myTaaHistoryValid || myTaaLightingChanged);
			// Streamline validates every resource and state it is handed and
			// simply returns false when something does not line up, which then
			// falls through to the native temporal path and looks like "the
			// denoiser did nothing" rather than an error. Say which happened,
			// once, so it is never ambiguous whether RR is actually running.
			static int sRrReported = -1;
			const int rrState = dlaaResolved ? 1 : 0;
			if (sRrReported != rrState)
			{
				sRrReported = rrState;
				if (dlaaResolved) INFO_PRINT("DXR denoiser: DLSS Ray Reconstruction active (%ux%u)", myResolution.x, myResolution.y);
				else ERROR_PRINT("DXR denoiser: Ray Reconstruction requested but Streamline declined it; falling back to the native temporal resolve");
			}
		}
		else dlaaResolved = StreamlineDLSS::Get().EvaluateDLSS(
			DX11::Rhi()->GetNativeCommandList(), DX11::Rhi()->GetNativeTexture(color), DX11::Rhi()->GetNativeTexture(myDlaaTex), DX11::Rhi()->GetNativeTexture(myTemporalTex[1]),
			DX11::Rhi()->GetNativeTexture(myTemporalTex[0]), myDxrRenderResolution.x, myDxrRenderResolution.y, myResolution.x, myResolution.y,
			myTunables.dlssMode > 0 ? myTunables.dlssMode : 1, myTaaFrameIndex, myViewToProj.GetDataPtr(), myProjToView.GetDataPtr(),
			clipToPrevious.GetDataPtr(), previousToClip.GetDataPtr(), myCameraTransform.GetDataPtr(), myNear, myFar, myTaaJitter.x, myTaaJitter.y,
			!myTaaHistoryValid || myTaaLightingChanged);
		if (dlaaResolved)
		{
			ctx.TransitionResource(myDlaaTex, rhi::ResourceState::PixelShaderResource);
			resolvedSrv = myDlaaSrv;
			myTaaHistoryValid = true;
			myPreviousTaaJitter = myTaaJitter;
			++myTaaFrameIndex;
		}
	}
	const bool resolveTemporal = !dlaaResolved && myTaaWasEnabled && myTemporalHistoryValid && (!myTunables.fogEnabled || myTunables.atmosphereDebugView == 0);
	if (resolveTemporal) {
		const uint32_t next = 1u - myTaaHistoryIndex;
		TaaCb cb{};
		cb.width = myResolution.x; cb.height = myResolution.y;
		cb.historyValid = (myTaaHistoryValid && !myTaaLightingChanged) ? 1u : 0u;
		cb.debugView = uint32_t(myTunables.taaDebugView);
		cb.jitter[0] = myTaaJitter.x; cb.jitter[1] = myTaaJitter.y;
		cb.previousJitter[0] = myPreviousTaaJitter.x; cb.previousJitter[1] = myPreviousTaaJitter.y;
		cb.nearPlane = myNear; cb.farPlane = myFar;
		const bool stationaryCoverage = myRaySceneStationary && myTaaCameraStationary;
		cb.stationaryCoverage = stationaryCoverage ? 1u : 0u;
		// `stationaryCoverage` is a bit field, not a boolean scene-state enum.
		// In particular, a moving emissive must use the normal, depth-clipped
		// history path rather than accidentally selecting the 0.975 static weight.
		cb.historyWeight = stationaryCoverage ? myTunables.taaStationaryWeight : myTunables.taaHistoryWeight;
		myTaaCb.Update(ctx,cb);
		rhi::ComputePipelineDesc pd; pd.cs = myTaaCS->module;
		ctx.SetComputePipeline(DX11::Rhi()->CreateComputePipeline(pd));
		myTaaCb.Bind(ctx);
		const rhi::SrvHandle inputs[] = {resolvedSrv,myTemporalSrv[1],myTemporalSrv[0],myTemporalSrv[2],myTaaSrv[myTaaHistoryIndex*2],myTaaSrv[myTaaHistoryIndex*2+1],myTemporalSrv[3],myTaaSrv[5+myTaaHistoryIndex]};
		ctx.SetShaderResources(rhi::ShaderStage::Compute,0,8,inputs);
		ctx.SetSampler(rhi::ShaderStage::Compute,0,myLinearSampler);
		ctx.SetUnorderedAccess(0,myTaaUav[next*2]);
		ctx.SetUnorderedAccess(1,myTaaUav[next*2+1]);
		ctx.SetUnorderedAccess(2,myTaaUav[4]);
		ctx.SetUnorderedAccess(3,myTaaUav[5+next]);
		ctx.Dispatch((myResolution.x+7)/8,(myResolution.y+7)/8,1);
		for (uint32_t i=0;i<4;++i) ctx.SetUnorderedAccess(i,{});
		const rhi::SrvHandle nulls[8] = {};
		ctx.SetShaderResources(rhi::ShaderStage::Compute,0,8,nulls);
		ctx.SetComputePipeline({});
		myTaaHistoryIndex = next;
		myTaaHistoryValid = true;
		resolvedSrv = myTunables.taaDebugView == 0 ? myTaaSrv[next*2] : myTaaSrv[4];
		myPreviousTaaJitter = myTaaJitter;
		++myTaaFrameIndex;
	} else myTaaHistoryValid = false;
	// The only case where the finished image is already in myHdr: display-
	// resolution fog was written there and nothing resolved it afterwards.
	// This used to be `atmosphereApplied && !resolveTemporal`, which is also
	// true when DLSS/DLAA/RR *did* resolve -- so with fog on, their output was
	// computed and then thrown away. It also skipped the lighting-changed reset
	// below, leaving that flag stuck after a light edit, which in turn kept
	// jitter off and forced a DLSS history reset every frame.
	if (atmosphereApplied && !superResolution && !resolveTemporal && !dlaaResolved)
	{
		myTaaLightingChanged = false;
		return;
	}
	const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
	ctx.ClearRenderTarget(myHdr.GetRtv(), clear);
	SetTargets(ctx, { myHdr.GetRtv() }, {}, myResolution);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, resolvedSrv);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 0, myDxrSmokeSampler);
	BindFullscreen(myDxrSmokeCopyPs);
	ctx.Draw(3, 0);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, rhi::SrvHandle{});
	// Light uploads happen before BuildFrame.  The flag is intentionally kept
	// through both the ray pass and this resolve, then becomes frame-local.
	myTaaLightingChanged = false;
}

void DeferredRenderer::DebugBlit(int aChannel)
{
	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetCustomShaderParameters({ (float)aChannel, 0.f, 0.f, 0.f });

	BindGBufferSrvs();
	BindFullscreen(myDebugPs);
	DX11::Rhi()->GetContext().Draw(3, 0);
	UnbindGBufferSrvs();

	gss.SetCustomShaderParameters({ 0.f, 0.f, 0.f, 0.f });
}
