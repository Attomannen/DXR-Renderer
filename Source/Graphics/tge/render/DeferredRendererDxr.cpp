#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: DXR lighting pass: ray-traced lighting, NRD denoising, exposure history,
// environment measurement and the resolve to HDR.

using namespace Tga;

Vector3f DeferredRenderer::EnvironmentTint() const
{
	if (myTunables.skyLuminanceNits <= 0.f || myEnvAverageLuminance <= 0.f)
		return myGiEnvironmentTint;
	// Keep the authored tint's hue, take its brightness from the physical sky.
	const Vector3f& t = myGiEnvironmentTint;
	const float luma = 0.2126f * t.x + 0.7152f * t.y + 0.0722f * t.z;
	const Vector3f hue = luma > 1e-6f ? t / luma : Vector3f{ 1.f, 1.f, 1.f };
	return hue * (Photometry::NitsToUnits(myTunables.skyLuminanceNits) / myEnvAverageLuminance);
}

float DeferredRenderer::SkyBrightnessScale() const
{
	// Flat ambient floor and fog colour were authored as absolute values for a
	// clear day; with a physical sky they follow its brightness instead.
	constexpr float kClearDaySkyNits = 8000.f;
	return myTunables.skyLuminanceNits > 0.f ? myTunables.skyLuminanceNits / kClearDaySkyNits : 1.f;
}

float DeferredRenderer::SkyDisplayScale() const
{
	if (myTunables.skyLuminanceNits <= 0.f || myEnvAverageLuminance <= 0.f) return 1.f;
	return Photometry::NitsToUnits(myTunables.skyLuminanceNits) / myEnvAverageLuminance;
}

bool DeferredRenderer::PreExposureActive() const
{
	return myTunables.preExposure && IsDxrRenderer() && IsPostFx() && myExposure[0].GetSrv().IsValid();
}

void DeferredRenderer::EnsureExposureHistory()
{
	if (myExposureCleared || !myExposure[0].GetSrv().IsValid()) return;
	// Seed with the manual camera's EV rather than 0: EV 0 as a pre-exposure
	// would push a sunlit frame far past FP16's range on the first frame.
	const float ev = Photometry::Ev100FromCamera(myTunables.cameraAperture, myTunables.cameraShutter, myTunables.cameraIso);
	myExposure[0].Clear({ ev, 0, 0, 0 });
	myExposure[1].Clear({ ev, 0, 0, 0 });
	myExposureCleared = true;
}

void DeferredRenderer::MeasureEnvironment(rhi::ICommandContext& ctx)
{
	if (!myEnvAverageCS) return;
	rhi::IDevice* dev = DX11::Rhi();
	if (myEnvAveragePending.IsValid())
	{
		// Dispatched last frame, which has been submitted since; the readback
		// waits for it. One stall per environment change.
		float v[4] = {};
		if (dev->ReadBackFloatPixel4(myEnvAverageTex, 0, 0, v) && v[3] == v[3])
		{
			myEnvAverageLuminance = std::max(v[3], 1e-8f);
			INFO_PRINT("DeferredRenderer: environment sky averages %.4g units (%.0f cd/m2 as authored)",
				myEnvAverageLuminance, myEnvAverageLuminance * Photometry::kNitsPerUnit);
		}
		myEnvAverageMeasured = myEnvAveragePending;
		myEnvAveragePending = {};
	}
	if (!myGiEnvironmentEnabled || myEnvAverageMeasured == myGiEnvironmentSrv) return;
	TGA_PROFILE_SCOPE(myProfiler, "Environment measure");

	rhi::ComputePipelineDesc pd;
	pd.cs = myEnvAverageCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myGiEnvironmentSrv);
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myDxrMaterialSampler);
	ctx.SetUnorderedAccess(0, myEnvAverageUav);
	ctx.Dispatch(1, 1, 1);
	ctx.SetUnorderedAccess(0, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetComputePipeline({});
	myEnvAveragePending = myGiEnvironmentSrv;
}

