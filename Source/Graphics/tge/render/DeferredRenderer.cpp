#include "stdafx.h"

#include <tge/render/DeferredRenderer.h>

#include <d3d11.h>

#include <algorithm>
#include <cmath>
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
#include <tge/graphics/Camera.h>
#include <tge/shaders/ModelShader.h>
#include <tge/render/RenderGraph.h>
#include <tge/render/RenderCommon.h>
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
	};

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

	Tga::Vector3f Lerp(const Tga::Vector3f& a, const Tga::Vector3f& b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
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
	myAlbedo   = RenderTarget::Create(aResolution, DXGI_FORMAT_R8G8B8A8_UNORM);
	myNormal   = RenderTarget::Create(aResolution, DXGI_FORMAT_R16G16B16A16_FLOAT);
	myMaterial = RenderTarget::Create(aResolution, DXGI_FORMAT_R8G8B8A8_UNORM);
	myEmissive = RenderTarget::Create(aResolution, DXGI_FORMAT_R11G11B10_FLOAT);
	myHdr      = RenderTarget::Create(aResolution, DXGI_FORMAT_R16G16B16A16_FLOAT);
	myAoRaw    = RenderTarget::Create(aResolution, DXGI_FORMAT_R8_UNORM);
	myAo       = RenderTarget::Create(aResolution, DXGI_FORMAT_R8_UNORM);
	// SSR marches + stores at half res; the resolve bilinearly upsamples it.
	mySsrRes   = { std::max(1u, aResolution.x / 2u), std::max(1u, aResolution.y / 2u) };
	mySsrTex    = RenderTarget::Create(mySsrRes, DXGI_FORMAT_R16G16B16A16_FLOAT);
	myIblSpecTex = RenderTarget::Create(aResolution, DXGI_FORMAT_R16G16B16A16_FLOAT);
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

	myFullscreenVs = DX11::LoadVertexShader("Shaders/PostprocessVS");
	myLightingPs   = DX11::LoadPixelShader("Shaders/DeferredLightingPS");
	myDebugPs      = DX11::LoadPixelShader("Shaders/DeferredDebugPS");
	if (!myFullscreenVs || !myLightingPs || !myDebugPs)
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
		if (!myGiShBuffer.IsValid())
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

			if (!myGiShBuffer.IsValid() || !myGiVolumeCb.IsValid() || !myGiProjectCb.IsValid() || !myGiLinearSampler.IsValid())
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
		myLocalAtlasSrv.Reset();
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

	myReady = true;
	INFO_PRINT("DeferredRenderer: %ux%u G-buffer ready", aResolution.x, aResolution.y);
	return true;
}

