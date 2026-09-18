#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: Height fog and volumetric sunlight.

using namespace Tga;

bool DeferredRenderer::CreateAtmosphereTargetSet(Vector2ui aResolution, AtmosphereTargetSet& aSet)
{
	ReleaseAtmosphereTargetSet(aSet);
	auto* dev = DX11::Rhi();
	aSet.hdr = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);
	(void)dev;
	CreateVolumeTexture(aResolution, "AtmosphereSunlightRender", aSet.volumeTex, aSet.volumeSrv, aSet.volumeUav);
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
	(void)dev;
	CreateVolumeTexture(resolution, "AtmosphereSunlight", myVolumeTex, myVolumeSrv, myVolumeUav);
	return myAtmosphereHdr.GetSrv().IsValid() && myVolumeSrv.IsValid() && myVolumeUav.IsValid();
}

bool DeferredRenderer::CreateVolumeTexture(Vector2ui aTargetSize, const char* aName, rhi::TextureHandle& aTex, rhi::SrvHandle& aSrv, rhi::UavHandle& aUav)
{
	auto* dev = DX11::Rhi();
	if (aUav.IsValid()) dev->Destroy(aUav);
	if (aSrv.IsValid()) dev->Destroy(aSrv);
	if (aTex.IsValid()) dev->Destroy(aTex);
	aUav = {}; aSrv = {}; aTex = {};
	// The volume shader and the composite both take their size from this
	// texture, so its resolution alone is the fog quality/cost knob.
	myVolumeDivisor = 1u << uint32_t(std::clamp(myTunables.volumetricResolution, 0, 3));
	rhi::TextureDesc td{};
	td.width = std::max(1u, (aTargetSize.x + myVolumeDivisor - 1) / myVolumeDivisor);
	td.height = std::max(1u, (aTargetSize.y + myVolumeDivisor - 1) / myVolumeDivisor);
	td.format = rhi::Format::R16G16B16A16_Float;
	td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
	td.debugName = aName;
	aTex = dev->CreateTexture(td);
	if (aTex.IsValid())
	{
		aSrv = dev->CreateSrv(aTex, rhi::SrvDesc{});
		aUav = dev->CreateUav(aTex, rhi::UavDesc{});
	}
	return aSrv.IsValid() && aUav.IsValid();
}

