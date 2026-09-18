#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: volumetric clouds. A 3D-noise raymarched cloud shell
// (Unreal-style, following Schneider's Horizon Zero Dawn talk for the cheap
// "powder" self-shadow approximation) rather than a flat billboard layer --
// see EngineAssets/Shaders/CloudsCommon.hlsli for the density field shared
// by this pass, the sky cubemap's coarse ambient contribution
// (DeferredRendererSky.cpp), and the cloud-shadow lookup fed into the
// existing volumetric-sunlight march (DeferredRendererAtmosphere.cpp).
//
// The two noise volumes are baked once at Init and kept forever -- they are
// tileable, wind-scrolled at sample time, and never depend on the sun,
// weather, or anything else that changes at runtime.

using namespace Tga;

bool DeferredRenderer::CreateCloudTargets(Vector2ui aResolution)
{
	auto* dev = DX11::Rhi();

	if (!myCloudShapeNoiseTex.IsValid())
	{
		rhi::TextureDesc td{};
		td.width = td.height = kCloudShapeNoiseRes; td.depthOrArraySize = kCloudShapeNoiseRes;
		td.dimension = rhi::TextureDimension::Tex3D;
		// rgb = perlinWorley, worleyMid, worleyFine (see CloudShapeNoiseCS.hlsl); a unused.
		td.format = rhi::Format::R8G8B8A8_UNorm;
		td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
		td.debugName = "CloudShapeNoise";
		myCloudShapeNoiseTex = dev->CreateTexture(td);
		if (myCloudShapeNoiseTex.IsValid())
		{
			myCloudShapeNoiseSrv = dev->CreateSrv(myCloudShapeNoiseTex, rhi::SrvDesc{});
			myCloudShapeNoiseUav = dev->CreateUav(myCloudShapeNoiseTex, rhi::UavDesc{});
		}

		rhi::TextureDesc dd{};
		dd.width = dd.height = kCloudDetailNoiseRes; dd.depthOrArraySize = kCloudDetailNoiseRes;
		dd.dimension = rhi::TextureDimension::Tex3D;
		dd.format = rhi::Format::R8_UNorm;
		dd.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
		dd.debugName = "CloudDetailNoise";
		myCloudDetailNoiseTex = dev->CreateTexture(dd);
		if (myCloudDetailNoiseTex.IsValid())
		{
			myCloudDetailNoiseSrv = dev->CreateSrv(myCloudDetailNoiseTex, rhi::SrvDesc{});
			myCloudDetailNoiseUav = dev->CreateUav(myCloudDetailNoiseTex, rhi::UavDesc{});
		}

		rhi::SamplerDesc sd{}; sd.filter = rhi::FilterMode::Bilinear; sd.address = rhi::AddressMode::Wrap;
		myCloudSampler = dev->CreateSampler(sd);

		// Clamp, not wrap: this samples the temporal history buffer by
		// reprojected UV (CloudsVolumeCS.hlsl), which can land outside
		// [0,1] near the screen edge -- wrap would fetch the opposite edge
		// instead of just clamping to the nearest valid history texel.
		rhi::SamplerDesc hd{}; hd.filter = rhi::FilterMode::Bilinear; hd.address = rhi::AddressMode::Clamp;
		myCloudHistorySampler = dev->CreateSampler(hd);
	}

	myCloudVolumeDivisor = 1u << uint32_t(std::clamp(myTunables.cloudResolution, 0, 3));
	rhi::TextureDesc vt{};
	vt.width = std::max(1u, aResolution.x / myCloudVolumeDivisor);
	vt.height = std::max(1u, aResolution.y / myCloudVolumeDivisor);
	vt.format = rhi::Format::R16G16B16A16_Float;
	vt.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	for (size_t i = 0; i < myCloudVolumeTexArr.size(); ++i)
	{
		if (myCloudVolumeUavArr[i].IsValid()) dev->Destroy(myCloudVolumeUavArr[i]);
		if (myCloudVolumeSrvArr[i].IsValid()) dev->Destroy(myCloudVolumeSrvArr[i]);
		if (myCloudVolumeTexArr[i].IsValid()) dev->Destroy(myCloudVolumeTexArr[i]);
		vt.debugName = i == 0 ? "CloudsVolumeA" : "CloudsVolumeB";
		myCloudVolumeTexArr[i] = dev->CreateTexture(vt);
		if (myCloudVolumeTexArr[i].IsValid())
		{
			myCloudVolumeSrvArr[i] = dev->CreateSrv(myCloudVolumeTexArr[i], rhi::SrvDesc{});
			myCloudVolumeUavArr[i] = dev->CreateUav(myCloudVolumeTexArr[i], rhi::UavDesc{});
		}
	}
	// The old resolution's history, if any, no longer matches these targets.
	myCloudVolumeIndex = 0;
	myCloudHistoryValid = false;
	myCloudVolumeTex = myCloudVolumeTexArr[0];
	myCloudVolumeSrv = myCloudVolumeSrvArr[0];
	myCloudVolumeUav = myCloudVolumeUavArr[0];

	return myCloudShapeNoiseSrv.IsValid() && myCloudDetailNoiseSrv.IsValid() &&
		myCloudVolumeSrv.IsValid() && myCloudSampler.IsValid() && myCloudHistorySampler.IsValid();
}