void DeferredRenderer::UploadLights(const DeferredLight* aLights, int aCount)
{
	myLightCount = aCount < 0 ? 0 : (aCount > kMaxLights ? kMaxLights : aCount);

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
	ID3D11ShaderResourceView* nulls[3] = {};

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

		ID3D11ShaderResourceView* srvs[2] = { myNormal.GetShaderResourceView(),
		                                      DX11::DepthBuffer->GetShaderResourceView() };
		DX11::Context->PSSetShaderResources(11, 1, &srvs[0]);
		DX11::Context->PSSetShaderResources(14, 1, &srvs[1]);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		mySsaoCb.Bind(ctx);

		BindFullscreen(mySsaoPs);
		DX11::LogDrawCall();
		DX11::Context->Draw(3, 0);
		DX11::Context->PSSetShaderResources(11, 1, nulls);
		DX11::Context->PSSetShaderResources(14, 1, nulls);
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

		ID3D11ShaderResourceView* rawSrv = myAoRaw.GetShaderResourceView();
		ID3D11ShaderResourceView* depSrv = DX11::DepthBuffer->GetShaderResourceView();
		DX11::Context->PSSetShaderResources(18, 1, &rawSrv);
		DX11::Context->PSSetShaderResources(14, 1, &depSrv);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		mySsaoBlurCb.Bind(ctx);

		BindFullscreen(mySsaoBlurPs);
		DX11::LogDrawCall();
		DX11::Context->Draw(3, 0);
		DX11::Context->PSSetShaderResources(18, 1, nulls);
		DX11::Context->PSSetShaderResources(14, 1, nulls);
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

void DeferredRenderer::ClearGi()
{
	if (!myGiShBuffer.Uav().IsValid()) return;
	const float z[4] = { 0.f, 0.f, 0.f, 0.f };
	DX11::Rhi()->GetContext().ClearUnorderedAccessFloat(myGiShBuffer.Uav(), z);
}

void DeferredRenderer::GiProjectProbe(ID3D11ShaderResourceView* aCubeSrv, int aProbeIndex, float aHysteresis, int aFaceRes)
{
	if (!HasGi() || !aCubeSrv || aProbeIndex < 0 || aProbeIndex >= kMaxGiProbes) return;

	{
		struct { uint32_t idx; float hyst; uint32_t faceRes; float pad; } p{
			(uint32_t)aProbeIndex, aHysteresis, (uint32_t)std::max(aFaceRes, 1), 0.f };
		myGiProjectCb.Update(DX11::Rhi()->GetContext(), p);
	}

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	DX11::Context->CSSetShader(myGiProjectCS->shader.Get(), nullptr, 0);
	DX11::Context->CSSetShaderResources(0, 1, &aCubeSrv);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myGiLinearSampler);
	myGiProjectCb.Bind(ctx);
	ctx.SetUnorderedAccess(0, myGiShBuffer.Uav());

	DX11::Context->Dispatch(1, 1, 1);

	ID3D11ShaderResourceView* ns = nullptr;
	ctx.SetUnorderedAccess(0, {});
	DX11::Context->CSSetShaderResources(0, 1, &ns);
	DX11::Context->CSSetShader(nullptr, nullptr, 0);
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
	ID3D11ShaderResourceView* nulls[6] = {};
	rhi::SamplerHandle lin = myLinearSampler.IsValid() ? myLinearSampler : myPointSampler;

	// --- pass 1: half-res ray-march -> mySsrTex ---
	{
		const float clr[4] = { 0.f, 0.f, 0.f, 0.f };
		ctx.ClearRenderTarget(mySsrTex.GetRtv(), clr);
		SetTargets(ctx, { mySsrTex.GetRtv() }, {}, mySsrRes);

		ID3D11ShaderResourceView* scene = myHdr.GetShaderResourceView();
		ID3D11ShaderResourceView* gb[3] = { myAlbedo.GetShaderResourceView(),
		                                    myNormal.GetShaderResourceView(),
		                                    myMaterial.GetShaderResourceView() };
		ID3D11ShaderResourceView* depth = DX11::DepthBuffer->GetShaderResourceView();
		DX11::Context->PSSetShaderResources(1, 1, &scene);
		DX11::Context->PSSetShaderResources(10, 3, gb);
		DX11::Context->PSSetShaderResources(14, 1, &depth);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 3, lin);
		mySsrCb.Bind(ctx);

		BindFullscreen(mySsrPs);
		DX11::LogDrawCall();
		DX11::Context->Draw(3, 0);

		DX11::Context->PSSetShaderResources(1, 1, nulls);
		DX11::Context->PSSetShaderResources(10, 3, nulls);
		DX11::Context->PSSetShaderResources(14, 1, nulls);
	}

	// --- pass 2: resolve SSR over the probe IBL, additively into HDR ---
	// SSRApplyPS outputs conf * (ssrRadiance - iblSpecular); additive blend then
	// does hdr += that, i.e. lerp(probe, ssr, conf) for the specular term.
	{
		auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
		gss.SetBlendState(BlendState::AdditiveBlend);

		SetTargets(ctx, { myHdr.GetRtv() }, {}, myResolution);

		ID3D11ShaderResourceView* srvs2[2] = { mySsrTex.GetShaderResourceView(),
		                                       myIblSpecTex.GetShaderResourceView() };
		DX11::Context->PSSetShaderResources(0, 2, srvs2);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 1, myPointSampler);
		ctx.SetSampler(rhi::ShaderStage::Pixel, 3, lin);   // bilinear upsample of the half-res SSR

		BindFullscreen(mySsrApplyPs);
		DX11::LogDrawCall();
		DX11::Context->Draw(3, 0);

		DX11::Context->PSSetShaderResources(0, 2, nulls);
		gss.SetBlendState(BlendState::Disabled);
	}
}