bool DeferredRenderer::RenderAtmosphere(bool beforeTemporal, bool aRenderResolution)
{
	const auto& t = myTunables;
	// The clouds and the fog shafts must see the same sun the surfaces do, or
	// the deck stays lit white after sunset while the ground below it is dark.
	const float sunUp = SunElevationFactor();
	// Two output domains. Display resolution writes into myAtmosphereHdr and
	// swaps it into myHdr, as before. Render resolution (DLSS upscaling) writes
	// into myAtmosphereRender, whose image then becomes DLSS's colour input;
	// every fog input in that mode (ray depth, the ray output) is already at
	// render resolution, and the shaders derive all their pixel maths from
	// FogWidth/FogHeight, so the only thing that changes is the extent.
	const Vector2ui extent = aRenderResolution ? myDxrRenderResolution : myResolution;
	const bool targetsValid = aRenderResolution ? myAtmosphereRender.IsValid() : myAtmosphereHdr.GetSrv().IsValid();
	// Diagnostic images must remain unmodified. Fog has no temporal history of its own.
	// The clouds are dispatched and composited by this pass too, so fog being
	// off must not skip it: fog is simply run at zero density.
	const bool fogWanted = t.fogEnabled && (t.fogDensity > 0.f || t.atmosphereDebugView != 0);
	const bool cloudsWanted = t.cloudsEnabled && myCloudsVolumeCS && (IsDxrRenderer() || IsDxrFullscreen());
	if ((!fogWanted && !cloudsWanted) || !myAtmospherePs || !myAtmosphereCb.IsValid() || !targetsValid
		|| (IsDxrFullscreen() && t.dxrLightingView != 0)
		|| (myTaaWasEnabled && t.taaDebugView != 0)) return false;
	auto* dev = DX11::Rhi(); auto& ctx = dev->GetContext();
	const bool rayDepth = IsDxrRenderer() || IsDxrFullscreen();
	if (rayDepth && !beforeTemporal) return false;
	if ((1u << uint32_t(std::clamp(t.volumetricResolution, 0, 3))) != myVolumeDivisor)
	{
		CreateVolumeTexture(myResolution, "AtmosphereSunlight", myVolumeTex, myVolumeSrv, myVolumeUav);
		if (myAtmosphereRender.IsValid())
			CreateVolumeTexture(myAtmosphereRender.size, "AtmosphereSunlightRender",
				myAtmosphereRender.volumeTex, myAtmosphereRender.volumeSrv, myAtmosphereRender.volumeUav);
	}
	if (aRenderResolution && !rayDepth) return false; // only the DXR path renders below display resolution
	const rhi::UavHandle volumeUav = aRenderResolution ? myAtmosphereRender.volumeUav : myVolumeUav;
	const rhi::SrvHandle volumeSrv = aRenderResolution ? myAtmosphereRender.volumeSrv : myVolumeSrv;
	const bool rayVolume = rayDepth && myVolumeRTCS && dev->BindRaytracingSceneForCompute();
	const ComputeShader* volume = rayDepth ? myVolumeRTCS : myVolumeCS;
	const bool volumeActive = fogWanted && t.volumetricEnabled && t.volumetricStrength > 0 && t.fogDensity > 0 && volume
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
		cb.sunRadiance[i]=std::max(0.f,t.dxrSunTint[i]*t.dxrSunIntensity)*sunUp;
		// Fog is lit by the sky, so its colour has to follow the sun. As a
		// constant it was a slab of daylight-coloured haze sitting across the
		// bottom of every night frame, which no amount of exposure work can
		// fix because it is emitting the same light at midnight as at noon.
		// The floor matches the ambient term's, for starlight and airglow.
		cb.fogColor[i]=std::max(0.f,t.fogColor[i]) * SkyBrightnessScale() * (0.03f + 0.97f * sunUp);
	}
	cb.density=fogWanted ? std::clamp(t.fogDensity,0.f,0.05f) : 0.f;
	cb.heightFalloff=std::clamp(t.fogHeightFalloff,0.f,0.2f);
	cb.baseHeight=t.fogBaseHeight; cb.startDistance=std::max(0.f,t.fogStartDistance);
	// Ceiling raised from 1000. The tunable's default is 20 000 m and the slider
	// runs to 50 000, so every value above a kilometre was being silently
	// truncated here -- which is exactly the "fog stops at a fixed distance"
	// failure the default was raised to fix. Units were never wrong on this
	// path; the clamp was just left behind when the default moved.
	cb.maxDistance=std::clamp(t.fogMaxDistance,1.f,50000.f);
	cb.volumeDistance=std::clamp(t.volumetricDistance,1.f,300.f);
	cb.volumeStrength=std::clamp(t.volumetricStrength,0.f,2.f);
	cb.anisotropy=std::clamp(t.volumetricAnisotropy,0.f,0.8f);
	cb.width=extent.x; cb.height=extent.y;
	cb.steps=uint32_t(std::clamp(t.volumetricSteps,8,64));
	cb.affectSky=t.fogAffectSky ? 1u:0u;
	cb.volumeEnabled=volumeActive ? 1u:0u; cb.debugView=uint32_t(t.atmosphereDebugView);
	{
		const Vector3f r = myCameraTransform.GetRight(), u = myCameraTransform.GetUp(), f = myCameraTransform.GetForward();
		cb.camRight[0]=r.x; cb.camRight[1]=r.y; cb.camRight[2]=r.z;
		cb.camUp[0]=u.x; cb.camUp[1]=u.y; cb.camUp[2]=u.z;
		cb.camForward[0]=f.x; cb.camForward[1]=f.y; cb.camForward[2]=f.z;
		const float m22 = myViewToProj.GetDataPtr()[5];
		cb.tanHalfFovY = m22 != 0.f ? 1.f / m22 : 1.f;
		cb.aspect = extent.y != 0 ? (float)extent.x / (float)extent.y : 1.f;
		cb.time = std::fmod(Application::GetInstance()->GetTotalTime(), 100000.f);
		cb.starIntensity = std::max(0.f, t.starIntensity);
		cb.starDensity = std::clamp(t.starDensity, 4.f, 400.f);
		cb.starTwinkle = std::clamp(t.starTwinkle, 0.f, 1.f);
		cb.starsEnabled = t.starsEnabled ? 1.f : 0.f;
	}
	cb.sunDiskAngularRadius=std::clamp(t.sunDiskAngularRadius, 0.001f, 0.08f);
	cb.sunDiskIntensity=std::max(0.f,t.sunDiskIntensity);
	cb.sunDiskEnabled=t.sunDiskEnabled ? 1u : 0u;
	cb.preExposed = rayDepth && PreExposureActive() ? 1.f : 0.f;
	if (rayDepth && myTaaWasEnabled) { cb.jitter[0]=-myTaaJitter.x; cb.jitter[1]=-myTaaJitter.y; }
	myAtmosphereCb.Update(ctx,cb);
	const rhi::SrvHandle depth = rayDepth ? myTemporalSrv[1] : DX11::DepthBuffer->GetSrv();
	// Unconditional. This used to be skipped when aRenderResolution was set,
	// on the assumption that a display-resolution pass had already marched the
	// volume this frame and the DLSS variant could reuse it. In the DXR
	// renderer no such pass exists: the graph's second atmosphere pass calls
	// this with beforeTemporal false and is rejected a few lines above, so the
	// call inside ResolveDxrLightingToHdr is the only one that does any work,
	// and its aRenderResolution is exactly the DLSS super-resolution flag.
	// Turning DLSS on therefore stopped the hero cloud volume being marched at
	// all, and the composite went on sampling whatever frame happened to be
	// left in the target -- a cloudscape frozen at some earlier camera
	// orientation, with the shell's horizon cutting a hard diagonal across
	// the sky. DLAA was unaffected because it renders 1:1 and so never sets
	// the flag, which is why this only ever showed up under upscaling.
	//
	// The volume is still marched at display resolution while the composite
	// runs at render resolution, so it is resampled down and then back up by
	// the upscaler. That is correct but slightly soft; sizing the cloud
	// targets to the render resolution under upscaling would be both sharper
	// and cheaper, and is worth doing separately.
	DispatchClouds(depth);
	ctx.SetRenderTargets(0,nullptr,{});
	if (volumeActive) {
		TGA_PROFILE_SCOPE(myProfiler, "Fog volume march");
		rhi::ComputePipelineDesc pd; pd.cs=volume->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		myAtmosphereCb.Bind(ctx);
		ctx.SetShaderResource(rhi::ShaderStage::Compute,4,depth);
		myCloudsConstantsCb.Bind(ctx,rhi::ShaderStage::Compute,13);
		ctx.SetShaderResource(rhi::ShaderStage::Compute,2,myCloudShapeNoiseSrv);
		ctx.SetShaderResource(rhi::ShaderStage::Compute,3,myCloudDetailNoiseSrv);
		ctx.SetSampler(rhi::ShaderStage::Compute,1,myCloudSampler);
		if (rayDepth) {
			ctx.SetShaderResource(rhi::ShaderStage::Compute,0,RayTracingMaterialTable::Upload(*dev,ctx));
			ctx.SetSampler(rhi::ShaderStage::Compute,0,myDxrMaterialSampler);
		}
		if (!rayDepth) {
			myAtmosphereShadowCameraCb.Update(ctx,myWorldToView.GetDataPtr(),64);
			myAtmosphereShadowCameraCb.Bind(ctx);
			myShadowCb.Bind(ctx,rhi::ShaderStage::Compute,9);
			ctx.SetShaderResource(rhi::ShaderStage::Compute,1,myShadowSrv);
			ctx.SetSampler(rhi::ShaderStage::Compute,2,myShadowCmpSampler);
		}
		ctx.SetUnorderedAccess(0,volumeUav);
		ctx.Dispatch(((extent.x+myVolumeDivisor-1)/myVolumeDivisor+7)/8,((extent.y+myVolumeDivisor-1)/myVolumeDivisor+7)/8,1);
		ctx.SetUnorderedAccess(0,{});
		const rhi::SrvHandle nulls[5]={}; ctx.SetShaderResources(rhi::ShaderStage::Compute,0,5,nulls);
		ctx.SetComputePipeline({});
	}
	TGA_PROFILE_SCOPE(myProfiler, "Fog composite");
	auto& gss=GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(BlendState::Disabled);
	SetTargets(ctx,{aRenderResolution ? myAtmosphereRender.hdr.GetRtv() : myAtmosphereHdr.GetRtv()},{},extent);
	const rhi::SrvHandle inputs[]={beforeTemporal ? myDxrLightingSrv : myHdr.GetSrv(),volumeActive ? volumeSrv : rhi::SrvHandle{}};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel,1,2,inputs);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel,4,depth);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel,5,myExposure[myExposureSrc].GetSrv());
	ctx.SetShaderResource(rhi::ShaderStage::Pixel,6,myCloudVolumeSrv);
	ctx.SetSampler(rhi::ShaderStage::Pixel,3,myLinearSampler);
	myAtmosphereCb.Bind(ctx,rhi::ShaderStage::Pixel,8);
	BindFullscreen(myAtmospherePs); ctx.Draw(3,0);
	const rhi::SrvHandle nulls[7]={}; ctx.SetShaderResources(rhi::ShaderStage::Pixel,0,7,nulls);
	ctx.SetRenderTargets(0,nullptr,{});
	if (!aRenderResolution) std::swap(myHdr,myAtmosphereHdr);
	return true;
}