void DeferredRenderer::ReleaseCloudTargets()
{
	auto* dev = DX11::Rhi();
	auto destroyTex = [&](rhi::TextureHandle& tex, rhi::SrvHandle& srv, rhi::UavHandle& uav)
	{
		if (uav.IsValid()) dev->Destroy(uav);
		if (srv.IsValid()) dev->Destroy(srv);
		if (tex.IsValid()) dev->Destroy(tex);
		tex = {}; srv = {}; uav = {};
	};
	destroyTex(myCloudShapeNoiseTex, myCloudShapeNoiseSrv, myCloudShapeNoiseUav);
	destroyTex(myCloudDetailNoiseTex, myCloudDetailNoiseSrv, myCloudDetailNoiseUav);
	for (size_t i = 0; i < myCloudVolumeTexArr.size(); ++i)
		destroyTex(myCloudVolumeTexArr[i], myCloudVolumeSrvArr[i], myCloudVolumeUavArr[i]);
	myCloudVolumeTex = {}; myCloudVolumeSrv = {}; myCloudVolumeUav = {};
	myCloudHistoryValid = false;
	myCloudNoiseBaked = false;
}

void DeferredRenderer::BakeCloudNoise()
{
	if (myCloudNoiseBaked || !myCloudShapeNoiseCS || !myCloudDetailNoiseCS) return;
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	// Both bake shaders #include CloudsCommon.hlsli (for the noise helpers)
	// and so declare the b13 cbuffer even though neither reads it -- bind it
	// anyway so the root signature never points at an empty descriptor slot.
	myCloudsConstantsCb.Bind(ctx, rhi::ShaderStage::Compute, 13);

	{
		rhi::ComputePipelineDesc pd; pd.cs = myCloudShapeNoiseCS->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		ctx.SetUnorderedAccess(0, myCloudShapeNoiseUav);
		const uint32_t g = (kCloudShapeNoiseRes + 3) / 4;
		ctx.Dispatch(g, g, g);
		ctx.SetUnorderedAccess(0, {});
		ctx.SetComputePipeline({});
	}
	{
		rhi::ComputePipelineDesc pd; pd.cs = myCloudDetailNoiseCS->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		ctx.SetUnorderedAccess(0, myCloudDetailNoiseUav);
		const uint32_t g = (kCloudDetailNoiseRes + 3) / 4;
		ctx.Dispatch(g, g, g);
		ctx.SetUnorderedAccess(0, {});
		ctx.SetComputePipeline({});
	}

	myCloudNoiseBaked = true;
}

