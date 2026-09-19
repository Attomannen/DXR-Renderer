#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer core: setup, G-buffer, lights, shadows, SSAO, SSR,
// clustered culling and frame graph assembly. The DXR, GI, atmosphere and
// post-FX passes live in the DeferredRenderer*.cpp files next to this one.

using namespace Ag;

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
	myDxrDepthPs   = DX11::LoadPixelShader("Shaders/DxrDepthToBufferPS");
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

	// --- DXR Stage-3: RayQuery DXR lighting pass (opt-in, DXR-1.1-only) ---
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
		myDxrLightingCS = DX11::LoadComputeShaderDxil("Shaders/DxrLightingCS");
		myDxrBrdfLutCS = DX11::LoadComputeShaderDxil("Shaders/DxrBrdfLutCS");
		myEmissiveGatherCS = DX11::LoadComputeShaderDxil("Shaders/EmissiveLightGatherCS");
		if (!myDxrLightingCS)
		{
			ERROR_PRINT("DeferredRenderer: DxrLightingCS failed to load; DXR lighting pass disabled");
		}
		else
		{
			myDxrLightingCb.Create(*DX11::Rhi(), sizeof(DxrLightingConstants), rhi::ShaderStage::Compute, 0, "DxrLightingConstants");
			{
				rhi::SamplerDesc sd2;
				sd2.filter = rhi::FilterMode::Anisotropic;
				sd2.maxAnisotropy = 16;
				sd2.address = rhi::AddressMode::Wrap;
				myDxrMaterialSampler = DX11::Rhi()->CreateSampler(sd2);
			}
			myDxrCopyPs = DX11::LoadPixelShader("Shaders/PostprocessCopyPS");
			myTaaCS = DX11::LoadComputeShaderDxil("Shaders/TemporalResolveCS");
			myNrdCompositeCS = DX11::LoadComputeShaderDxil("Shaders/NrdCompositeCS");
			myNrdCompositeCb.Create(*DX11::Rhi(), 16, rhi::ShaderStage::Compute, 0, "NrdCompositeCb");
			myEnvAverageCS = DX11::LoadComputeShaderDxil("Shaders/EnvironmentAverageCS");
			if (myEnvAverageCS)
			{
				rhi::TextureDesc ed{};
				ed.width = ed.height = 1; ed.mipLevels = 1;
				ed.dimension = rhi::TextureDimension::Tex2D;
				ed.format = rhi::Format::R32G32B32A32_Float;
				ed.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
				ed.debugName = "EnvironmentAverage";
				myEnvAverageTex = DX11::Rhi()->CreateTexture(ed);
				myEnvAverageUav = DX11::Rhi()->CreateUav(myEnvAverageTex, rhi::UavDesc{});
				if (!myEnvAverageUav.IsValid()) myEnvAverageCS = nullptr;
			}
			myTaaCb.Create(*DX11::Rhi(), sizeof(TaaCb), rhi::ShaderStage::Compute, 0, "TaaCb");
			if (!myTaaCS || !myTaaCb.IsValid()) myTunables.taaEnabled = false;
			if (!myDxrLightingCb.IsValid() || !myDxrMaterialSampler.IsValid() || !myDxrCopyPs ||
				!CreateDxrLightingTargets(aResolution))
				myDxrLightingCS = nullptr;
			else if (!CreateDxrBrdfLut())
				ERROR_PRINT("DeferredRenderer: BRDF LUT generation failed; using the analytic IBL fallback.");
			else
			{
				GraphicsEngine::GetInstance()->GetGraphicsStateStack().SetBrdfLutSrv(myDxrBrdfLutSrv);
				INFO_PRINT("DeferredRenderer: DXR lighting pass ready (toggle with SetDxrLighting)");
			}
		}

		// Ray-traced probe capture -- reuses myGiShBuffer (created above, if
		// GiProjectSHCS loaded) and myDxrMaterialSampler (created just above) for
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
	myBloomBlurPs      = DX11::LoadPixelShader("Shaders/BloomBlurPS");
	myBloomCombinePs   = DX11::LoadPixelShader("Shaders/BloomCombinePS");
	myBloomDownPs      = DX11::LoadPixelShader("Shaders/BloomDownPS");
	myBloomUpPs        = DX11::LoadPixelShader("Shaders/BloomUpPS");
	myExposureLumaPs   = DX11::LoadPixelShader("Shaders/ExposureLumaPS");
	myExposureDownPs   = DX11::LoadPixelShader("Shaders/ExposureDownPS");
	myExposureAdaptPs  = DX11::LoadPixelShader("Shaders/ExposureAdaptPS");
	myDofCocPs         = DX11::LoadPixelShader("Shaders/DofCocPS");
	myDofBlurPs        = DX11::LoadPixelShader("Shaders/DofBlurPS");
	myDofCompositePs   = DX11::LoadPixelShader("Shaders/DofCompositePS");
	myMbTileMaxCs      = DX11::LoadComputeShaderDxil("Shaders/MotionTileMaxCS");
	myMbNeighbourCs    = DX11::LoadComputeShaderDxil("Shaders/MotionNeighbourMaxCS");
	myMbBlurCs         = DX11::LoadComputeShaderDxil("Shaders/MotionBlurCS");
	myMbCb.Create(*DX11::Rhi(), sizeof(MotionBlurCb), rhi::ShaderStage::Compute, 0, "MotionBlurCb");
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

	mySkyTransmittanceLutCS = DX11::LoadComputeShader("Shaders/SkyTransmittanceLutCS");
	mySkyMultiScatterLutCS  = DX11::LoadComputeShader("Shaders/SkyMultiScatterLutCS");
	mySkyViewLutCS          = DX11::LoadComputeShader("Shaders/SkyViewLutCS");
	mySkyCubemapPs          = DX11::LoadPixelShader("Shaders/SkyCubemapPS");
	mySkyConstantsCb.Create(*DX11::Rhi(), sizeof(SkyAtmosphereConstants), rhi::ShaderStage::Compute, 11, "SkyAtmosphereConstants");
	mySkyCubemapFaceCb.Create(*DX11::Rhi(), sizeof(SkyCubemapFaceCb), rhi::ShaderStage::Pixel, 12, "SkyCubemapFaceCb");
	if (!mySkyTransmittanceLutCS || !mySkyMultiScatterLutCS || !mySkyViewLutCS || !mySkyCubemapPs ||
	    !mySkyConstantsCb.IsValid() || !mySkyCubemapFaceCb.IsValid() || !CreateSkyTargets())
	{
		ERROR_PRINT("DeferredRenderer: procedural sky shaders/targets failed; falling back to the authored cubemap.");
		myTunables.proceduralSkyEnabled = false;
	}

	myCloudShapeNoiseCS = DX11::LoadComputeShader("Shaders/CloudShapeNoiseCS");
	myCloudDetailNoiseCS = DX11::LoadComputeShader("Shaders/CloudDetailNoiseCS");
	myCloudsVolumeCS = DX11::LoadComputeShader("Shaders/CloudsVolumeCS");
	myCloudsConstantsCb.Create(*DX11::Rhi(), sizeof(CloudsConstants), rhi::ShaderStage::Compute, 13, "CloudsConstants");
	if (!myCloudShapeNoiseCS || !myCloudDetailNoiseCS || !myCloudsVolumeCS ||
	    !myCloudsConstantsCb.IsValid() || !CreateCloudTargets(aResolution))
	{
		ERROR_PRINT("DeferredRenderer: volumetric cloud shaders/targets failed; sky will render without clouds.");
		myTunables.cloudsEnabled = false;
	}

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
		gss.UpdateGpuStates();
		// t0 is the environment cube for every later pass.
		gss.BindLightingTextures();
	}
}

