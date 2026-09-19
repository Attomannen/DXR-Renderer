#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: Bloom, exposure metering and the final composite / tonemap.

using namespace Ag;

// Grading constants, filled identically wherever PostFxCb is built.
//
// Composite() builds its own copy of the buffer rather than going through
// PostFxFullscreen, so anything added to one and not the other silently reads
// as zero -- which is exactly how the first version of this shipped with every
// grading slider doing nothing at all.
static void FillGradeConstants(PostFxCb& c, const DeferredRenderer::Tunables& t)
{
	c.gradeEnabled = t.gradeEnabled ? 1.f : 0.f;
	c.gradeTemperature = std::clamp(t.gradeTemperature, -1.f, 1.f);
	c.gradeTint = std::clamp(t.gradeTint, -1.f, 1.f);
	c.gradeContrast = std::max(0.f, t.gradeContrast);
	c.gradeSaturation = std::max(0.f, t.gradeSaturation);
	for (int k = 0; k < 3; ++k)
	{
		c.gradeLift[k]  = std::clamp(t.gradeLift[k], -1.f, 1.f);
		c.gradeGamma[k] = std::clamp(t.gradeGamma[k], 0.1f, 4.f);
		c.gradeGain[k]  = std::max(0.f, t.gradeGain[k]);
	}
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
	if (!myExposure[0].GetSrv().IsValid())
	{
		myExposure[0] = RenderTarget::Create({ 1, 1 }, rhi::Format::R32_Float);
		myExposure[1] = RenderTarget::Create({ 1, 1 }, rhi::Format::R32_Float);
		myExposureCleared = false;
	}
	// Half resolution: the gather is the expensive part and a defocused image
	// has no high frequencies left to lose. The full-resolution scratch exists
	// because the composite reads the sharp frame and cannot write to it.
	myDofHalfSize = { std::max(1u, aResolution.x / 2), std::max(1u, aResolution.y / 2) };
	myDofHalf = RenderTarget::Create(myDofHalfSize, rhi::Format::R16G16B16A16_Float);
	myDofBlur = RenderTarget::Create(myDofHalfSize, rhi::Format::R16G16B16A16_Float);
	myDofFull = RenderTarget::Create(aResolution, rhi::Format::R16G16B16A16_Float);

	// Motion blur. 16 display pixels a tile: small enough that one velocity is a
	// fair summary of what is in it, large enough that the tile pass is cheap
	// and the neighbour dilation reaches a useful distance.
	{
		rhi::IDevice* dev = DX11::Rhi();
		const uint32_t kMbTile = 16;
		myMbTileCount = { std::max(1u, (aResolution.x + kMbTile - 1) / kMbTile),
		                  std::max(1u, (aResolution.y + kMbTile - 1) / kMbTile) };
		auto make = [&](Vector2ui size, rhi::Format fmt, const char* name,
		                rhi::TextureHandle& tex, rhi::SrvHandle& srv, rhi::UavHandle& uav)
		{
			if (uav.IsValid()) dev->Destroy(uav);
			if (srv.IsValid()) dev->Destroy(srv);
			if (tex.IsValid()) dev->Destroy(tex);
			rhi::TextureDesc td{};
			td.width = size.x; td.height = size.y; td.mipLevels = 1;
			td.dimension = rhi::TextureDimension::Tex2D;
			td.format = fmt;
			td.bind = rhi::TextureBind::ShaderResource | rhi::TextureBind::UnorderedAccess;
			td.debugName = name;
			tex = dev->CreateTexture(td);
			srv = dev->CreateSrv(tex, rhi::SrvDesc{});
			uav = dev->CreateUav(tex, rhi::UavDesc{});
		};
		make(myMbTileCount, rhi::Format::R16G16_Float, "MotionTileMax", myMbTileTex, myMbTileSrv, myMbTileUav);
		make(myMbTileCount, rhi::Format::R16G16_Float, "MotionNeighbourMax", myMbNeighbourTex, myMbNeighbourSrv, myMbNeighbourUav);
		make(aResolution, rhi::Format::R16G16B16A16_Float, "MotionBlurOut", myMbOutTex, myMbOutSrv, myMbOutUav);
	}

	return true;
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
		c.manualEv100 = Photometry::Ev100FromCamera(t.cameraAperture, t.cameraShutter, t.cameraIso);
		c.autoEvMin = t.autoEvMin;
		c.autoEvMax = t.autoEvMax;
		c.exposureAuto = t.exposureAuto ? 1.f : 0.f;
		c.tonemapper = uint32_t(std::clamp(t.tonemapper, 0, 3));
		c.hdrPreExposed = PreExposureActive() ? 1u : 0u;
		c.exposureComp = t.exposureComp;
		c.adaptRate = t.exposureSpeed;
		c.adaptStrength = std::clamp(t.exposureAdaptStrength, 0.f, 1.f);
		{
			// Thin lens, in full-resolution pixels. Everything here is camera
			// geometry, so it collapses to one scalar: blur radius equals this
			// times (z - focus) / z.
			const float focusMm = std::max(t.dofFocusDistance, t.dofFocalLength * 0.002f) * 1000.f;
			const float f = std::max(1.f, t.dofFocalLength);
			const float pixelsPerMm = (float)myResolution.x / 36.f;   // 35 mm sensor width
			c.dofCocScale = (f * f) / (std::max(0.7f, t.dofAperture) * std::max(1.f, focusMm - f)) * pixelsPerMm;
			// In engine units, which are metres. myNear/myFar are metres, so the
			// shader's linearised depth is centimetres too; handing it a focus
			// distance in metres made everything a hundred times out of focus
			// and nothing in the frame was ever sharp. The tunable stays in
			// metres because that is what a focus dial reads.
			// Linearised depth is metres now, same as the tunable, so this no longer
			// converts. It used to multiply by 100 to reach the centimetre domain.
			c.dofFocusDistance = std::max(0.01f, t.dofFocusDistance);
			c.dofMaxRadius = std::max(1.f, t.dofMaxRadius);
			c.dofEnabled = t.dofEnabled ? 1.f : 0.f;
			c.dofNear = myNear; c.dofFar = myFar;
		}
		{
			// Shutter angle over a full rotation is the fraction of the frame
			// the shutter is open, and the velocity buffer already spans exactly
			// one frame -- so the trail length is just that fraction of it.
			c.mbEnabled = t.mbEnabled ? 1.f : 0.f;
			const float shutterFraction = std::clamp(t.mbShutterAngle, 0.f, 360.f) / 360.f;
			// Render pixels to display pixels: the velocity buffer is written at
			// render resolution and this pass runs at display resolution.
			const float renderToDisplay = myDxrRenderResolution.x > 0
				? (float)myResolution.x / (float)myDxrRenderResolution.x : 1.f;
			c.mbVelocityScale = shutterFraction * renderToDisplay;
			c.mbTileSize = 16.f;
			c.mbMaxRadius = std::max(1.f, t.mbMaxRadius);
		}
		FillGradeConstants(c, t);
		c.deltaTime = std::min(Application::GetInstance()->GetDeltaTime(), 0.1f);
		myPostFxCb.Update(DX11::Rhi()->GetContext(), c);
	}

	auto& gss = GraphicsEngine::GetInstance()->GetGraphicsStateStack();
	gss.SetBlendState(aAdditive ? BlendState::AdditiveBlend : BlendState::Disabled);

	rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
	SetTargets(ctx, { aDst.GetRtv() }, {}, aDstSize);

	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, (uint32_t)aSrvCount, aSrvs);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, myExposure[myPreExposureIndex].GetSrv());
	ctx.SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(ctx);   // b10

	BindFullscreen(aPs);
	ctx.Draw(3, 0);

	const rhi::SrvHandle nulls[4] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, (uint32_t)(aSrvCount < 4 ? aSrvCount : 4), nulls);
	gss.SetBlendState(BlendState::Disabled);
}

