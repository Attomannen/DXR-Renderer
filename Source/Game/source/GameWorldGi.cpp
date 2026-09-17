#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: Reflection probe and GI irradiance-volume capture.

bool GameWorld::Impl::RebuildWorldEnvironmentPrefilter()
{
	worldEnvironmentPrefiltered.Reset();
	if (!probePrefilter || !fallbackCube) return false;

	// DX12 TextureResource intentionally does not expose its native texture
	// descriptor. The shipped environments are 512px faces; on DX11 retain
	// the exact source size so importance-sampling chooses the correct LOD.
	uint32_t sourceResolution = 512;
	if (DX11::Rhi() && DX11::Rhi()->GetBackend() == rhi::Backend::DX11 &&
		fallbackCube->GetShaderResourceView())
	{
		sourceResolution = std::max(1u, fallbackCube->CalculateTextureSize().x);
	}

	if (!probePrefilter->GeneratePrefilteredCubemap(
		fallbackCube->GetSrv(), sourceResolution, 128, 128, worldEnvironmentPrefiltered))
	{
		ERROR_PRINT("environment IBL: prefilter failed; DXR will use the source cubemap.");
		return false;
	}
	return true;
}

void GameWorld::Impl::StartGiPrime()
{
	giScheduler.Restart();
	if (deferred && deferred->HasGi()) deferred->ClearGi();
}

void GameWorld::Impl::CaptureProbeImpl(GraphicsEngine& ge)
{
	if (!probePrefilter || !probeFaceRt || !probeFaceDepth || !fallbackCube) return;

	auto& gss = ge.GetGraphicsStateStack();
	auto& mdl = ge.GetModelDrawer();
	const Camera savedCam = gss.GetCamera();

	// bind fallback cube for the capture's own IBL + skybox
	AmbientLight capAmb = ambient;
	capAmb.cubemap = fallbackCube;
	gss.SetAmbientLight(capAmb);

	const VertexShader* skyVS = DX11::LoadVertexShader("Shaders/SkyboxVS");
	const PixelShader*  skyPS = DX11::LoadPixelShader("Shaders/SkyboxPS");

	auto faceCb = [&](uint32_t face)
	{
		probeFaceRt->SetAsActiveTarget(probeFaceDepth.get());
		probeFaceRt->Clear({ 0, 0, 0, 1 });
		probeFaceDepth->Clear();

		Camera cam;
		cam.SetTransform(CubemapPrefilter::GetCubemapCameraTransform(face, probePos));
		cam.SetPerspectiveProjection(90.f, { (float)kProbeRes, (float)kProbeRes }, 1.f, 100000.f);
		gss.SetCamera(cam);
		gss.UpdateGpuStates(true);

		// skybox (fullscreen tri sampling the fallback cube at t0)
		// `.module.IsValid()`, not the DX11-only `->shader` ComPtr -- that's
		// never populated on DX12 by design (see DX11::ForceLoad*Shader), so
		// checking it here silently skipped the skybox draw on every DX12
		// GI-probe capture (found 2026-09-12).
		if (skyVS && skyVS->module.IsValid() && skyPS && skyPS->module.IsValid())
		{
			gss.Push();
			gss.SetDepthStencilState(DepthStencilState::ReadOnlyLessOrEqual);
			gss.SetRasterizerState(RasterizerState::NoFaceCulling);
			gss.SetCustomShaderParameters({ 0.f, 1.f, 0.f, 0.f });
			gss.UpdateGpuStates();
			rhi::ICommandContext& skyCtx = DX11::Rhi()->GetContext();
			skyCtx.SetShaderResource(rhi::ShaderStage::Pixel, 0, fallbackCube->GetSrv());
			skyCtx.SetPrimitiveTopology(rhi::Topology::TriangleList);
			skyCtx.SetInputLayout({}, nullptr, 0);
			skyCtx.SetVertexBuffer(0, {}, 0, 0);
			skyCtx.SetIndexBuffer({}, rhi::Format::R32_UInt, 0);
			skyCtx.SetVertexShader(skyVS->module);
			skyCtx.SetPixelShader(skyPS->module);
			skyCtx.Draw(3, 0);
			gss.Pop();
			gss.UpdateGpuStates(true);
		}

		mdl.SetCullFrustum(nullptr);
		for (ModelInstance& m : models) mdl.DrawPbr(m);
	};

	if (probePrefilter->CaptureSceneToCubemap(*probeFaceRt, probeFaceDepth.get(), faceCb, probeBase))
	{
		probePrefilter->GeneratePrefilteredCubemap(
			probeBase.GetSrv(), probeBase.size, 128, 128, probePrefiltered);
	}

	gss.SetCamera(savedCam);
	gss.SetAmbientLight(ambient);
	gss.UpdateGpuStates(true);
	DX11::BackBuffer->SetAsActiveTarget(DX11::DepthBuffer);
}

