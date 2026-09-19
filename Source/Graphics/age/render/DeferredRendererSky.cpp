#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: procedural sky/atmosphere. Physically based single +
// multiple Rayleigh/Mie/ozone scattering (Hillaire 2020, the technique
// Unreal Engine ships) driven by the current sun direction -- see
// EngineAssets/Shaders/SkyAtmosphereCommon.hlsli for the actual physics.
//
// This file only produces a base sky cubemap (mySkyCubemapSrv). It is
// deliberately NOT a parallel lighting path: GameWorld feeds that cubemap
// into the exact same CubemapPrefilter::GeneratePrefilteredCubemap ->
// ambient.cubemap / worldEnvironmentPrefiltered pipeline the reflection
// probe already uses, so raster IBL, DXR's environment and GI's environment
// tint all pick it up with no changes of their own.

using namespace Ag;

namespace
{
	// Real Earth atmosphere constants, in meters -- the same values used by
	// every public reference implementation of this technique (Bruneton 2008 /
	// Hillaire 2020's own demo). gAtmosphereTurbidity/groundAlbedo are the only
	// tunables layered on top; everything else here is physical ground truth.
	constexpr float kBottomRadius = 6360000.f;
	constexpr float kTopRadius = 6460000.f;
	constexpr float kRayleighScattering[3] = { 5.802e-6f, 13.558e-6f, 33.1e-6f };
	constexpr float kRayleighDensityH = 8000.f;
	constexpr float kMieScattering = 3.996e-6f;
	constexpr float kMieExtinction = 4.440e-6f;   // single-scattering albedo ~0.9
	constexpr float kMieDensityH = 1200.f;
	constexpr float kMiePhaseG = 0.8f;
	constexpr float kOzoneAbsorption[3] = { 0.650e-6f, 1.881e-6f, 0.085e-6f };
	constexpr float kOzoneCenterAltitude = 25000.f;
	constexpr float kOzoneWidth = 15000.f;
}

bool DeferredRenderer::CreateSkyTargets()
{
	ReleaseSkyTargets();
	auto* dev = DX11::Rhi();

	auto createLut = [&](Vector2ui res, const char* name, rhi::TextureHandle& tex, rhi::SrvHandle& srv, rhi::UavHandle& uav)
	{
		rhi::TextureDesc td{};
		td.width = res.x; td.height = res.y;
		td.format = rhi::Format::R16G16B16A16_Float;
		td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
		td.debugName = name;
		tex = dev->CreateTexture(td);
		if (!tex.IsValid()) return false;
		srv = dev->CreateSrv(tex, rhi::SrvDesc{});
		uav = dev->CreateUav(tex, rhi::UavDesc{});
		return srv.IsValid() && uav.IsValid();
	};

	bool ok = createLut({ kTransmittanceLutW, kTransmittanceLutH }, "SkyTransmittanceLut", myTransmittanceLutTex, myTransmittanceLutSrv, myTransmittanceLutUav);
	ok &= createLut({ kMultiScatterLutRes, kMultiScatterLutRes }, "SkyMultiScatterLut", myMultiScatterLutTex, myMultiScatterLutSrv, myMultiScatterLutUav);
	ok &= createLut({ kSkyViewLutW, kSkyViewLutH }, "SkyViewLut", mySkyViewLutTex, mySkyViewLutSrv, mySkyViewLutUav);

	{
		rhi::TextureDesc td{};
		td.width = kSkyCubemapRes; td.height = kSkyCubemapRes;
		td.dimension = rhi::TextureDimension::TexCube;
		td.format = rhi::Format::R16G16B16A16_Float;
		td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::RenderTarget;
		td.debugName = "SkyBaseCubemap";
		mySkyCubemapTex = dev->CreateTexture(td);
		if (mySkyCubemapTex.IsValid())
		{
			rhi::SrvDesc sd{}; sd.asCube = true;
			mySkyCubemapSrv = dev->CreateSrv(mySkyCubemapTex, sd);
			for (uint32_t face = 0; face < 6; ++face)
			{
				rhi::RtvDesc rd{}; rd.firstArraySlice = face; rd.arraySize = 1;
				mySkyCubemapFaceRtv[face] = dev->CreateRtv(mySkyCubemapTex, rd);
				ok &= mySkyCubemapFaceRtv[face].IsValid();
			}
		}
		ok &= mySkyCubemapSrv.IsValid();
	}

	if (!mySkyLutSampler.IsValid())
	{
		rhi::SamplerDesc sd{}; sd.filter = rhi::FilterMode::Bilinear; sd.address = rhi::AddressMode::Clamp;
		mySkyLutSampler = dev->CreateSampler(sd);
		rhi::SamplerDesc cd{}; cd.filter = rhi::FilterMode::Bilinear; cd.address = rhi::AddressMode::Clamp;
		mySkyCubeSampler = dev->CreateSampler(cd);
	}

	mySkyFixedLutsValid = false;   // force a first bake
	return ok && mySkyLutSampler.IsValid() && mySkyCubeSampler.IsValid();
}