bool DeferredRenderer::CreateDxrLightingTargets(Vector2ui aResolution)
{
	ResetTemporalHistory();
	rhi::IDevice* dev = DX11::Rhi();
	// DLSS SR and Ray Reconstruction both reconstruct the display image from
	// this smaller input; mode 0/1 renders 1:1. RR's guide textures share it.
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
	if (myDxrLightingUav.IsValid()) dev->Destroy(myDxrLightingUav);
	if (myDxrLightingSrv.IsValid()) dev->Destroy(myDxrLightingSrv);
	if (myDxrLightingTex.IsValid()) dev->Destroy(myDxrLightingTex);
	myDxrLightingUav = {};
	myDxrLightingSrv = {};
	myDxrLightingTex = {};

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
	td.debugName = "DxrLightingTex";
	myDxrLightingTex = dev->CreateTexture(td);
	if (!myDxrLightingTex.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: DXR lighting pass texture creation failed");
		return false;
	}

	myDxrLightingSrv = dev->CreateSrv(myDxrLightingTex, rhi::SrvDesc{});
	myDxrLightingUav = dev->CreateUav(myDxrLightingTex, rhi::UavDesc{});
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
		// Always allocated: DxrLightingCS declares u7..u10 unconditionally.
		const rhi::Format nrdFormats[kNrdTexCount] = { rhi::Format::R32_Float, rhi::Format::R10G10B10A2_UNorm,
			rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float, rhi::Format::R16G16B16A16_Float,
			rhi::Format::R8G8B8A8_UNorm };
		const char* nrdNames[kNrdTexCount] = { "NrdViewZ", "NrdNormalRoughness", "NrdDiffuseNoisy", "NrdSpecularNoisy", "NrdDiffuseDenoised", "NrdSpecularDenoised", "NrdValidation" };
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
	if (!myDxrLightingSrv.IsValid() || !myDxrLightingUav.IsValid())
	{
		ERROR_PRINT("DeferredRenderer: DXR lighting pass view creation failed");
		return false;
	}
	// Fog has to be applied in the render-resolution domain when DLSS upscales
	// (see ResolveDxrLightingToHdr), and every fog input -- ray depth, the ray
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

void DeferredRenderer::RenderDxrLighting()
{
	if (!IsDxrLighting()) return;

	rhi::IDevice* dev = DX11::Rhi();
	rhi::ICommandContext& ctx = dev->GetContext();
	// See GiProjectProbeRT: a begin-frame root bind can predate this frame's
	// TLAS build, and an empty scene must never trace the previous frame.
	if (!dev->BindRaytracingSceneForCompute()) { ResetTemporalHistory(); return; }
	MeasureEnvironment(ctx);
	EnsureExposureHistory();
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
	//   1. Ray generation offsets the sample by +gJitter (DxrLightingCS::main).
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

	{
		DxrLightingConstants c{};
		const Vector3f pos = myCameraTransform.GetPosition();
		const Vector3f right = myCameraTransform.GetRight();
		const Vector3f up = myCameraTransform.GetUp();
		const Vector3f fwd = myCameraTransform.GetForward();
		c.gCameraOrigin[0] = pos.x;   c.gCameraOrigin[1] = pos.y;   c.gCameraOrigin[2] = pos.z;
		c.gCameraRight[0]  = right.x; c.gCameraRight[1]  = right.y; c.gCameraRight[2]  = right.z;
		c.gCameraUp[0]     = up.x;    c.gCameraUp[1]     = up.y;    c.gCameraUp[2]     = up.z;
		c.gCameraForward[0] = fwd.x;  c.gCameraForward[1] = fwd.y;  c.gCameraForward[2] = fwd.z;
		// CreatePerspectiveMatrixFovX: persp[5] (m22) = xScale*aspectRatio = 1/tan(halfFovY).
		const float m22 = myViewToProj.GetDataPtr()[5];
		c.gTanHalfFovY = m22 != 0.f ? 1.f / m22 : 1.f;
		c.gAspect = myResolution.y != 0 ? (float)myResolution.x / (float)myResolution.y : 1.f;
		c.gOutputSize[0] = myDxrRenderResolution.x;
		c.gOutputSize[1] = myDxrRenderResolution.y;
		// myShadowLightDir is the direction the sun's rays TRAVEL (matches
		// SetShadowLight's cascade-camera convention); shading wants the
		// opposite -- the direction FROM a surface TOWARD the light.
		const float lx = -myShadowLightDir.x, ly = -myShadowLightDir.y, lz = -myShadowLightDir.z;
		const float lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
		if (lLen > 1e-5f) { c.gSunDirToLight[0] = lx / lLen; c.gSunDirToLight[1] = ly / lLen; c.gSunDirToLight[2] = lz / lLen; }
		else { c.gSunDirToLight[0] = 0.f; c.gSunDirToLight[1] = 1.f; c.gSunDirToLight[2] = 0.f; }
		c.gLightCount = (uint32_t)myLightCount;
		c.gSunRadiance[0] = myTunables.dxrSunTint[0] * myTunables.dxrSunIntensity;
		c.gSunRadiance[1] = myTunables.dxrSunTint[1] * myTunables.dxrSunIntensity;
		c.gSunRadiance[2] = myTunables.dxrSunTint[2] * myTunables.dxrSunIntensity;
		c.gAmbientIntensity = myTunables.dxrAmbientIntensity * SkyBrightnessScale();
		c.gReflectionRoughnessCutoff = myTunables.dxrReflectionRoughnessCutoff;
		constexpr float kDxrEnvironmentScale = 1.0f;
		const Vector3f environmentTint = EnvironmentTint() * kDxrEnvironmentScale;
		c.gEnvironmentTint[0] = environmentTint.x;
		c.gEnvironmentTint[1] = environmentTint.y;
		c.gEnvironmentTint[2] = environmentTint.z;
		c.gSkyDisplayScale = SkyDisplayScale();
		c.gPreExposed = PreExposureActive() ? 1u : 0u;
		c.gEnvironmentMip = myGiEnvironmentEnabled ? 0.f : -1.f;
		c.gAoDistance = myTunables.dxrAoDistance;
		c.gAoStrength = myTunables.dxrAoStrength;
		c.gEnableDirectLighting = myTunables.dxrDirectLighting ? 1u : 0u;
		c.gEnableEnvironmentLighting = myTunables.dxrEnvironmentLighting ? 1u : 0u;
		c.gEnableIndirectGi = myTunables.dxrIndirectGi ? 1u : 0u;
		c.gEnableReflections = myTunables.dxrReflections ? 1u : 0u;
		c.gEnableAmbientOcclusion = myTunables.dxrAmbientOcclusion ? 1u : 0u;
		c.gLightingView = (uint32_t)myTunables.dxrLightingView;
		c.gBrdfLutValid = myDxrBrdfLutSrv.IsValid() ? 1u : 0u;
		{
			const bool checkerboard = NrdActive() && myTunables.nrdCheckerboard;
			if (checkerboard != myNrdCheckerboardActive)
			{
				// NRD's history holds the other layout; start clean.
				myNrdCheckerboardActive = checkerboard;
				myNrdHistoryValid = false;
			}
			c.gNrdCheckerboard = checkerboard ? 1u : 0u;
			c.gCheckerboardPhase = (myNrd ? myNrd->NextFrameIndex() : 0u) & 1u;
		}
		c.gSunShadowSamples = uint32_t(std::clamp(myTunables.dxrSunShadowSamples, 1, 4));
		c.gNrdReblur = myTunables.nrdDenoiser == 0 ? 1.f : 0.f;
		c.gNrdHitDistA = kNrdHitDistanceA;
		c.gTextureFiltering = myTunables.dxrTextureFiltering ? 1u : 0u;
		c.gReflectionSamples = uint32_t(std::clamp(myTunables.dxrReflectionSamples, 1, 8));
		c.gAoSamples = uint32_t(std::clamp(myTunables.dxrAoSamples, 1, 8));
		const Matrix4x4f worldToClip = myWorldToView * myViewToProj;
		memcpy(c.gWorldToClip.m, worldToClip.GetDataPtr(), sizeof(c.gWorldToClip.m));
		memcpy(c.gPreviousWorldToClip.m, myPreviousWorldToClip.GetDataPtr(), sizeof(c.gPreviousWorldToClip.m));
		c.gTemporalHistoryValid = (myTemporalHistoryValid && !lightingChanged) ? 1u : 0u;
		c.gSpecularAaStrength = myTunables.specularAaEnabled ? myTunables.specularAaStrength : 0.f;
		c.gReflectionFrameIndex = myDxrFrameIndex++;
		c.gJitter[0] = myTaaJitter.x; c.gJitter[1] = myTaaJitter.y;
		c.gPreviousJitter[0] = myPreviousTaaJitter.x; c.gPreviousJitter[1] = myPreviousTaaJitter.y;
		c.gNrdEnabled = NrdActive() ? 1u : 0u;
		myDxrLightingCb.Update(ctx, c);
	}

	// Rebuilding a few dozen material records every frame is trivial next to
	// an actual ray-traced pass -- see RayTracingMaterialTable::Upload.
	rhi::ComputePipelineDesc pd;
	pd.cs = myDxrLightingCS->module;
	rhi::SrvHandle materialSrv;
	{
		TGA_PROFILE_SCOPE(myProfiler, "Material table upload");
		materialSrv = RayTracingMaterialTable::Upload(*dev, ctx);
	}
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	myDxrLightingCb.Bind(ctx);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, materialSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myLightBuffer.Srv());
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, HasGi() ? myGiShBuffer.Srv() : rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myGiEnvironmentSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, myDxrBrdfLutSrv);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 5, myGiVisibilityBuffer.Srv());
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, myExposure[myExposureSrc].GetSrv());
	ctx.SetSampler(rhi::ShaderStage::Compute, 0, myDxrMaterialSampler);
	myGiVolumeCb.Bind(ctx, rhi::ShaderStage::Compute, 13);
	ctx.SetUnorderedAccess(0, myDxrLightingUav);
	for (uint32_t i = 0; i < myTemporalUav.size(); ++i) ctx.SetUnorderedAccess(i + 1, myTemporalUav[i]);
	const uint32_t nrdFirstUav = uint32_t(myTemporalUav.size()) + 1;
	for (uint32_t i = 0; i <= kNrdSpecular; ++i) ctx.SetUnorderedAccess(nrdFirstUav + i, myNrdUav[i]);

	{
		// One dispatch: primary rays, direct light + shadows, AO, GI lookup,
		// reflections. Use the Feature cost sweep to split this further.
		TGA_PROFILE_SCOPE(myProfiler, "Ray trace + shade");
		ctx.Dispatch((myDxrRenderResolution.x + 7) / 8, (myDxrRenderResolution.y + 7) / 8, 1);
	}

	ctx.SetUnorderedAccess(0, {});
	for (uint32_t i = 0; i < myTemporalUav.size(); ++i) ctx.SetUnorderedAccess(i + 1, {});
	for (uint32_t i = 0; i <= kNrdSpecular; ++i) ctx.SetUnorderedAccess(nrdFirstUav + i, {});
	// The resolve (DLSS / RR) runs after this and needs last frame's matrix.
	myResolvePreviousWorldToClip = myPreviousWorldToClip;
	myPreviousWorldToClip = myWorldToView * myViewToProj;
	myTemporalHistoryValid = true;
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 5, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 6, rhi::SrvHandle{});
	ctx.SetComputePipeline({});

	if (NrdActive())
		DenoiseDxrDiffuse(ctx);
}