bool DeferredRenderer::OnResize(Vector2ui aResolution)
{
	if (aResolution.x == 0 || aResolution.y == 0) return false;
	if (aResolution.x == myResolution.x && aResolution.y == myResolution.y) return true;
	if (!CreateTargets(aResolution)) return false;
	if (myClusterCS) CreateClusterBuffers(aResolution);
	if (myCompositePs) CreatePostFxTargets(aResolution);
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
	D3D11_TEXTURE2D_DESC td{};
	td.Width = td.Height = kShadowRes;
	td.MipLevels = 1;
	td.ArraySize = kNumCascades;
	td.Format = DXGI_FORMAT_R32_TYPELESS;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(DX11::Device->CreateTexture2D(&td, nullptr, myShadowTex.GetAddressOf())))
	{
		ERROR_PRINT("DeferredRenderer: shadow texture creation failed");
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
	sv.Format = DXGI_FORMAT_R32_FLOAT;
	sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	sv.Texture2DArray.MipLevels = 1;
	sv.Texture2DArray.ArraySize = kNumCascades;
	if (FAILED(DX11::Device->CreateShaderResourceView(myShadowTex.Get(), &sv, myShadowSrv.GetAddressOf())))
		return false;

	for (int i = 0; i < kNumCascades; ++i)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
		dv.Format = DXGI_FORMAT_D32_FLOAT;
		dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dv.Texture2DArray.FirstArraySlice = i;
		dv.Texture2DArray.ArraySize = 1;
		if (FAILED(DX11::Device->CreateDepthStencilView(myShadowTex.Get(), &dv, myShadowDsvs[i].GetAddressOf())))
			return false;
	}
	return true;
}

bool DeferredRenderer::CreateLocalShadowAtlas()
{
	D3D11_TEXTURE2D_DESC td{};
	td.Width = td.Height = kLocalAtlasRes;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R32_TYPELESS;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(DX11::Device->CreateTexture2D(&td, nullptr, myLocalAtlasTex.GetAddressOf())))
	{
		ERROR_PRINT("DeferredRenderer: local shadow atlas texture failed");
		return false;
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
	dv.Format = DXGI_FORMAT_D32_FLOAT;
	dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	if (FAILED(DX11::Device->CreateDepthStencilView(myLocalAtlasTex.Get(), &dv, myLocalAtlasDsv.GetAddressOf())))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
	sv.Format = DXGI_FORMAT_R32_FLOAT;
	sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	if (FAILED(DX11::Device->CreateShaderResourceView(myLocalAtlasTex.Get(), &sv, myLocalAtlasSrv.GetAddressOf())))
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
		myBloomMip[i]  = RenderTarget::Create(s, DXGI_FORMAT_R11G11B10_FLOAT);
		s = { std::max(1u, s.x / 2u), std::max(1u, s.y / 2u) };
	}

	const unsigned expSizes[7] = { 64, 32, 16, 8, 4, 2, 1 };
	for (int i = 0; i < 7; ++i)
		myExpMip[i] = RenderTarget::Create({ expSizes[i], expSizes[i] }, DXGI_FORMAT_R16_FLOAT);

	// Persistent 1x1 exposure ping-pong -- created once, survives resize.
	if (!myExposure[0].GetShaderResourceView())
	{
		myExposure[0] = RenderTarget::Create({ 1, 1 }, DXGI_FORMAT_R32_FLOAT);
		myExposure[1] = RenderTarget::Create({ 1, 1 }, DXGI_FORMAT_R32_FLOAT);
		myExposureCleared = false;
	}
	return true;
}

void DeferredRenderer::SetShadowLight(const Vector3f& aLightDir, const Vector3f& aSceneCenter, float aSceneRadius)
{
	myShadowLightDir = aLightDir;
	myShadowSceneCenter = aSceneCenter;
	myShadowSceneRadius = aSceneRadius > 1.f ? aSceneRadius : 1.f;
}