void DeferredRenderer::ReleaseSkyTargets()
{
	auto* dev = DX11::Rhi();
	auto destroyTex = [&](rhi::TextureHandle& tex, rhi::SrvHandle& srv, rhi::UavHandle& uav)
	{
		if (uav.IsValid()) dev->Destroy(uav);
		if (srv.IsValid()) dev->Destroy(srv);
		if (tex.IsValid()) dev->Destroy(tex);
		tex = {}; srv = {}; uav = {};
	};
	destroyTex(myTransmittanceLutTex, myTransmittanceLutSrv, myTransmittanceLutUav);
	destroyTex(myMultiScatterLutTex, myMultiScatterLutSrv, myMultiScatterLutUav);
	destroyTex(mySkyViewLutTex, mySkyViewLutSrv, mySkyViewLutUav);
	for (auto& rtv : mySkyCubemapFaceRtv) { if (rtv.IsValid()) dev->Destroy(rtv); rtv = {}; }
	if (mySkyCubemapSrv.IsValid()) dev->Destroy(mySkyCubemapSrv);
	if (mySkyCubemapTex.IsValid()) dev->Destroy(mySkyCubemapTex);
	mySkyCubemapSrv = {}; mySkyCubemapTex = {};
}

void DeferredRenderer::DispatchSkyFixedLuts()
{
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	mySkyConstantsCb.Bind(ctx, rhi::ShaderStage::Compute, 11);

	{
		rhi::ComputePipelineDesc pd; pd.cs = mySkyTransmittanceLutCS->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		ctx.SetUnorderedAccess(0, myTransmittanceLutUav);
		ctx.Dispatch((kTransmittanceLutW + 7) / 8, (kTransmittanceLutH + 7) / 8, 1);
		ctx.SetUnorderedAccess(0, {});
		ctx.SetComputePipeline({});
	}
	{
		rhi::ComputePipelineDesc pd; pd.cs = mySkyMultiScatterLutCS->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myTransmittanceLutSrv);
		ctx.SetSampler(rhi::ShaderStage::Compute, 0, mySkyLutSampler);
		ctx.SetUnorderedAccess(0, myMultiScatterLutUav);
		ctx.Dispatch(kMultiScatterLutRes, kMultiScatterLutRes, 1);
		ctx.SetUnorderedAccess(0, {});
		ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, {});
		ctx.SetComputePipeline({});
	}
}

void DeferredRenderer::DispatchSkyViewLut()
{
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	rhi::ComputePipelineDesc pd; pd.cs = mySkyViewLutCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	mySkyConstantsCb.Bind(ctx, rhi::ShaderStage::Compute, 11);
	const rhi::SrvHandle srvs[2] = { myTransmittanceLutSrv, myMultiScatterLutSrv };
	ctx.SetShaderResources(rhi::ShaderStage::Compute, 0, 2, srvs);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, mySkyLutSampler);
	ctx.SetUnorderedAccess(0, mySkyViewLutUav);
	ctx.Dispatch((kSkyViewLutW + 7) / 8, (kSkyViewLutH + 7) / 8, 1);
	ctx.SetUnorderedAccess(0, {});
	const rhi::SrvHandle nulls[2] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Compute, 0, 2, nulls);
	ctx.SetComputePipeline({});
}

void DeferredRenderer::RenderSkyCubemap(rhi::SrvHandle aNightSkyCubeSrv)
{
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(BlendState::Disabled);

	mySkyConstantsCb.Bind(ctx, rhi::ShaderStage::Pixel, 11);
	myCloudsConstantsCb.Bind(ctx, rhi::ShaderStage::Pixel, 13);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 2, mySkyViewLutSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 3, aNightSkyCubeSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 4, myCloudShapeNoiseSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, myCloudDetailNoiseSrv);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 0, mySkyLutSampler);
	ctx.SetSampler(rhi::ShaderStage::Pixel, 1, mySkyCubeSampler);

	for (uint32_t face = 0; face < 6; ++face)
	{
		SkyCubemapFaceCb faceCb{ face, std::max(0.f, myTunables.nightSkyIntensity), {0,0} };
		mySkyCubemapFaceCb.Update(ctx, faceCb);
		mySkyCubemapFaceCb.Bind(ctx, rhi::ShaderStage::Pixel, 12);
		SetTargets(ctx, { mySkyCubemapFaceRtv[face] }, {}, { kSkyCubemapRes, kSkyCubemapRes });
		BindFullscreen(mySkyCubemapPs);
		ctx.Draw(3, 0);
	}

	ctx.SetRenderTargets(0, nullptr, {});
	const rhi::SrvHandle nulls[4] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 2, 4, nulls);
}