bool DeferredRenderer::NrdActive() const
{
	// Mutually exclusive with Ray Reconstruction, which denoises the composed
	// frame itself and reads gDiffuseAlbedo.a as plain alpha.
	return myTunables.nrdEnabled && !myNrdFailed && myNrdCompositeCS && myNrdTex[kNrdSpecularOut].IsValid()
		&& !myTunables.rayReconstructionEnabled && myTunables.dxrLightingView == 0
		&& dynamic_cast<rhi::dx12::Dx12CommandContext*>(&DX11::Rhi()->GetContext()) != nullptr;
}

void DeferredRenderer::DenoiseDxrDiffuse(rhi::ICommandContext& ctx)
{
	rhi::IDevice* dev = DX11::Rhi();
	auto* dx12Ctx = static_cast<rhi::dx12::Dx12CommandContext*>(&ctx);
	const auto denoiser = myTunables.nrdDenoiser == 0 ? rhi::dx12::NrdWrapper::Denoiser::Reblur : rhi::dx12::NrdWrapper::Denoiser::Relax;
	if (myNrd && myNrd->GetDenoiser() != denoiser)
		myNrd.reset();
	if (!myNrd)
	{
		myNrd = std::make_unique<rhi::dx12::NrdWrapper>();
		if (!myNrd->Initialize(static_cast<rhi::dx12::Dx12Device*>(dev), myDxrRenderResolution.x, myDxrRenderResolution.y, denoiser))
		{
			myNrd.reset();
			myNrdFailed = true;
			return;
		}
		myNrdHistoryValid = false;
	}

	{
		rhi::dx12::NrdWrapper::Settings config;
		config.checkerboard = myNrdCheckerboardActive;
		// NRD asks for history in seconds; convert with the current frame rate.
		const float fps = 1.f / std::max(Application::GetInstance()->GetDeltaTime(), 1.f / 240.f);
		const uint32_t maxFrames = denoiser == rhi::dx12::NrdWrapper::Denoiser::Reblur ? nrd::REBLUR_MAX_HISTORY_FRAME_NUM : 255u;
		// Quantised so a fluctuating frame rate doesn't re-upload settings every frame.
		const uint32_t frames = nrd::GetMaxAccumulatedFrameNum(std::max(myTunables.nrdHistorySeconds, 0.01f), fps);
		config.historyFrames = std::clamp((frames + 2u) / 4u * 4u, 2u, maxFrames);
		config.fastHistoryFrames = uint32_t(std::clamp(myTunables.nrdFastHistoryFrames, 1, int(config.historyFrames)));
		config.hitDistanceA = kNrdHitDistanceA;
		config.antilag = myTunables.nrdAntilag;
		myNrd->Configure(config);
	}

	nrd::CommonSettings settings = {};
	// Both engine and NRD store matrices for row vectors in row-major order,
	// which is byte-identical to NRD's column-major, column-vector layout.
	// NRD wants the unjittered projection; jitter is passed separately.
	// NRD's world is the engine's world in metres, so its world -> view matrix
	// is the same rigid transform with the translation (row 4, row vectors)
	// scaled. Scaling the whole matrix instead makes it non-rigid, and NRD's
	// camera decomposition then resets history every frame.
	Matrix4x4f worldToViewMeters = myWorldToView;
	for (int c = 1; c <= 3; ++c) worldToViewMeters(4, c) *= kNrdMetersPerUnit;
	memcpy(settings.viewToClipMatrix, myViewToProj.GetDataPtr(), sizeof(settings.viewToClipMatrix));
	memcpy(settings.worldToViewMatrix, worldToViewMeters.GetDataPtr(), sizeof(settings.worldToViewMatrix));
	const bool history = myNrdHistoryValid;
	memcpy(settings.viewToClipMatrixPrev, (history ? myNrdPrevViewToClip : myViewToProj).GetDataPtr(), sizeof(settings.viewToClipMatrixPrev));
	memcpy(settings.worldToViewMatrixPrev, (history ? myNrdPrevWorldToView : worldToViewMeters).GetDataPtr(), sizeof(settings.worldToViewMatrixPrev));
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
	myNrdPrevWorldToView = worldToViewMeters;
	myNrdHistoryValid = true;

	// NRD restores exactly these states, so the RHI's tracking stays valid.
	for (int i = kNrdViewZ; i <= kNrdSpecular; ++i)
		ctx.TransitionResource(myNrdTex[i], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myTemporalTex[0], rhi::ResourceState::NonPixelShaderResource);
	ctx.TransitionResource(myNrdTex[kNrdDiffuseOut], rhi::ResourceState::UnorderedAccess);
	ctx.TransitionResource(myNrdTex[kNrdSpecularOut], rhi::ResourceState::UnorderedAccess);
	dx12Ctx->FlushBarriers();

	auto native = [dev](rhi::TextureHandle h) { return static_cast<ID3D12Resource*>(dev->GetNativeTexture(h)); };
	rhi::dx12::NrdWrapper::Inputs in;
	in.viewZ = native(myNrdTex[kNrdViewZ]);
	in.normalRoughness = native(myNrdTex[kNrdNormal]);
	in.diffuse = native(myNrdTex[kNrdDiffuse]);
	in.specular = native(myNrdTex[kNrdSpecular]);
	in.diffuseOut = native(myNrdTex[kNrdDiffuseOut]);
	in.specularOut = native(myNrdTex[kNrdSpecularOut]);
	in.motion = native(myTemporalTex[0]);
	if (myTunables.nrdValidation)
	{
		ctx.TransitionResource(myNrdTex[kNrdValidation], rhi::ResourceState::UnorderedAccess);
		dx12Ctx->FlushBarriers();
		in.validation = native(myNrdTex[kNrdValidation]);
		settings.enableValidation = true;
	}
	{
		TGA_PROFILE_SCOPE(myProfiler, "NRD denoise");
		dx12Ctx->PushMarker("NRD RELAX diffuse+specular");
		myNrd->Denoise(*dx12Ctx, in, settings);
		dx12Ctx->PopMarker();
	}

	TGA_PROFILE_SCOPE(myProfiler, "NRD composite");

	rhi::ComputePipelineDesc pd;
	pd.cs = myNrdCompositeCS->module;
	ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myNrdSrv[kNrdDiffuseOut]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myTemporalSrv[4]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, myNrdSrv[kNrdSpecularOut]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myTemporalSrv[5]);
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, myNrdSrv[kNrdValidation]);
	{
		struct { uint32_t reblur, validation; float overlayScale; uint32_t pad; } cb{
			denoiser == rhi::dx12::NrdWrapper::Denoiser::Reblur ? 1u : 0u,
			myTunables.nrdValidation ? 1u : 0u,
			1.f,   // the HDR is pre-exposed, so ~1 displays as white-ish
			0u };
		myNrdCompositeCb.Update(ctx, cb);
		myNrdCompositeCb.Bind(ctx);
	}
	ctx.SetUnorderedAccess(0, myDxrLightingUav);
	ctx.Dispatch((myDxrRenderResolution.x + 7) / 8, (myDxrRenderResolution.y + 7) / 8, 1);
	ctx.SetUnorderedAccess(0, {});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, rhi::SrvHandle{});
	ctx.SetShaderResource(rhi::ShaderStage::Compute, 4, rhi::SrvHandle{});
	ctx.SetComputePipeline({});
}