void GameWorld::Impl::CaptureGiProbesImpl(GraphicsEngine& ge)
{
	DeferredRenderer* dr = deferred;
	if (!dr || !dr->HasGi()) return;

	// Ray-traced capture (DeferredRenderer::GiProjectProbeRT) needs none of
	// the raster prerequisites below -- no cubemap render target, no sky
	// shaders, no per-face scene redraw. Falls back to the raster path if
	// DXR isn't actually available even though the toggle is on.
	const bool useRT = giUseRT && dr->HasGiRT();
	if (!useRT && (!probePrefilter || !giFaceRt || !giFaceDepth || !fallbackCube)) return;

	auto& gss = ge.GetGraphicsStateStack();
	auto& mdl = ge.GetModelDrawer();
	const Camera savedCam = gss.GetCamera();

	// A sealed scene has no sky during capture.
	const bool sealed = sealScene;

	AmbientLight capAmb = ambient;
	capAmb.cubemap = sealed ? nullptr : fallbackCube;
	gss.SetAmbientLight(capAmb);

	const VertexShader* skyVS = DX11::LoadVertexShader("Shaders/SkyboxVS");
	const PixelShader*  skyPS = DX11::LoadPixelShader("Shaders/SkyboxPS");

	const int total = giCx * giCy * giCz;
	if (total <= 0) return;

	GiProbeScheduler::Settings schedule;
	schedule.rayTraced = useRT;
	schedule.primeBatch = giPrimeBatch;
	schedule.trickleBatch = giTrickle;
	schedule.probeBudget = giProbeBudget;
	schedule.raysPerProbe = giRTRayCount;
	schedule.hysteresis = giHysteresis;
	const GiProbeScheduler::Batch batch = giScheduler.Next(total, schedule);

	std::vector<DeferredRenderer::GiProbeBatchEntry> rtBatchEntries;
	if (useRT) rtBatchEntries.reserve(batch.probes.size());
	for (const int p : batch.probes)
	{
		const int px = p % giCx;
		const int py = (p / giCx) % giCy;
		const int pz = p / (giCx * giCy);
		const Vector3f pos = giOrigin + Vector3f{ px * giSpacing.x, py * giSpacing.y, pz * giSpacing.z };

		if (useRT)
		{
			// No cubemap, no per-face scene redraw -- just ray-trace straight
			// from the probe position. Collect rather than dispatch, so the
			// whole batch goes out as one dispatch after this loop.
			rtBatchEntries.push_back({ pos, p });
			continue;
		}

		auto faceCb = [&](uint32_t face)
		{
			giFaceRt->SetAsActiveTarget(giFaceDepth.get());
			giFaceRt->Clear({ 0, 0, 0, 0 });   // alpha 0 = sky; geometry writes alpha 1
			giFaceDepth->Clear();

			Camera cam;
			cam.SetTransform(CubemapPrefilter::GetCubemapCameraTransform(face, pos));
			cam.SetPerspectiveProjection(90.f, { (float)kGiFaceRes, (float)kGiFaceRes }, 1.f, 5000.f);
			gss.SetCamera(cam);
			gss.UpdateGpuStates(true);

			// See the other skybox check above for why this is `.module.IsValid()`.
			if (!sealed && skyVS && skyVS->module.IsValid() && skyPS && skyPS->module.IsValid())
			{
				gss.Push();
				gss.SetDepthStencilState(DepthStencilState::ReadOnlyLessOrEqual);
				gss.SetRasterizerState(RasterizerState::NoFaceCulling);
				gss.SetCustomShaderParameters({ 0.f, 1.f, 0.f, 0.f });
				gss.UpdateGpuStates();
				rhi::ICommandContext& skyCtx = DX11::Rhi()->GetContext();
				skyCtx.SetShaderResource(rhi::ShaderStage::Pixel, 0, fallbackCube->GetSrv());
				skyCtx.SetPrimitiveTopology(rhi::Topology::TriangleList);
				skyCtx.SetInputLayout({}, nullptr, 0);
				skyCtx.SetVertexBuffer(0, {}, 0, 0);
				skyCtx.SetIndexBuffer({}, rhi::Format::R32_UInt, 0);
				skyCtx.SetVertexShader(skyVS->module);
				skyCtx.SetPixelShader(skyPS->module);
				skyCtx.Draw(3, 0);
				gss.Pop();
				gss.UpdateGpuStates(true);
			}

			// Cull sub-meshes to this face's 90 deg frustum -- keeps the GI
			// capture cheap even though the scene is redrawn per face.
			// Only `models` + (optional) skybox are drawn, so a prime is fully
			// deterministic when geometry and the placed lights are static.
			const Frustum ff = CalculateFrustum(cam);
			const ModelShader& psh = mdl.GetPbrShader();
			for (ModelInstance& m : models) m.Render(psh, ff);
		};

		if (probePrefilter->CaptureSceneToCubemap(*giFaceRt, giFaceDepth.get(), faceCb, giCube, &giDepthCube))
		{
			// Priming replaces (deterministic); only the live trickle blends.
			dr->GiProjectProbe(giCube.GetSrv(), p, batch.hysteresis, kGiFaceRes);
		}
	}

	if (!rtBatchEntries.empty())
	{
		const float hyst = batch.hysteresis;
		const int rayCount = giRTRayCount;
		if (giBatchProbes)
			dr->GiProjectProbeBatchRT(rtBatchEntries.data(), (int)rtBatchEntries.size(), hyst, rayCount, giFireflyClamp);
		else
			// A/B only (BENCH_GI_BATCH=0), same role as BENCH_CLUSTERED's
			// brute-force path: one Dispatch(1,1,1) per probe, each a single
			// 64-thread group with a full UAV barrier after it.
			for (const DeferredRenderer::GiProbeBatchEntry& e : rtBatchEntries)
				dr->GiProjectProbeRT(e.position, e.index, hyst, rayCount, giFireflyClamp);
	}

	gss.SetCamera(savedCam);
	gss.SetAmbientLight(ambient);
	gss.UpdateGpuStates(true);
	DX11::BackBuffer->SetAsActiveTarget(DX11::DepthBuffer);
}