void DeferredRenderer::RenderShadows(const std::function<void(const Camera&)>& aDrawShadowCasters)
{
	if (!IsShadows() || !aDrawShadowCasters) return;

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

	// myShadowLightDir is L = the direction FROM a surface TOWARD the sun (matches
	// the shading shader). The shadow camera sits at the sun and looks the other
	// way, so its forward is -L.
	Vector3f fwd = { -myShadowLightDir.x, -myShadowLightDir.y, -myShadowLightDir.z };
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
		DX11::Context->ClearDepthStencilView(myShadowDsvs[c].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		ID3D11RenderTargetView* noRtv = nullptr;
		DX11::Context->OMSetRenderTargets(1, &noRtv, myShadowDsvs[c].Get());
		D3D11_VIEWPORT vp{ 0.f, 0.f, (float)kShadowRes, (float)kShadowRes, 0.f, 1.f };
		DX11::Context->RSSetViewports(1, &vp);

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
	struct Cand { int idx; float score; int tiles; bool spot; };
	std::vector<Cand> cands;
	cands.reserve(myLights.size());
	for (int i = 0; i < (int)myLights.size(); ++i)
	{
		const DeferredLight& L = myLights[i];
		if (L._pad[0] > 0.5f) continue;   // light opted out of shadow casting
		const bool spot = L.spotCosOuter > 0.0f;
		const Vector3f p{ L.position[0], L.position[1], L.position[2] };
		const Vector3f d{ p.x - myCameraPos.x, p.y - myCameraPos.y, p.z - myCameraPos.z };
		const float dist2 = d.x*d.x + d.y*d.y + d.z*d.z;
		const float lum = 0.2126f*L.color[0] + 0.7152f*L.color[1] + 0.0722f*L.color[2];
		if (lum <= 1e-4f) continue;
		const float score = lum / (1.0f + dist2 / std::max(L.range*L.range, 1.0f));
		cands.push_back({ i, score, spot ? 1 : 6, spot });
	}
	std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });

	std::vector<LocalShadowGpu> entries((size_t)kLocalTileCount);
	memset(entries.data(), 0, entries.size() * sizeof(LocalShadowGpu));

	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	const Camera savedCam = gss.GetCamera();

	ID3D11RenderTargetView* noRtv = nullptr;
	DX11::Context->ClearDepthStencilView(myLocalAtlasDsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	DX11::Context->OMSetRenderTargets(1, &noRtv, myLocalAtlasDsv.Get());

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

		D3D11_VIEWPORT vpr{ (float)(tx * kLocalTilePx), (float)(ty * kLocalTilePx),
		                    (float)kLocalTilePx, (float)kLocalTilePx, 0.0f, 1.0f };
		DX11::Context->RSSetViewports(1, &vpr);
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
	myViewToProj  = aCamera.GetProjection();
	myProjToView  = aCamera.GetProjection().GetInverse();
	myWorldToView = Matrix4x4f::GetFastInverse(aCamera.GetTransform());
	aCamera.GetProjectionPlanes(myNear, myFar);
	myCameraPos = aCamera.GetTransform().GetPosition();
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

	DX11::Context->CSSetShader(myClusterCS->shader.Get(), nullptr, 0);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myLightBuffer.Srv());
	myClusterCb.Bind(ctx);   // CS b0
	const rhi::UavHandle uavs[2] = { myClusterIndexBuffer.Uav(), myClusterCountBuffer.Uav() };
	ctx.SetUnorderedAccesses(0, 2, uavs);

	DX11::Context->Dispatch((myNumClusters + 63) / 64, 1, 1);

	const rhi::UavHandle nullUavs[2] = {};
	ctx.SetUnorderedAccesses(0, 2, nullUavs);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	DX11::Context->CSSetShader(nullptr, nullptr, 0);
}