void DeferredRenderer::ResolveDxrLightingToHdr()
{
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	// Any mode that renders below the display resolution: DLSS SR and RR alike.
	const bool superResolution = myTunables.dlssMode >= 2 && StreamlineDLSS::Get().IsAvailable();
	// Fog is applied before the temporal/DLSS resolve in both modes. When DLSS
	// upscales, it is applied at render resolution and becomes part of the image
	// DLSS reconstructs. This used to be skipped entirely under upscaling on the
	// grounds that fog belongs after DLSS at display resolution -- but the pass
	// meant to do that returns immediately for ray depth, so fog was simply
	// never drawn in any DLSS super-resolution mode.
	const bool atmosphereApplied = RenderAtmosphere(true, superResolution);
	const RenderTarget& foggedTarget = superResolution ? myAtmosphereRender.hdr : myHdr;
	rhi::SrvHandle resolvedSrv = atmosphereApplied ? foggedTarget.GetSrv() : myDxrLightingSrv;
	const bool dlaaRequested = (myTunables.dlssMode > 0 || myTunables.dlaaEnabled || myTunables.rayReconstructionEnabled) && myTunables.dxrLightingView == 0 && StreamlineDLSS::Get().IsAvailable();
	// Ray Reconstruction is independent of the quality mode: it denoises and
	// upscales in one step, so it runs at DLAA (1:1) or at any DLSS ratio.
	const bool rrRequested = myTunables.rayReconstructionEnabled && StreamlineDLSS::Get().IsRayReconstructionAvailable();
	bool dlaaResolved = false;
	if (dlaaRequested && myDlaaTex.IsValid())
	{
		TGA_PROFILE_SCOPE(myProfiler, rrRequested ? "DLSS Ray Reconstruction" : "DLSS / DLAA");
		const rhi::TextureHandle color = atmosphereApplied ? foggedTarget.GetTextureHandle() : myDxrLightingTex;
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
		StreamlineDLSS::Get().SetPreset(myTunables.dlssPreset);
		const Matrix4x4f previousToWorld = myResolvePreviousWorldToClip.GetInverse();
		const Matrix4x4f clipToPrevious = worldToClip.GetInverse() * myResolvePreviousWorldToClip;
		const Matrix4x4f previousToClip = previousToWorld * worldToClip;
		if (rrRequested)
		{
			const Matrix4x4f viewToWorld = myWorldToView.GetInverse();
			dlaaResolved = StreamlineDLSS::Get().EvaluateRayReconstruction(
				DX11::Rhi()->GetNativeCommandList(), DX11::Rhi()->GetNativeTexture(color), DX11::Rhi()->GetNativeTexture(myDlaaTex),
				DX11::Rhi()->GetNativeTexture(myTemporalTex[1]), DX11::Rhi()->GetNativeTexture(myTemporalTex[0]), DX11::Rhi()->GetNativeTexture(myTemporalTex[3]),
				DX11::Rhi()->GetNativeTexture(myTemporalTex[4]), DX11::Rhi()->GetNativeTexture(myTemporalTex[5]),
				myDxrRenderResolution.x, myDxrRenderResolution.y, myResolution.x, myResolution.y,
				myTunables.dlssMode > 0 ? myTunables.dlssMode : 1,
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
				if (dlaaResolved) INFO_PRINT("DXR denoiser: DLSS Ray Reconstruction active (%ux%u -> %ux%u)",
					myDxrRenderResolution.x, myDxrRenderResolution.y, myResolution.x, myResolution.y);
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
		TGA_PROFILE_SCOPE(myProfiler, "TAA resolve");
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
	}
	// Only the *native* resolve invalidates history when it is skipped. This
	// used to be an unconditional `else`, which ran whenever DLSS/DLAA/RR had
	// resolved instead -- clearing the flag that branch had just set, so every
	// frame afterwards passed reset=true to Streamline. DLSS then rebuilt from
	// a single jittered frame each time, which is the sub-pixel wobble: it
	// tracked the jitter amplitude and ignored its sign, exactly as measured.
	else if (!dlaaResolved) myTaaHistoryValid = false;
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
	ctx.SetSampler(rhi::ShaderStage::Pixel, 0, myDxrMaterialSampler);
	BindFullscreen(myDxrCopyPs);
	ctx.Draw(3, 0);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, rhi::SrvHandle{});
	// Light uploads happen before BuildFrame.  The flag is intentionally kept
	// through both the ray pass and this resolve, then becomes frame-local.
	myTaaLightingChanged = false;
}

// The forward transparent pass depth-tests against DX11::DepthBuffer, which
// the DXR renderer never rasterises. Fill it from the ray-traced device depth.
void DeferredRenderer::WriteDxrDepthToDepthBuffer()
{
	if (!myDxrDepthPs || !myTemporalSrv[1].IsValid()) return;
	TGA_PROFILE_SCOPE(myProfiler, "DXR depth for transparents");
	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	ctx.ClearDepthStencil(DX11::DepthBuffer->GetDsv(), 1.f, 0);
	SetTargets(ctx, {}, DX11::DepthBuffer->GetDsv(), myResolution);
	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(BlendState::Disabled);
	gss.SetDepthStencilState(DepthStencilState::WriteLessOrEqual);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, myTemporalSrv[1]);
	BindFullscreen(myDxrDepthPs);
	ctx.Draw(3, 0);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 1, {});
	gss.SetDepthStencilState(DepthStencilState::WriteLess);
}
