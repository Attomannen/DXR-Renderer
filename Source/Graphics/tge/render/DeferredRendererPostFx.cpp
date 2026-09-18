#include "stdafx.h"
#include "DeferredRendererInternal.h"

// DeferredRenderer: Bloom, exposure metering and the final composite / tonemap.

using namespace Tga;

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

	// --- bloom: prefilter HDR -> mip[0], downsample chain, additive tent upsample ---
	if (myTunables.bloomEnabled)
	{
		TGA_PROFILE_SCOPE(myProfiler, "Bloom");
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
		TGA_PROFILE_SCOPE(myProfiler, "Exposure metering");
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