void DeferredRenderer::BuildFrame(RenderGraph& aGraph,
                                 const std::function<void()>& aDrawOpaque,
                                 const std::function<void()>& aDrawTransparent,
                                 const std::function<void(const Camera&)>& aDrawShadowCasters,
                                 int aDebugChannel)
{
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

	if (aDrawTransparent)
	{
		aGraph.AddPass("transparent", [this, aDrawTransparent](RenderGraph&)
		{
			// Forward pass: alpha-blend into the lit HDR target, testing (but not
			// writing) the opaque depth so glass sorts behind walls / in front of
			// the floor. No back-to-front sort yet -- fine for the few glass panes.
			SetTargets(DX11::Rhi()->GetContext(), { myHdr.GetRtv() },
			           DX11::DepthBuffer->GetDsv(), myResolution);

			auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
			gss.SetBlendState(BlendState::AlphaBlend);
			gss.SetDepthStencilState(DepthStencilState::ReadOnlyLessOrEqual);

			aDrawTransparent();

			gss.SetBlendState(BlendState::Disabled);
			gss.SetDepthStencilState(DepthStencilState::WriteLess);
		});
	}

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

	ID3D11ShaderResourceView* srvs[5] = {
		myAlbedo.GetShaderResourceView(),
		myNormal.GetShaderResourceView(),
		myMaterial.GetShaderResourceView(),
		myEmissive.GetShaderResourceView(),
		DX11::DepthBuffer->GetShaderResourceView(),
	};
	DX11::Context->PSSetShaderResources(10, 5, srvs);
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
	ID3D11ShaderResourceView* aoSrv = IsSSAO() ? myAo.GetShaderResourceView() : nullptr;
	DX11::Context->PSSetShaderResources(18, 1, &aoSrv);

	// Shadow cascades (t19, cmp sampler s2, params b9). gShadowEnabled gates use.
	ID3D11ShaderResourceView* shadowSrv = IsShadows() ? myShadowSrv.Get() : nullptr;
	DX11::Context->PSSetShaderResources(19, 1, &shadowSrv);
	if (myShadowCmpSampler.IsValid())
		ctx.SetSampler(rhi::ShaderStage::Pixel, 2, myShadowCmpSampler);
	myShadowCb.Bind(ctx);   // b9

	// Box-projected reflection probe (b12), read by EvaluateAmbiance.
	myProbeCb.Bind(ctx);   // b12

	// Emissive-GI irradiance volume (b13 params, t22 SH buffer).
	myGiVolumeCb.Bind(ctx);   // b13
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 22, HasGi() ? myGiShBuffer.Srv() : rhi::SrvHandle{});

	// Point / spot shadow atlas (t20 transforms, t21 depth). Shared s2 cmp sampler.
	const bool localSh = IsLocalShadows();
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 20, localSh ? myLocalShadowBuffer.Srv() : rhi::SrvHandle{});
	ID3D11ShaderResourceView* atlasSrv = localSh ? myLocalAtlasSrv.Get() : nullptr;
	DX11::Context->PSSetShaderResources(21, 1, &atlasSrv);
}

void DeferredRenderer::UnbindGBufferSrvs()
{
	ID3D11ShaderResourceView* nulls[13] = {};
	DX11::Context->PSSetShaderResources(10, 13, nulls);   // t10..t22
}

void DeferredRenderer::BindFullscreen(const PixelShader* aPixelShader)
{
	GraphicsEngine::GetInstance()->GetGraphicsStateStack().UpdateGpuStates();

	DX11::Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DX11::Context->IASetInputLayout(nullptr);
	ID3D11Buffer* noBuffers[1] = { nullptr };
	UINT z0 = 0;
	DX11::Context->IASetVertexBuffers(0, 1, noBuffers, &z0, &z0);
	DX11::Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

	DX11::Context->VSSetShader(myFullscreenVs->shader.Get(), nullptr, 0);
	DX11::Context->GSSetShader(nullptr, nullptr, 0);
	DX11::Context->PSSetShader(aPixelShader->shader.Get(), nullptr, 0);
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
	DX11::LogDrawCall();
	DX11::Context->Draw(3, 0);
	UnbindGBufferSrvs();

	ctx.SetRenderTargets(0, nullptr, {});
}