bool DeferredRenderer::UpdateProceduralSky(const Vector3f& aSunDirToLight, const Vector3f& aSunIlluminance,
                                           float aCameraHeightM, rhi::SrvHandle aNightSkyCubeSrv)
{
	if (!myTunables.proceduralSkyEnabled || !mySkyTransmittanceLutCS || !mySkyMultiScatterLutCS ||
	    !mySkyViewLutCS || !mySkyCubemapPs || !mySkyConstantsCb.IsValid() || !mySkyCubemapSrv.IsValid())
		return false;

	const float turbidity = std::max(0.1f, myTunables.atmosphereTurbidity);
	const float groundAlbedo = std::clamp(myTunables.groundAlbedo, 0.f, 1.f);
	// Same cm -> m convention the height-fog code already uses (AtmosphereCommon.hlsli).
	const float cameraHeightM = std::max(0.f, aCameraHeightM);

	const bool fixedLutsDirty = !mySkyFixedLutsValid
		|| std::abs(turbidity - myLastSkyTurbidity) > 1e-4f
		|| std::abs(groundAlbedo - myLastSkyGroundAlbedo) > 1e-4f;

	const float dirEps = 1e-5f;
	const bool sunMoved =
		std::abs(aSunDirToLight.x - myLastSkySunDir.x) > dirEps ||
		std::abs(aSunDirToLight.y - myLastSkySunDir.y) > dirEps ||
		std::abs(aSunDirToLight.z - myLastSkySunDir.z) > dirEps;
	const bool illuminanceChanged =
		std::abs(aSunIlluminance.x - myLastSkySunIlluminance.x) > 1e-6f ||
		std::abs(aSunIlluminance.y - myLastSkySunIlluminance.y) > 1e-6f ||
		std::abs(aSunIlluminance.z - myLastSkySunIlluminance.z) > 1e-6f;
	// Camera height only changes the sky-view LUT on the scale of the
	// atmosphere's own density falloff (kilometres), while re-rendering the
	// cubemap also re-prefilters the IBL and re-measures the environment (a
	// CPU readback stall). A 1 m dead zone re-ran all of that every frame the
	// camera moved vertically.
	const bool heightChanged = std::abs(cameraHeightM - myLastSkyCameraHeight) > std::max(50.0f, 0.1f * myLastSkyCameraHeight);
	const bool skyViewDirty = fixedLutsDirty || sunMoved || illuminanceChanged || heightChanged;

	if (!fixedLutsDirty && !skyViewDirty) return false;

	auto& ctx = DX11::Rhi()->GetContext();
	{
		SkyAtmosphereConstants c{};
		c.gSunDirToLight[0] = aSunDirToLight.x; c.gSunDirToLight[1] = aSunDirToLight.y; c.gSunDirToLight[2] = aSunDirToLight.z;
		c.gBottomRadius = kBottomRadius;
		c.gSunIlluminance[0] = aSunIlluminance.x; c.gSunIlluminance[1] = aSunIlluminance.y; c.gSunIlluminance[2] = aSunIlluminance.z;
		c.gTopRadius = kTopRadius;
		c.gRayleighScattering[0] = kRayleighScattering[0]; c.gRayleighScattering[1] = kRayleighScattering[1]; c.gRayleighScattering[2] = kRayleighScattering[2];
		c.gRayleighDensityH = kRayleighDensityH;
		c.gMieScattering[0] = c.gMieScattering[1] = c.gMieScattering[2] = kMieScattering * turbidity;
		c.gMieDensityH = kMieDensityH;
		c.gMieExtinction[0] = c.gMieExtinction[1] = c.gMieExtinction[2] = kMieExtinction * turbidity;
		c.gMiePhaseG = kMiePhaseG;
		c.gOzoneAbsorption[0] = kOzoneAbsorption[0]; c.gOzoneAbsorption[1] = kOzoneAbsorption[1]; c.gOzoneAbsorption[2] = kOzoneAbsorption[2];
		c.gOzoneCenterAltitude = kOzoneCenterAltitude;
		c.gOzoneWidth = kOzoneWidth;
		c.gGroundAlbedo = groundAlbedo;
		c.gCameraHeight = cameraHeightM;
		c.gMultiScatterLutRes = kMultiScatterLutRes;
		c.gSkyViewLutWidth = kSkyViewLutW; c.gSkyViewLutHeight = kSkyViewLutH;
		c.gSunAngularRadius = std::clamp(myTunables.sunDiskAngularRadius, 0.001f, 0.2f);
		c.gSunDiskIntensity = std::max(0.f, myTunables.sunDiskIntensity);
		mySkyConstantsCb.Update(ctx, c);
	}

	if (fixedLutsDirty)
	{
		DispatchSkyFixedLuts();
		mySkyFixedLutsValid = true;
		myLastSkyTurbidity = turbidity;
		myLastSkyGroundAlbedo = groundAlbedo;
	}

	DispatchSkyViewLut();
	RenderSkyCubemap(myTunables.nightSkyIntensity > 0.f ? aNightSkyCubeSrv : rhi::SrvHandle{});

	myLastSkySunDir = aSunDirToLight;
	myLastSkySunIlluminance = aSunIlluminance;
	myLastSkyCameraHeight = cameraHeightM;
	return true;
}