void DeferredRenderer::DispatchClouds(rhi::SrvHandle aDepthSrv)
{
	auto& t = myTunables;
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	if (!myCloudsVolumeCS || !myCloudVolumeUav.IsValid()) return;

	if ((1u << uint32_t(std::clamp(t.cloudResolution, 0, 3))) != myCloudVolumeDivisor)
		CreateCloudTargets(myResolution);

	const uint32_t current = myCloudVolumeIndex;
	const uint32_t previous = 1u - current;

	{
		CloudsConstants c{};
		c.gCloudsEnabled = t.cloudsEnabled ? 1u : 0u;
		c.gCloudCoverage = std::clamp(t.cloudCoverage, 0.f, 1.f);
		c.gCloudDensity = std::max(0.f, t.cloudDensity);
		c.gCloudBaseAltitude = t.cloudBaseAltitude;
		c.gCloudTopAltitude = std::max(t.cloudBaseAltitude + 1.f, t.cloudTopAltitude);
		c.gCloudScale = std::max(100.f, t.cloudScale);
		c.gCloudSpeed[0] = t.cloudSpeed[0]; c.gCloudSpeed[1] = t.cloudSpeed[1];
		// Wrapped: the noise functions are effectively periodic, and an
		// ever-growing float here would eventually lose the sub-meter
		// precision wind-scrolling needs over a long play session.
		c.gTime = std::fmod(Application::GetInstance()->GetTotalTime(), 100000.f);
		c.gCloudLightSteps = uint32_t(std::clamp(t.cloudLightSteps, 1, 8));
		c.gCloudDetailStrength = std::max(0.f, t.cloudDetailStrength);
		c.gCloudHistoryValid = (t.cloudTemporalEnabled && myCloudHistoryValid) ? 1u : 0u;
		std::memcpy(c.gCloudPrevWorldToClip.m, myCloudPrevWorldToClip.GetDataPtr(), sizeof(c.gCloudPrevWorldToClip.m));
		{
			const Vector3f r = myCameraTransform.GetRight(), u = myCameraTransform.GetUp(), f = myCameraTransform.GetForward();
			c.gCloudCamRight[0] = r.x; c.gCloudCamRight[1] = r.y; c.gCloudCamRight[2] = r.z;
			c.gCloudCamUp[0] = u.x; c.gCloudCamUp[1] = u.y; c.gCloudCamUp[2] = u.z;
			c.gCloudCamForward[0] = f.x; c.gCloudCamForward[1] = f.y; c.gCloudCamForward[2] = f.z;
			const float m22 = myViewToProj.GetDataPtr()[5];   // same derivation as the DXR lighting constants
			c.gCloudTanHalfFovY = m22 != 0.f ? 1.f / m22 : 1.f;
			c.gCloudAspect = myResolution.y != 0 ? (float)myResolution.x / (float)myResolution.y : 1.f;
		}
		myCloudsConstantsCb.Update(ctx, c);
	}

	if (!t.cloudsEnabled)
	{
		// The camera keeps moving while clouds are off; the matrix we'd
		// reproject from next time they're on is stale, so start history
		// fresh rather than reprojecting into whatever the camera used to
		// be looking at.
		myCloudHistoryValid = false;
		// The atmosphere composite pass (AtmosphereCompositePS.hlsl) always
		// blends myCloudVolumeSrv onto the sky -- it has no "clouds are
		// off" branch of its own, because that used to be unconditional:
		// this function simply didn't run its dispatch. Skipping the
		// dispatch without also clearing the buffer left whatever was last
		// rendered frozen there, so turning clouds off left the last
		// frame's cloud image stuck in the sky rather than actually
		// clearing it -- and, worse, gave temporal reprojection a stale,
		// wrong-camera-angle "history" to blend back in next time clouds
		// were re-enabled, which read as clouds visibly duplicating.
		const float clear[4] = { 0.f, 0.f, 0.f, 1.f };   // rgb=0, transmittance=1 -- no cloud
		ctx.ClearUnorderedAccessFloat(myCloudVolumeUavArr[current], clear);
		ctx.ClearUnorderedAccessFloat(myCloudVolumeUavArr[previous], clear);
		return;
	}
	if (!myCloudNoiseBaked) BakeCloudNoise();

	rhi::ComputePipelineDesc pd; pd.cs = myCloudsVolumeCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myAtmosphereCb.Bind(ctx, rhi::ShaderStage::Compute, 8);
	mySkyConstantsCb.Bind(ctx, rhi::ShaderStage::Compute, 11);
	myCloudsConstantsCb.Bind(ctx, rhi::ShaderStage::Compute, 13);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, mySkyViewLutSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myCloudShapeNoiseSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, myCloudDetailNoiseSrv);
	// Previous frame's blended result, reprojected by view direction in the
	// shader (clouds sit kilometers away, so per-frame camera translation is
	// negligible parallax) and blended with this frame's fresh sample --
	// see CloudsVolumeCS.hlsl. Ping-ponged with the output below so this
	// frame never reads the buffer it is itself writing.
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myCloudVolumeSrvArr[previous]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, aDepthSrv);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myCloudSampler);
	ctx.SetSampler(rhi::ShaderStage::Compute, 1, myCloudHistorySampler);
	ctx.SetUnorderedAccess(0, myCloudVolumeUavArr[current]);
	const Vector2ui volSize{ std::max(1u, myResolution.x / myCloudVolumeDivisor), std::max(1u, myResolution.y / myCloudVolumeDivisor) };
	ctx.Dispatch((volSize.x + 7) / 8, (volSize.y + 7) / 8, 1);
	ctx.SetUnorderedAccess(0, {});
	const rhi::SrvHandle nulls[4] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Compute, 0, 4, nulls);
	ctx.SetComputePipeline({});

	myCloudVolumeTex = myCloudVolumeTexArr[current];
	myCloudVolumeSrv = myCloudVolumeSrvArr[current];
	myCloudVolumeUav = myCloudVolumeUavArr[current];
	myCloudPrevWorldToClip = myWorldToView * myViewToProj;
	myCloudHistoryValid = true;
	myCloudVolumeIndex = previous;
}