void DeferredRenderer::PostFxFullscreen(const PixelShader* aPs, RenderTarget& aDst, Vector2ui aDstSize,
                                       ID3D11ShaderResourceView* const* aSrvs, int aSrvCount,
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

	SetTargets(DX11::Rhi()->GetContext(), { aDst.GetRtv() }, {}, aDstSize);

	DX11::Context->PSSetShaderResources(0, aSrvCount, aSrvs);
	DX11::Rhi()->GetContext().SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(DX11::Rhi()->GetContext());   // b10

	BindFullscreen(aPs);
	DX11::LogDrawCall();
	DX11::Context->Draw(3, 0);

	ID3D11ShaderResourceView* nulls[4] = {};
	DX11::Context->PSSetShaderResources(0, aSrvCount < 4 ? aSrvCount : 4, nulls);
	gss.SetBlendState(BlendState::Disabled);
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
		ID3D11ShaderResourceView* src = myHdr.GetShaderResourceView();
		PostFxFullscreen(myBloomPrefilterPs, myBloomMip[0], myBloomSize[0], &src, 1, hdrTexel);

		for (int i = 1; i < kBloomMips; ++i)
		{
			ID3D11ShaderResourceView* s = myBloomMip[i - 1].GetShaderResourceView();
			const Vector2f texel{ 1.f / (float)myBloomSize[i - 1].x, 1.f / (float)myBloomSize[i - 1].y };
			PostFxFullscreen(myBloomDownPs, myBloomMip[i], myBloomSize[i], &s, 1, texel);
		}

		for (int i = kBloomMips - 2; i >= 0; --i)
		{
			ID3D11ShaderResourceView* s = myBloomMip[i + 1].GetShaderResourceView();
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
		ID3D11ShaderResourceView* hdrSrv = myHdr.GetShaderResourceView();
		PostFxFullscreen(myExposureLumaPs, myExpMip[0], { 64, 64 }, &hdrSrv, 1, hdrTexel);
		for (int i = 1; i < 7; ++i)
		{
			ID3D11ShaderResourceView* s = myExpMip[i - 1].GetShaderResourceView();
			const float p = 1.f / (float)expSizes[i - 1];
			PostFxFullscreen(myExposureDownPs, myExpMip[i], { expSizes[i], expSizes[i] }, &s, 1, { p, p });
		}

		const int dst = 1 - myExposureSrc;
		ID3D11ShaderResourceView* adaptSrvs[2] = {
			myExpMip[6].GetShaderResourceView(),
			myExposure[myExposureSrc].GetShaderResourceView(),
		};
		PostFxFullscreen(myExposureAdaptPs, myExposure[dst], { 1, 1 }, adaptSrvs, 2, { 1.f, 1.f });
		myExposureSrc = dst;
	}
}

void DeferredRenderer::Composite()
{
	if (!IsPostFx())
	{
		// Fallback: HDR -> engine tonemap -> currently bound target (samples t1).
		ID3D11ShaderResourceView* hdr = myHdr.GetShaderResourceView();
		DX11::Context->PSSetShaderResources(1, 1, &hdr);
		GraphicsEngine::GetInstance()->GetFullscreenEffectTonemap().Render();
		ID3D11ShaderResourceView* nullSrv = nullptr;
		DX11::Context->PSSetShaderResources(1, 1, &nullSrv);
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

	ID3D11ShaderResourceView* srvs[3] = {
		myHdr.GetShaderResourceView(),
		myBloomMip[0].GetShaderResourceView(),
		myExposure[myExposureSrc].GetShaderResourceView(),
	};
	DX11::Context->PSSetShaderResources(0, 3, srvs);
	DX11::Rhi()->GetContext().SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(DX11::Rhi()->GetContext());   // b10

	BindFullscreen(myCompositePs);
	DX11::LogDrawCall();
	DX11::Context->Draw(3, 0);

	ID3D11ShaderResourceView* nulls[3] = {};
	DX11::Context->PSSetShaderResources(0, 3, nulls);
}

void DeferredRenderer::DebugBlit(int aChannel)
{
	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetCustomShaderParameters({ (float)aChannel, 0.f, 0.f, 0.f });

	BindGBufferSrvs();
	BindFullscreen(myDebugPs);
	DX11::LogDrawCall();
	DX11::Context->Draw(3, 0);
	UnbindGBufferSrvs();

	gss.SetCustomShaderParameters({ 0.f, 0.f, 0.f, 0.f });
}