void DeferredRenderer::RenderPostFx()
{
	if (!IsPostFx()) return;

	EnsureExposureHistory();
	// The HDR this frame was rendered with the history as it stands now; the
	// adapt pass below writes the other target.
	myPreExposureIndex = myExposureSrc;

	const Vector2f hdrTexel{ 1.f / (float)myResolution.x, 1.f / (float)myResolution.y };

	// --- depth of field: CoC -> half-res bokeh gather -> composite over HDR ---
	//
	// Before bloom, deliberately. Bloom is the lens scattering light inside
	// itself, so it should see the defocused image: a blown-out highlight that
	// is out of focus blooms as a wide soft disc, not as a sharp point that is
	// blurred afterwards. Running it the other way round is what makes bokeh
	// look pasted on.
	if (myTunables.dofEnabled && myDofCocPs && myDofBlurPs && myDofCompositePs && myDofFull.GetSrv().IsValid())
	{
		AG_PROFILE_SCOPE(myProfiler, "Depth of field");
		rhi::ICommandContext& ctx = DX11::Rhi()->GetContext();
		// Ray depth when the DXR renderer owns the frame, the raster depth
		// buffer otherwise. Under upscaling the ray depth is at render
		// resolution while this pass runs at display resolution; that is fine,
		// because it is sampled by uv and a circle of confusion is a
		// low-frequency quantity -- it is the COLOUR that needs full resolution.
		const rhi::SrvHandle depth = (IsDxrRenderer() || IsDxrFullscreen()) && myTemporalSrv[1].IsValid()
			? myTemporalSrv[1] : DX11::DepthBuffer->GetSrv();

		const rhi::SrvHandle cocSrvs[2] = { myHdr.GetSrv(), depth };
		PostFxFullscreen(myDofCocPs, myDofHalf, myDofHalfSize, cocSrvs, 2, hdrTexel);

		const Vector2f halfTexel{ 1.f / (float)myDofHalfSize.x, 1.f / (float)myDofHalfSize.y };
		rhi::SrvHandle blurSrc = myDofHalf.GetSrv();
		PostFxFullscreen(myDofBlurPs, myDofBlur, myDofHalfSize, &blurSrc, 1, halfTexel);

		const rhi::SrvHandle compositeSrvs[2] = { myHdr.GetSrv(), myDofBlur.GetSrv() };
		PostFxFullscreen(myDofCompositePs, myDofFull, myResolution, compositeSrvs, 2, hdrTexel);
		// The composite reads the sharp frame, so it cannot write to it.
		ctx.CopyTexture(myHdr.GetTextureHandle(), myDofFull.GetTextureHandle());
	}

	// --- motion blur: tile max -> neighbour max -> reconstruction gather ---
	//
	// After depth of field and before bloom. After, because a defocused
	// highlight that is also moving should smear as a disc, not as a point that
	// is blurred twice; before, for the same reason depth of field is -- bloom
	// is the lens scattering the image it actually forms.
	//
	// All three passes are COMPUTE. The velocity target is a UAV written by the
	// ray pass, and reading it from a pixel shader blacked out the entire frame:
	// with the tile pass reading it the frame was black, with the identical pass
	// reading nothing it was correct, and the barriers were present and tracked
	// either way. Compute is the path every other consumer of that buffer uses.
	//
	// DXR path only -- the raster path writes no velocity.
	if (myTunables.mbEnabled && myMbTileMaxCs && myMbNeighbourCs && myMbBlurCs && myMbCb.IsValid()
		&& myMbOutUav.IsValid() && myTemporalSrv[0].IsValid() && myTemporalSrv[1].IsValid())
	{
		AG_PROFILE_SCOPE(myProfiler, "Motion blur");
		rhi::IDevice* dev = DX11::Rhi();
		rhi::ICommandContext& ctx = dev->GetContext();

		MotionBlurCb cb{};
		cb.tileCount[0] = myMbTileCount.x; cb.tileCount[1] = myMbTileCount.y;
		cb.sourceSize[0] = myDxrRenderResolution.x; cb.sourceSize[1] = myDxrRenderResolution.y;
		cb.outputSize[0] = myResolution.x; cb.outputSize[1] = myResolution.y;
		cb.tileSize = 16;
		// Shutter angle over a full rotation is the fraction of the frame the
		// shutter is open, and the velocity buffer already spans exactly one
		// frame, so the trail is that fraction of it. Times render-to-display,
		// because the velocity is in render pixels and this runs at display.
		cb.velocityScale = (std::clamp(myTunables.mbShutterAngle, 0.f, 360.f) / 360.f)
			* (myDxrRenderResolution.x > 0 ? (float)myResolution.x / (float)myDxrRenderResolution.x : 1.f);
		cb.nearPlane = myNear; cb.farPlane = myFar;
		cb.maxRadius = std::max(1.f, myTunables.mbMaxRadius);
		myMbCb.Update(ctx, cb);

		const auto dispatch = [&](const ComputeShader* cs, Vector2ui size)
		{
			rhi::ComputePipelineDesc pd; pd.cs = cs->module;
			ctx.SetComputePipeline(dev->CreateComputePipeline(pd));
			myMbCb.Bind(ctx);
			ctx.Dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);
			ctx.SetComputePipeline({});
		};

		ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myTemporalSrv[0]);
		ctx.SetUnorderedAccess(0, myMbTileUav);
		dispatch(myMbTileMaxCs, myMbTileCount);
		ctx.SetUnorderedAccess(0, {});

		ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myMbTileSrv);
		ctx.SetUnorderedAccess(0, myMbNeighbourUav);
		dispatch(myMbNeighbourCs, myMbTileCount);
		ctx.SetUnorderedAccess(0, {});

		ctx.SetShaderResource(rhi::ShaderStage::Compute, 0, myHdr.GetSrv());
		ctx.SetShaderResource(rhi::ShaderStage::Compute, 1, myTemporalSrv[0]);
		ctx.SetShaderResource(rhi::ShaderStage::Compute, 2, myMbNeighbourSrv);
		ctx.SetShaderResource(rhi::ShaderStage::Compute, 3, myTemporalSrv[1]);
		ctx.SetSampler(rhi::ShaderStage::Compute, 0, myLinearSampler);
		ctx.SetUnorderedAccess(0, myMbOutUav);
		dispatch(myMbBlurCs, myResolution);
		ctx.SetUnorderedAccess(0, {});
		const rhi::SrvHandle nulls[4] = {};
		ctx.SetShaderResources(rhi::ShaderStage::Compute, 0, 4, nulls);

		// The gather reads the unblurred frame, so it cannot write to it.
		ctx.CopyTexture(myHdr.GetTextureHandle(), myMbOutTex);
	}

	// --- bloom: prefilter HDR -> mip[0], downsample chain, additive tent upsample ---
	if (myTunables.bloomEnabled)
	{
		AG_PROFILE_SCOPE(myProfiler, "Bloom");
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
	// Manual exposure still runs the 1x1 adapt pass (it just writes the camera
	// EV) so the history is always the EV the next frame is pre-exposed with.
	if (myTunables.exposureAuto)
	{
		AG_PROFILE_SCOPE(myProfiler, "Exposure metering");
		const unsigned expSizes[7] = { 64, 32, 16, 8, 4, 2, 1 };
		rhi::SrvHandle hdrSrv = myHdr.GetSrv();
		PostFxFullscreen(myExposureLumaPs, myExpMip[0], { 64, 64 }, &hdrSrv, 1, hdrTexel);
		for (int i = 1; i < 7; ++i)
		{
			rhi::SrvHandle s = myExpMip[i - 1].GetSrv();
			const float p = 1.f / (float)expSizes[i - 1];
			PostFxFullscreen(myExposureDownPs, myExpMip[i], { expSizes[i], expSizes[i] }, &s, 1, { p, p });
		}
	}
	{
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

	// (HDR + bloom) * exposure, then tonemap -> backbuffer (already bound).
	{
		const Tunables& t = myTunables;
		PostFxCb c{};
		c.texelSize[0] = 1.f / (float)myResolution.x;
		c.texelSize[1] = 1.f / (float)myResolution.y;
		c.bloomThreshold = t.bloomThreshold;
		c.bloomKnee = t.bloomKnee;
		c.bloomIntensity = t.bloomEnabled ? t.bloomIntensity : 0.f;
		c.manualEv100 = Photometry::Ev100FromCamera(t.cameraAperture, t.cameraShutter, t.cameraIso);
		c.autoEvMin = t.autoEvMin;
		c.autoEvMax = t.autoEvMax;
		c.exposureAuto = t.exposureAuto ? 1.f : 0.f;
		c.tonemapper = uint32_t(std::clamp(t.tonemapper, 0, 3));
		c.hdrPreExposed = PreExposureActive() ? 1u : 0u;
		c.exposureComp = t.exposureComp;
		c.adaptRate = t.exposureSpeed;
		c.adaptStrength = std::clamp(t.exposureAdaptStrength, 0.f, 1.f);
		FillGradeConstants(c, t);
		c.deltaTime = std::min(Application::GetInstance()->GetDeltaTime(), 0.1f);
		myPostFxCb.Update(DX11::Rhi()->GetContext(), c);
	}

	const rhi::SrvHandle srvs[3] = {
		myHdr.GetSrv(),
		myBloomMip[0].GetSrv(),
		myExposure[myExposureSrc].GetSrv(),
	};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 3, srvs);
	ctx.SetShaderResource(rhi::ShaderStage::Pixel, 5, myExposure[myPreExposureIndex].GetSrv());
	ctx.SetSampler(rhi::ShaderStage::Pixel, 3, myLinearSampler);
	myPostFxCb.Bind(ctx);   // b10

	BindFullscreen(myCompositePs);
	ctx.Draw(3, 0);

	const rhi::SrvHandle nulls[3] = {};
	ctx.SetShaderResources(rhi::ShaderStage::Pixel, 0, 3, nulls);
}
