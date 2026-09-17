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
		cb.fogColor[i]=std::max(0.f,t.fogColor[i]) * SkyBrightnessScale();
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
	cb.preExposed = rayDepth && PreExposureActive() ? 1.f : 0.f;
	if (rayDepth && myTaaWasEnabled) { cb.jitter[0]=-myTaaJitter.x; cb.jitter[1]=-myTaaJitter.y; }
	myAtmosphereCb.Update(ctx,cb);
	const rhi::SrvHandle depth = rayDepth ? myTemporalSrv[1] : DX11::DepthBuffer->GetSrv();
	ctx.SetRenderTargets(0,nullptr,{});
	if (volumeActive) {
		TGA_PROFILE_SCOPE(myProfiler, "Fog volume march");
		rhi::ComputePipelineDesc pd; pd.cs=volume->module;
		ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
		myAtmosphereCb.Bind(ctx);
		ctx.SetShaderResource(rhi::ShaderStage::Compute,4,depth);
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
	ctx.SetSampler(rhi::ShaderStage::Pixel,3,myLinearSampler);
	myAtmosphereCb.Bind(ctx,rhi::ShaderStage::Pixel,8);
	BindFullscreen(myAtmospherePs); ctx.Draw(3,0);
	const rhi::SrvHandle nulls[6]={}; ctx.SetShaderResources(rhi::ShaderStage::Pixel,0,6,nulls);
	ctx.SetRenderTargets(0,nullptr,{});
	if (!aRenderResolution) std::swap(myHdr,myAtmosphereHdr);
	return true;
}