bool DeferredRenderer::OnResize(Vector2ui aResolution)
{
	if (aResolution.x == 0 || aResolution.y == 0) return false;
	if (aResolution.x == myResolution.x && aResolution.y == myResolution.y) return true;
	// No history buffer is valid across a resolution change.  This covers both
	// raster and DXR modes; CreateDxrLightingTargets also resets it, but may be
	// absent when the DXR path is disabled.
	ResetTemporalHistory();
	if (!CreateTargets(aResolution)) return false;
	if (myClusterCS) CreateClusterBuffers(aResolution);
	if (myCompositePs) CreatePostFxTargets(aResolution);
	if (myAtmospherePs && !CreateAtmosphereTargets(aResolution)) return false;
	// The cloud volume and its two history buffers are resolution-dependent
	// like everything above, but used to be built only by Init -- DispatchClouds
	// rebuilt them solely when the cloudResolution divisor changed, never when
	// the resolution did. After any resize the pass went on dispatching
	// myResolution/divisor threads into textures still sized for the startup
	// resolution, so only a sub-rect of the sky was written and the rest kept
	// stale content at the old scale. Invisible while the sky was black, which
	// is why it only ever showed up with volumetric clouds or an HDRI sky.
	if (myCloudsVolumeCS && !CreateCloudTargets(aResolution)) return false;
	if (myDxrLightingCS && !CreateDxrLightingTargets(aResolution)) return false;
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
		const float castHeadroom = std::min(myShadowSceneRadius, radius * 4.f + 4.f);   // metres
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
	myCameraTransform = aCamera.GetTransform();   // DXR lighting pass needs position + basis directly
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
			// Without depth writes, back faces of closed glass would blend over
			// the front faces in triangle order.
			gss.SetRasterizerState(RasterizerState::BackfaceCulling);
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 9, myOpaqueHdr.GetSrv());
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 6, DX11::DepthBuffer->GetSrv());
			const bool preExposed = PreExposureActive();
			if (preExposed) ctx.SetShaderResource(rhi::ShaderStage::Pixel, 7, myExposure[myPreExposureIndex].GetSrv());
			// In the DXR renderer, reflect the same prefiltered environment the rays use.
			const bool dxrEnvironment = IsDxrRenderer() && myGiEnvironmentEnabled;
			const Vector3f tint = dxrEnvironment ? EnvironmentTint() : Vector3f{ 1.f, 1.f, 1.f };
			gss.SetCustomShaderParameters({ preExposed ? 1.f : 0.f, tint.x, tint.y, tint.z });
			gss.UpdateGpuStates();
			if (dxrEnvironment) ctx.SetShaderResource(rhi::ShaderStage::Pixel, 0, myGiEnvironmentSrv);
			aDrawTransparent();
			gss.SetCustomShaderParameters({ 0.f, 0.f, 0.f, 0.f });
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 9, {});
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 6, {});
			ctx.SetShaderResource(rhi::ShaderStage::Pixel, 7, {});
			gss.SetBlendState(BlendState::Disabled);
			gss.SetDepthStencilState(DepthStencilState::WriteLess);
		});
	};

	// Full DXR renderer: the camera RayQuery pass is the sole producer of HDR
	// radiance. Do this before any G-buffer, CSM, local-shadow-atlas, SSAO, SSR,
	// or transparent raster work is scheduled, so no raster lighting input can
	// silently influence the image.
	if (IsDxrRenderer())
	{
		aGraph.AddPass("dxrRenderer", [this](RenderGraph&) { RenderDxrLighting(); });
		aGraph.AddPass("dxrRendererToHdr", [this](RenderGraph&) { ResolveDxrLightingToHdr(); });
		// Glass and other forward materials composite over the ray-traced
		// result, depth-tested against the ray-traced depth.
		if (aDrawTransparent)
		{
			aGraph.AddPass("dxrDepth", [this](RenderGraph&) { WriteDxrDepthToDepthBuffer(); });
			addTransparentPass();
		}
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
	if (IsDxrSunShadows()) aGraph.AddPass("dxrSunShadows", [this](RenderGraph&) { RenderDxrSunShadows(); });

	// DXR Stage-3 validation: independent of the debug-channel system (reads
	// no G-buffer channel, writes its own diagnostic texture) -- runs
	// whenever toggled on, debug channel or not. See SetDxrLighting's
	// comment for why this always sees the CURRENT frame's TLAS.
	if (IsDxrLighting())
	{
		aGraph.AddPass("dxrLighting", [this](RenderGraph&) { RenderDxrLighting(); });

		if (IsDxrFullscreen())
		{
			aGraph.AddPass("dxrLightingToHdr", [this](RenderGraph&)
			{
				ResolveDxrLightingToHdr();
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
	GraphicsEngine::GetInstance()->GetGraphicsStateStack().BindLightingTextures();
	ctx.Draw(3, 0);
	UnbindGBufferSrvs();

	ctx.SetRenderTargets(0, nullptr, {});
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
