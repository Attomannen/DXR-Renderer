#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include "BenchConfig.h"
#include <DirectXTex/ScreenGrab/ScreenGrab11.h>
#pragma comment(lib, "windowscodecs.lib")

GameWorld* GameWorld::ourInstance = nullptr;

GameWorld::GameWorld() : myImpl(std::make_unique<Impl>()) { ourInstance = this; }
GameWorld::~GameWorld() { ourInstance = nullptr; }

void GameWorld::OnWinProc(unsigned int aMessage, unsigned long long aWParam, long long aLParam)
{
	if (myImpl->input)
		myImpl->input->UpdateEvents((UINT)aMessage, (WPARAM)aWParam, (LPARAM)aLParam);
}

void GameWorld::Init()
{
	Impl& s = *myImpl;

	BenchConfig::ApplyStartupOverrides(s);

	// Materials rendered in the forward transparent pass (substring match, after
	// lowercasing + stripping a Blender ".003" suffix). BENCH_TRANSPARENT_MATS
	// (comma-separated) overrides the default; empty disables the pass.
	{
		const std::string mats = s.bench.transparentMats;
		size_t start = 0;
		while (start <= mats.size())
		{
			const size_t comma = mats.find(',', start);
			std::string tok = mats.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			// trim
			while (!tok.empty() && (tok.front() == ' ')) tok.erase(tok.begin());
			while (!tok.empty() && (tok.back() == ' ')) tok.pop_back();
			if (!tok.empty()) s.transparentMatKeys.push_back(LowerStr(tok));
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
	}

	s.ambient.type = AmbientLightType::Custom;
	// BENCH_CUBEMAP: env_powerplant | env_studio | env_slipway | horizonCubeMap (default)
	// or any "Textures/<name>.dds". Metals reflect this, so a flat gradient makes them dead.
	std::string cubeName = s.bench.cubemap;
	if (cubeName.find('/') == std::string::npos) cubeName = "Textures/" + cubeName;
	if (cubeName.rfind(".dds") == std::string::npos) cubeName += ".dds";
	s.ambient.cubemap = GraphicsEngine::GetInstance()->GetTextureManager()
		.GetTexture(cubeName.c_str(), TextureSrgbMode::None);
	s.fallbackCube = s.ambient.cubemap;

	const Vector2ui res = Application::GetInstance()->GetRenderSize();
	s.camera.SetPerspectiveProjection(90.f, { (float)res.x, (float)res.y }, 1.f, 100000.f);
	s.cameraProjectionSize = res;

	// Load the scene (instances, bounds, light rig, start camera). Runtime scene
	// switching (ImGui) calls this again with aEnv = false.
	if (s.currentScene.empty())
	{
		auto firstScene = FindFirstTgsScene();
		if (!firstScene)
		{
			ERROR_PRINT("bench: no .tgs scene found under %s", Settings::GameAssetRoot().c_str());
			return;
		}
		s.currentScene = *firstScene;
		INFO_PRINT("bench: BENCH_SCENE not set; loading first scene '%s'", s.currentScene.c_str());
	}
	{
		TGA_CPU_SCOPE("Load scene content");
		if (!s.LoadSceneContent(s.currentScene, true))
			return;
	}

	if (HWND* hwnd = Application::GetInstance()->GetHWND())
		s.input = std::make_unique<InputManager>(*hwnd);

	{
		TGA_CPU_SCOPE("GPU profiler init");
		s.gpu.Init();
	}

	// --- reflection probe (Phase 5 stage A) ---
	// Auto probe: scene centre dropped toward the lower third; box = scene bounds.
	s.probePos      = s.sceneCenter - Vector3f{ 0.f, s.sceneExtents.y * 0.32f, 0.f };
	s.probeBox      = s.sceneExtents * 1.35f;   // a bit generous so geometry sits well inside
	// Artist override: <scene>_probes.json = { "probes": [ { "pos":[x,y,z], "box":[hx,hy,hz] } ] }
	{
		const std::string pf = s.currentScene.empty() ? std::string("bench_probes.json")
		                                              : ("bench_probes_" + s.currentScene + ".json");
		std::ifstream in(pf);
		if (in)
		{
			try
			{
				nlohmann::json j; in >> j;
				if (j.contains("probes") && j["probes"].is_array() && !j["probes"].empty())
				{
					const auto& p0 = j["probes"][0];
					auto a3 = [](const nlohmann::json& v, Vector3f d) {
						return (v.is_array() && v.size() >= 3)
							? Vector3f{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() } : d;
					};
					if (p0.contains("pos")) s.probePos = a3(p0["pos"], s.probePos);
					if (p0.contains("box")) s.probeBox = a3(p0["box"], s.probeBox);
					INFO_PRINT("reflection probe: from %s  pos(%.0f,%.0f,%.0f) box(%.0f,%.0f,%.0f)", pf.c_str(),
						s.probePos.x, s.probePos.y, s.probePos.z, s.probeBox.x, s.probeBox.y, s.probeBox.z);
				}
			}
			catch (...) { ERROR_PRINT("reflection probe: bad %s", pf.c_str()); }
		}
	}
	BenchConfig::ApplyContentOverrides(s);
	s.probePrefilter = std::make_unique<CubemapPrefilter>();
	if (s.probePrefilter->Init())
	{
		s.probeFaceRt = std::make_unique<RenderTarget>(
			RenderTarget::Create({ (unsigned)Impl::kProbeRes, (unsigned)Impl::kProbeRes }, rhi::Format::R16G16B16A16_Float));
		s.probeFaceDepth = std::make_unique<DepthBuffer>(
			DepthBuffer::Create({ (unsigned)Impl::kProbeRes, (unsigned)Impl::kProbeRes }));
		s.giFaceRt = std::make_unique<RenderTarget>(
			RenderTarget::Create({ (unsigned)Impl::kGiFaceRes, (unsigned)Impl::kGiFaceRes }, rhi::Format::R16G16B16A16_Float));
		s.giFaceDepth = std::make_unique<DepthBuffer>(
			DepthBuffer::Create({ (unsigned)Impl::kGiFaceRes, (unsigned)Impl::kGiFaceRes }));
		// Precompute the selected authored sky once. This gives DXR the same
		// physically filtered IBL representation already used by raster probes.
		s.RebuildWorldEnvironmentPrefilter();
	}
	else
	{
		ERROR_PRINT("reflection probe: CubemapPrefilter::Init failed; probe disabled");
		s.probePrefilter.reset();
		s.probeEnabled = false;
		s.giEnabled = false;
	}

	BenchConfig::ApplyWorldOverrides(s);
	if (s.useDeferred)
	{
		// The engine owns the deferred renderer now; the bench just drives it.
		DeferredRenderer& dr = GraphicsEngine::GetInstance()->GetDeferredRenderer();
		if (dr.IsReady())
		{
			dr.OnResize(res);
			s.deferred = &dr;
			auto& tun = dr.GetTunables();
			BenchConfig::ApplyRendererOverrides(tun);
			// dlssMode selects the DXR render resolution, and that is baked into
			// the targets when they are created; recreate them for any upscaling mode.
			if (tun.dlssMode != 0) dr.RecreateDxrTargets();
		}
		else
		{
			ERROR_PRINT("Sponza bench: engine deferred renderer not ready, falling back to forward");
			s.deferred = nullptr;
			s.useDeferred = false;
		}
	}

	INFO_PRINT("Scene bench: scene '%s'  center(%.0f,%.0f,%.0f) extents(%.0f,%.0f,%.0f)  copies=%d  lights=%d  benchFrames=%d",
		s.currentScene.c_str(), s.sceneCenter.x, s.sceneCenter.y, s.sceneCenter.z,
		s.sceneExtents.x, s.sceneExtents.y, s.sceneExtents.z, s.sponzaCopies, (int)s.pointLights.size(), s.benchFrames);

	int totalTr = 0, totalOp = 0;
	for (size_t k = 0; k < s.models.size(); ++k) { totalTr += (int)s.transparentMeshes[k].size(); totalOp += (int)s.opaqueMeshes[k].size(); }
	INFO_PRINT("Sponza bench: transparent pass %s  (%d transparent sub-mesh(es), %d opaque; keys: %s)",
		s.anyTransparent ? "ON" : "off", totalTr, totalOp,
		s.transparentMatKeys.empty() ? "(none)" : "see BENCH_TRANSPARENT_MATS");
}

void GameWorld::Update(float aDeltaTime)
{
	Impl& s = *myImpl;
	s.cpuStart = std::chrono::high_resolution_clock::now();

	if (s.benchFrames > 0)
		s.SetScriptedCamera();
	else
		s.UpdateFreeFly(aDeltaTime);

	s.animTime += aDeltaTime;
	if (s.showOrbitBalls)
		s.orbitAngle += aDeltaTime * s.orbitSpeed;

	if (s.frame == 1) s.firstFrameMs = (double)aDeltaTime * 1000.0;
	if (s.benchFrames > 0 && s.frame == s.warmupFrames && s.bench.costSweep)
		s.StartCostSweep();

	// record previous frame's timing (skip warmup)
	if (s.frame > 0 && s.frame > s.warmupFrames && (s.benchFrames == 0 || s.frame <= s.benchFrames))
	{
		s.frameMs.push_back((double)aDeltaTime * 1000.0);
		s.drawCalls.push_back(DX11::GetPreviousDrawCallCount());
	}

	if (s.benchFrames > 0 && s.frame >= s.benchFrames)
	{
		s.WriteReport();
		PostQuitMessage(0);
	}
	++s.frame;
	s.ReleaseRetiredEnvironmentCubemaps();
}


void GameWorld::Render()
{
	Impl& s = *myImpl;
	GraphicsEngine& ge = *GraphicsEngine::GetInstance();
	GraphicsStateStack& gss = ge.GetGraphicsStateStack();

	// Open the GPU frame before any GPU work (GI probes, TLAS) so it is timed.
	s.gpu.BeginFrame();
	if (s.deferred) s.deferred->SetProfiler(&s.gpu);

	// Application updates its render size after the OS resize message has been
	// processed.  Rebuild the perspective matrix before submitting this frame;
	// otherwise the old aspect ratio is rasterized across the new backbuffer.
	const Vector2ui renderSize = Application::GetInstance()->GetRenderSize();
	if (renderSize != s.cameraProjectionSize && renderSize.x != 0 && renderSize.y != 0)
	{
		s.camera.SetPerspectiveProjection(90.f, { static_cast<float>(renderSize.x), static_cast<float>(renderSize.y) }, 1.f, 100000.f);
		s.cameraProjectionSize = renderSize;
	}

	// DX12 screenshot capture: DX11's own path (further down, in the
	// screenshotPath block) reaches into DX11::SwapChain/DX11::Context
	// directly, both null under DX12 -- CaptureBackBufferPng is the DX12-only
	// equivalent. Must run at the very START of the frame, before anything
	// touches this frame-in-flight slot's backbuffer texture: it reads the
	// LAST FULLY PRESENTED contents of that slot, which BeginFrame (called by
	// the main loop right before this) has already fence-waited to be idle.
	if (rhi::IDevice* dev = DX11::Rhi(); dev && dev->GetBackend() == rhi::Backend::DX12 &&
		!s.screenshotPath.empty() && !s.screenshotTaken)
	{
		// BENCH_SHOT_FRAME pins the capture to a chosen frame instead of the end
		// of the run. The scripted camera is parameterised by fraction-of-run and
		// the orbit completes two full loops, so the default (benchFrames - 2)
		// always lands back at the starting angle -- which for Sponza is inside a
		// wall. Any other viewpoint needs an explicit frame.
		const int shotFrame = s.shotFrame > 0 ? s.shotFrame : (s.benchFrames > 3 ? s.benchFrames - 2 : 120);
		// BENCH_SHOT_COUNT > 1 captures consecutive frames as name_0, name_1, ...
		// (for judging temporal stability within one run).
		const int shotCount = s.bench.shotCount;
		if (s.frame >= shotFrame)
		{
			const int index = s.frame - shotFrame;
			std::string path = s.screenshotPath;
			if (shotCount > 1)
			{
				const size_t dot = path.find_last_of('.');
				path = path.substr(0, dot) + "_" + std::to_string(index) + (dot == std::string::npos ? "" : path.substr(dot));
			}
			if (index + 1 >= shotCount) s.screenshotTaken = true;
			const std::wstring wpath(path.begin(), path.end());
			bool ok = dev->CaptureBackBufferPng(wpath.c_str());
			INFO_PRINT("Sponza bench: screenshot (DX12) -> %s (%s)", path.c_str(), ok ? "ok" : "failed");
		}
	}

	// Interactive tuning panel (free-fly runs only) — updates s.* live.
	if (s.benchFrames == 0 || s.bench.debugUi)
	{
		TGA_CPU_SCOPE("Debug UI");
		DrawDebugUI();
		s.DrawPerfOverlayImpl();
	}

	// Rebuild the sun / ambient from live state so slider tweaks take effect.
	s.dirLight.transform = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ s.sunPitch, s.sunYaw, 0.f });
	const Vector3f sunColor = s.SunColor() * s.SunIntensity();
	s.dirLight.color = Color{ sunColor.x, sunColor.y, sunColor.z };
	s.dirLight.softness = s.sunSoftness;
	s.ambient.color = Color{ s.ambientColor[0] * s.ambientScale, s.ambientColor[1] * s.ambientScale, s.ambientColor[2] * s.ambientScale };

	// A sealed scene gets no exterior sun and no sky IBL.
	const bool sealedRoom = s.sealScene;
	if (sealedRoom)
	{
		s.dirLight.color = Color{ 0.f, 0.f, 0.f, 1.f };
		s.ambient.color  = Color{ 0.015f, 0.016f, 0.018f, 1.f };   // faint floor so corners aren't crushed
	}

	if (s.deferred) {
		auto& lighting = s.deferred->GetTunables();
		lighting.dxrSunIntensity = 3.14159265f;
		lighting.dxrSunTint[0] = s.dirLight.color.r;
		lighting.dxrSunTint[1] = s.dirLight.color.g;
		lighting.dxrSunTint[2] = s.dirLight.color.b;

		// Procedural sky: physically-based ambient/IBL tied to the sun (see
		// DeferredRendererSky.cpp). Cheap to call every frame -- it only
		// actually re-marches the LUTs/cubemap when the sun moved or an
		// atmosphere tunable changed, and reports that back so the prefilter
		// (shared with the reflection probe's own capture path) only reruns
		// then too.
		if (lighting.proceduralSkyEnabled && !sealedRoom)
		{
			const Vector3f dirLightForward = s.dirLight.transform.GetForward();
			const Vector3f sunDirToLight{ -dirLightForward.x, -dirLightForward.y, -dirLightForward.z };
			const Vector3f sunRadiance{
				lighting.dxrSunTint[0] * lighting.dxrSunIntensity,
				lighting.dxrSunTint[1] * lighting.dxrSunIntensity,
				lighting.dxrSunTint[2] * lighting.dxrSunIntensity };
			const float cameraHeightCm = s.camera.GetTransform().GetPosition().y;
			const rhi::SrvHandle nightSkySrv = s.fallbackCube ? s.fallbackCube->GetSrv() : rhi::SrvHandle{};
			if (s.deferred->UpdateProceduralSky(sunDirToLight, sunRadiance, cameraHeightCm, nightSkySrv))
				s.RebuildWorldEnvironmentPrefilter();
		}
	}
	gss.SetCamera(s.camera);
	gss.SetDirectionalLight(s.dirLight);
	gss.ClearPointLights();

	// Reflection probe: re-capture every N frames, then point the IBL at it.
	if (s.probeEnabled && s.probePrefilter && !sealedRoom && !s.dxrRenderer)
	{
		if (--s.probeCountdown <= 0)
		{
			s.probeCountdown = s.probeInterval;
			s.CaptureProbeImpl(ge);
		}
		s.ambient.cubemap = s.probePrefiltered.resource ? s.probePrefiltered.resource.get() : s.fallbackCube;
	}
	else
	{
		// Procedural sky, when on, replaces the static fallback cubemap here --
		// the reflection probe branch above needs no change of its own, since
		// whatever it captures already includes the currently bound sky as its
		// own backdrop.
		const bool useProceduralSky = !sealedRoom && s.deferred && s.deferred->GetTunables().proceduralSkyEnabled
			&& s.worldEnvironmentPrefiltered.resource != nullptr;
		s.ambient.cubemap = sealedRoom ? nullptr
			: (useProceduralSky ? s.worldEnvironmentPrefiltered.resource.get() : s.fallbackCube);
	}

	// Emissive GI: prime the whole volume over the first ~1 s, then a slow trickle
	// so it tracks lighting changes without a per-frame cost.
	if (s.giEnabled && s.deferred && s.deferred->HasGi())
	{
		// Re-prime when the sun / ambient / cubemap changes so GI tracks it.
		if (s.giAutoReprime)
		{
			// GI capture only sees `models` + (unsealed) skybox + placed pointLights,
			// so those are the only inputs that change the volume. Quantised so
			// slider hover / float jitter can't retrigger a prime.
			const auto* skyTunForHash = s.deferred ? &s.deferred->GetTunables() : nullptr;
			const float h = std::round(
				s.sunPitch * 2.f + s.sunYaw * 1.3f + s.SunIntensity() * 40.f
				+ (s.SunColor().x + s.SunColor().y * 2.f + s.SunColor().z * 3.f) * 20.f
				+ (s.ambientColor[0] + s.ambientColor[1] + s.ambientColor[2]) * s.ambientScale * 50.f
				+ (float)s.cubemapIdx * 100.f
				+ (sealedRoom ? 777.f : 0.f)
				// Procedural sky tunables: the sky itself is already covered by
				// sunPitch/sunYaw above, but turbidity/ground albedo/the on-off
				// switch change its appearance without moving the sun.
				+ (skyTunForHash ? (skyTunForHash->proceduralSkyEnabled ? 555.f : 0.f)
					+ skyTunForHash->atmosphereTurbidity * 30.f
					+ skyTunForHash->groundAlbedo * 60.f : 0.f));
			// The physical sky scale is only known once the environment map has
			// been measured (a frame or two in), and the probes must be traced
			// with it; hash it separately so a tiny night sky still registers.
			// The measured environment average drifts by a few percent on every
			// sky refresh (camera height, sun creep); at 8 steps per stop that
			// re-primed -- i.e. blacked out -- the whole GI volume each time.
			// One step per half-stop only reacts to real lighting changes.
			const float skyH = std::round(std::log2(std::max(s.deferred->GetTunables().skyLuminanceNits, 1e-6f)) * 8.f
				+ std::log2(std::max(s.deferred->GetEnvironmentAverageLuminance(), 1e-9f)) * 2.f
				+ std::log2(std::max(s.sunIlluminanceLux, 1e-6f)) * 8.f);
			const float lightHash = h + skyH * 7919.f;
			if (lightHash != s.giLightHash)
			{
				s.giLightHash = lightHash;
				// A change mid-prime (typically the sky measurement arriving)
				// would otherwise leave half the volume traced with stale light.
				if (s.giScheduler.IsPriming()) s.StartGiPrime();
				else
				{
					if (s.giUseRT)
					{
						// Do not clear the whole cache while the user drags a sun.
						// Refresh one sweep with low hysteresis instead, so existing
						// indirect light remains stable and the new result converges.
						s.giKeepUpdating = true;
						s.giSkipCount = 0;
						s.giScheduler.RequestLightingRefresh(s.giCx * s.giCy * s.giCz);
					}
					else s.StartGiPrime();
				}
			}
		}
		if (s.giScheduler.IsPriming())
		{
			if (s.giUseRT) s.giRtCapturePending = true;
			else s.CaptureGiProbesImpl(ge);
		}
		else if (s.giKeepUpdating && --s.giSkipCount <= 0)
		{
			s.giSkipCount = std::max(1, s.giFrameSkip);
			if (s.giUseRT) s.giRtCapturePending = true;
			else s.CaptureGiProbesImpl(ge);
		}
	}

	gss.SetAmbientLight(s.ambient);
	gss.SetCamera(s.camera);

	s.UpdateDebugMaterials();

	// Position the material-preview sphere (in front of the camera, or fixed).
	if (s.showDebugBall && s.debugBallValid)
	{
		const Matrix4x4f camXf = s.camera.GetTransform();
		const Vector3f fwd = camXf.GetForward();
		const Vector3f pos = s.debugBallFollowCam
			? camXf.GetPosition() + fwd * (s.debugBallRadius * 5.f)
			: s.debugBallPos;
		const float sc = s.debugBallRadius / s.debugBallModelRadius;
		Matrix4x4f xf = Matrix4x4f::CreateIdentityMatrix();
		xf(1, 1) = xf(2, 2) = xf(3, 3) = sc;
		xf.SetPosition(pos);
		s.debugBall.SetTransform(xf);
	}

	// Position the orbiting material-preview spheres on a ring around the scene.
	if (s.showOrbitBalls && s.debugBallValid && !s.orbitBalls.empty())
	{
		const int n = std::clamp(s.orbitBallCount, 1, Impl::kMaxOrbitBalls);
		const float sc = s.orbitBallRadius / s.debugBallModelRadius;
		const Vector3f ctr = s.sceneCenter + Vector3f{ 0.f, s.orbitHeight, 0.f };
		for (int i = 0; i < n; ++i)
		{
			const float a = s.orbitAngle + (6.28318530718f * i) / n;
			const Vector3f p = ctr + Vector3f{ std::cos(a) * s.orbitPathRadius,
			                                   0.f,
			                                   std::sin(a) * s.orbitPathRadius };
			Matrix4x4f xf = Matrix4x4f::CreateIdentityMatrix();
			xf(1, 1) = xf(2, 2) = xf(3, 3) = sc;
			xf.SetPosition(p);
			s.orbitBalls[i].SetTransform(xf);
		}
	}
	// Forward / transparent path: still capped at the engine's cbuffer size.
	for (int i = 0; i < (int)s.pointLights.size() && i < NUMBER_OF_LIGHTS_ALLOWED; ++i)
		gss.AddPointLight(s.pointLights[i]);

	// DXR uses DeferredRenderer as its resource/presentation host too.  Force it
	// on before this setup branch so an old forward-renderer setting can never
	// prevent the DXR mode request from reaching BuildFrame.
	if (s.dxrRenderer && s.deferred) s.useDeferred = true;

	// Deferred path: all lights via the structured buffer (no cap) + froxel cull.
	if (s.useDeferred && s.deferred)
	{
		std::vector<DeferredLight> gl;
		gl.reserve(s.pointLights.size());
		for (size_t i = 0; i < s.pointLights.size(); ++i)
		{
			const PointLight& p = s.pointLights[i];
			const Impl::LightExtra ex = i < s.lightExtra.size() ? s.lightExtra[i] : Impl::LightExtra{};
			gl.push_back(DeferredLight{
				{ p.position.x, p.position.y, p.position.z }, p.range,
				{ p.color.r, p.color.g, p.color.b }, p.radius,
				{ ex.spotDir.x, ex.spotDir.y, ex.spotDir.z }, ex.spotCosOuter,
				ex.spotCosInner, -1.0f, { 0.f, 0.f } });
		}

		// Emissive light proxy: a lit debug sphere with emission becomes an actual
		// area light so it illuminates the room (Lumen-style emissive lighting, the
		// cheap way -- real inverse-square falloff + participates in shadows).
		// Driven by "Emissive strength" alone; a black emissive colour falls back to
		// white so raising the strength slider always does something.
		{
			const MaterialAsset& dm = s.debugMat;
			const float emStr = dm.emissiveStrength;
			float ec[3] = { dm.emissiveColor[0], dm.emissiveColor[1], dm.emissiveColor[2] };
			if (std::max({ ec[0], ec[1], ec[2] }) < 0.001f) { ec[0] = ec[1] = ec[2] = 1.f; }

			// The engine's point/area falloff is 1/distance_metres^2 (world units are
			// treated as cm). At room scale that makes a naive proxy vanish, so scale
			// the intensity by (0.01 * refDist)^2 -- refDist ~ sphere-to-far-wall --
			// which cancels the falloff at that distance and keeps the lit result
			// stable whatever the room size. `gain` then reads as emissive efficiency.
			// refDist ~ the sphere's typical distance to the surface it lights
			// (spheres orbit near half-extent; walls at full extent -> ~half-extent).
			const float refDist = std::max({ s.sceneExtents.x, s.sceneExtents.z, 200.f }) * 0.5f;
			const float distScale = (0.01f * refDist) * (0.01f * refDist);
			const float kRaw = s.debugEmissiveLightGain * distScale * emStr;
			const float k = std::min(kRaw, 60.f);   // guard against blow-out on huge rooms / strengths
			const float proxyRange = refDist * 5.0f;

			if (emStr > 0.01f && s.showDebugBall && s.debugBallValid && s.debugBallEmitsLight
				&& (int)gl.size() < DeferredRenderer::kMaxLights)
			{
				const Vector3f bp = s.debugBall.GetTransform().GetPosition();
				gl.push_back(DeferredLight{
					{ bp.x, bp.y, bp.z }, std::max(proxyRange, s.debugBallRadius * 14.f),
					{ ec[0] * k, ec[1] * k, ec[2] * k },
					s.debugBallRadius, { 0,-1,0 }, -1.f, -1.f, -1.f,
					{ s.debugEmissiveCastShadow ? 0.f : 1.f, 0.f } });   // _pad[0] = "no shadow"
			}

			// Orbiting spheres get the same proxy (never shadow-casting -- a moving
			// cube-shadow slot per ball would be brutal).
			if (emStr > 0.01f && s.showOrbitBalls && s.debugBallValid && s.orbitBallsEmitLight)
			{
				const int n = std::clamp(s.orbitBallCount, 1, Impl::kMaxOrbitBalls);
				for (int i = 0; i < n && (int)gl.size() < DeferredRenderer::kMaxLights; ++i)
				{
					const Vector3f bp = s.orbitBalls[i].GetTransform().GetPosition();
					gl.push_back(DeferredLight{
						{ bp.x, bp.y, bp.z }, std::max(proxyRange, s.orbitBallRadius * 14.f),
						{ ec[0] * k, ec[1] * k, ec[2] * k },
						s.orbitBallRadius, { 0,-1,0 }, -1.f, -1.f, -1.f,
						{ 1.f, 0.f } });   // _pad[0] = "no shadow"
				}
			}
		}

		s.deferred->UploadLights(gl.data(), (int)gl.size());
		s.deferred->SetClustered(s.wantClustered);
		s.deferred->SetSSAO(s.wantSSAO);
		s.deferred->SetShadows(s.wantShadows);
		s.deferred->SetLocalShadows(s.wantLocalShadows);
		s.deferred->SetSSR(s.wantSSR);
		s.deferred->SetPostFx(s.wantPostFx);
		s.deferred->SetDxrRenderer(s.dxrRenderer);
		s.deferred->SetShadowLight(s.dirLight.transform.GetForward(), s.sceneCenter, s.sceneExtents.Length());
		s.deferred->SetCamera(s.camera);
		if (s.frame == s.bench.taaResetFrame) s.deferred->ResetTemporalHistory();
		// Box-parallax the IBL against the probe influence box when a probe is live.
		const bool boxOn = s.probeEnabled && s.probePrefilter && s.probePrefiltered.resource != nullptr;
		s.deferred->SetReflectionProbeBox(s.probePos, s.probeBox, boxOn);
		s.deferred->SetGiVolume(s.giOrigin, s.giSpacing, s.giCx, s.giCy, s.giCz,
			s.giIntensity, s.giEnabled && s.deferred->HasGi(),
			(s.giEnabled && !sealedRoom) ? s.autoSeal : 0.f);
		// Full DXR must use the authored world environment, never the dynamic
		// reflection-probe capture held in ambient.cubemap. That capture is
		// intentionally low-resolution and prefiltered for local raster
		// reflections; using it as a primary-ray sky creates the huge blurry
		// blue blobs visible in the DXR renderer and corrupts its IBL energy.
		// A sealed room deliberately receives no exterior sky bounce.
		const Color& ambientTint = s.ambient.color;
		const rhi::SrvHandle dxrEnvironment = s.worldEnvironmentPrefiltered.IsValid()
			? s.worldEnvironmentPrefiltered.GetSrv()
			: ((!sealedRoom && s.fallbackCube) ? s.fallbackCube->GetSrv() : rhi::SrvHandle{});
		s.deferred->SetGiEnvironment(
			!sealedRoom ? dxrEnvironment : rhi::SrvHandle{},
			{ ambientTint.r, ambientTint.g, ambientTint.b }, !sealedRoom);
	}

	gss.Push();
	gss.SetBlendState(BlendState::Disabled);
	gss.SetAlphaTestThreshold(0.33f);   // so masked decals cut out (opaque albedo is alpha=1)
	if (s.bench.noCullFace)
		gss.SetRasterizerState(RasterizerState::NoFaceCulling);   // room planes are viewed from the inside

	const Frustum frustum = CalculateFrustum(s.camera);
	ModelDrawer& md = ge.GetModelDrawer();
	// The "more than one model" gate dated from scenes made of many small
	// instances, where whole-model bounds were the only thing worth testing.
	// A scene gathered into one FBX is a single instance spanning everything, so
	// that gate turned culling off entirely; the per-sub-mesh paths below are
	// what actually reject geometry now.
	md.SetCullFrustum(s.frustumCull ? &frustum : nullptr);

	// Phase-1 TLAS validation: include every static scene mesh, not only the
	// raster-visible subset. RayQuery must see off-screen occluders as well.
	// Matrix4x4f uses row vectors, while D3D12's 3x4 instance transform is the
	// equivalent column-vector form, hence the explicit transpose below.
	if (rhi::IDevice* dxr = DX11::Rhi(); dxr && dxr->SupportsRaytracingTier11())
	{
		std::vector<rhi::RaytracingInstanceDesc> rayInstances;
		std::map<const ModelInstance*, Matrix4x4f> nextRayTransforms;
		bool raySceneStationary = true;
		uint32_t instanceId = 0;
		auto addInstance = [&](const ModelInstance& instance, uint32_t materialOverride = 0u)
		{
			const std::shared_ptr<Model> model = instance.GetModel();
			if (!model) return;
			const Matrix4x4f& m = instance.GetTransform();
			const auto previous = s.previousRayTransforms.find(&instance);
			const bool historyValid = previous != s.previousRayTransforms.end();
			const Matrix4x4f& previousM = historyValid ? previous->second : m;
			raySceneStationary = raySceneStationary && historyValid && previousM == m;
			nextRayTransforms.emplace(&instance, m);
			size_t meshIndex = 0;
			for (const Model::MeshData& mesh : model->GetMeshDataList())
			{
				const size_t textureMeshIndex = meshIndex++;
				if (!mesh.rayGeometry.blas.IsValid()) continue;
				rhi::RaytracingInstanceDesc d = {};
				 d.blas = mesh.rayGeometry.blas; d.instanceId = instanceId++;
				d.vertexSrv = dxr->RegisterRaySceneSrv(mesh.rayGeometry.vertexRawSrv);
				d.indexSrv = dxr->RegisterRaySceneSrv(mesh.rayGeometry.indexRawSrv);
				d.materialIndex = materialOverride != 0u ? materialOverride : mesh.rayGeometry.materialIndex;
				// Per-instance .tgmat material (ApplySceneMaterial), same as raster.
				if (materialOverride == 0u && textureMeshIndex < MAX_MESHES_PER_MODEL)
					if (const uint32_t instanceMaterial = instance.GetMaterialOverride(textureMeshIndex))
						d.materialIndex = instanceMaterial;
				// After every branch that can still change materialIndex above
				// (the TGO per-instance texture override rewrites it), so this
				// classifies the record the shader will actually decode.
				d.rayOpaque = RayTracingMaterialTable::IsRayOpaque(d.materialIndex);
				d.vertexStride = mesh.rayGeometry.vertexStride;
				d.positionOffset = mesh.rayGeometry.positionOffset;
				d.normalOffset = mesh.rayGeometry.normalOffset;
				d.uv0Offset = mesh.rayGeometry.uv0Offset;
				d.tangentOffset = mesh.rayGeometry.tangentOffset;
				d.binormalOffset = mesh.rayGeometry.binormalOffset;
				d.vertexFormat = (uint32_t)mesh.rayGeometry.vertexFormat;
				d.indexCount = mesh.numberOfIndices;
				for (uint32_t row = 0; row < 3; ++row)
					for (uint32_t col = 0; col < 4; ++col)
					{
						d.transform[row * 4 + col] = m(col + 1, row + 1);
						d.previousTransform[row * 4 + col] = previousM(col + 1, row + 1);
					}
				d.motionHistoryValid = historyValid ? 1u : 0u;
				rayInstances.push_back(d);
			}
		};

			const uint32_t pillarMaterial = s.IsPillarTest() && s.pillarMaterialOverride
				? s.pillarMaterialIndex : 0u;
			for (const ModelInstance& instance : s.models) addInstance(instance, pillarMaterial);

		const uint32_t debugMaterial = s.debugBallValid && (s.showDebugBall || s.showOrbitBalls)
			? s.debugMaterialIndex : 0u;

		// The orbiting/debug spheres are drawn separately from s.models (see
		// their own .Render() calls further down) and were never fed into the
		// TLAS -- they simply didn't exist for any ray to hit. Same visibility
		// gating as the raster path so DXR sees exactly what raster would draw.
		if (s.showOrbitBalls && s.debugBallValid)
		{
			// Same active-count bound the raster draw loop uses further down
			// (orbitBalls is a fixed-size pool; not all slots are "live").
			const int n = std::clamp(s.orbitBallCount, 1, Impl::kMaxOrbitBalls);
			for (int i = 0; i < n && i < (int)s.orbitBalls.size(); ++i) addInstance(s.orbitBalls[i], debugMaterial);
		}
		if (s.showDebugBall && s.debugBallValid)
			addInstance(s.debugBall, debugMaterial);

		{
			TGA_PROFILE_SCOPE(&s.gpu, "TLAS build");
			dxr->BuildRaytracingTlas(rayInstances.data(), (uint32_t)rayInstances.size());
		}
		if (s.deferred) s.deferred->SetRaySceneStationary(raySceneStationary && nextRayTransforms.size() == s.previousRayTransforms.size());
		s.previousRayTransforms = std::move(nextRayTransforms);
	}

	// The ray-traced probe scheduler deferred capture until the current TLAS
	// exists; otherwise RayQuery would consume an old frame slot or no scene.
	if (s.giRtCapturePending)
	{
		s.giRtCapturePending = false;
		TGA_PROFILE_SCOPE(&s.gpu, "GI probe update");
		s.CaptureGiProbesImpl(ge);
	}

	{
		RenderGraph rg(ge.GetRenderResourcePool(), &s.gpu);

		if (s.useDeferred && s.deferred)
		{
			DeferredRenderer* dr = s.deferred;
			ModelDrawer* mdp = &md;
			Impl* sp = &s;
			const Frustum* fr = s.frustumCull ? &frustum : nullptr;

			auto drawOpaque = [dr, mdp, sp, fr]()
			{
				mdp->SetCullFrustum(fr);   // the shadow pass clears it
				const ModelShader& gsh = dr->GetGeometryShader();
				if (sp->IsPillarTest() && sp->pillarMaterialOverride)
				{
					ModelShader::SetMaterialOverride(sp->pillarMaterialIndex);
					for (const ModelInstance& instance : sp->models) instance.Render(gsh);
					ModelShader::SetMaterialOverride(0);
				}
				else
				for (size_t k = 0; k < sp->models.size(); ++k)
				{
					if (!sp->anyTransparent || sp->transparentMeshes[k].empty())
						mdp->Draw(sp->models[k], gsh);              // whole model (keeps frustum cull)
					else if (fr)
						sp->models[k].Render(gsh, sp->opaqueMeshes[k], *fr);
					else
						sp->models[k].Render(gsh, sp->opaqueMeshes[k]);
				}
				// Preview spheres carry their own material (UpdateDebugMaterials);
				// glass ones are drawn by the transparent pass instead.
				if (sp->debugBallValid && !sp->DebugBallIsGlass())
					sp->DrawDebugBalls(gsh);
			};

			std::function<void()> drawTransparent;
			if (s.anyTransparent || (s.debugBallValid && s.DebugBallIsGlass()))
			{
				drawTransparent = [dr, mdp, sp]()
				{
					// Deferred transparency is classified as glass during import.  Use
					// the HDR/depth-aware forward shader when it compiled; retain the
					// original PBR path as a safe shader-load fallback.
					const ModelShader& psh = dr->HasGlassShader() ? dr->GetGlassShader() : mdp->GetPbrShader();
					if (sp->anyTransparent)
						for (size_t k = 0; k < sp->models.size(); ++k)
							sp->models[k].Render(psh, sp->transparentMeshes[k]);
					if (sp->debugBallValid && sp->DebugBallIsGlass())
						sp->DrawDebugBalls(psh);
				};
			}

			// Shadow casters. The deferred renderer sets a per-cascade / per-tile
			// cull frustum before calling this; the whole-model path honours it,
			// so distant instances are skipped per shadow view.
			auto drawShadowCasters = [dr, sp](const Camera& shadowCam)
			{
				const Frustum lf = CalculateFrustum(shadowCam);
				const ModelShader& ssh = dr->GetShadowShader();
				for (size_t k = 0; k < sp->models.size(); ++k)
				{
					if (!sp->anyTransparent || sp->transparentMeshes[k].empty())
						sp->models[k].Render(ssh, lf);           // sub-meshes culled to this shadow view
					else
						sp->models[k].Render(ssh, sp->opaqueMeshes[k], lf);
				}
			};

			dr->BuildFrame(rg, drawOpaque, drawTransparent, drawShadowCasters, s.gbufChannel);
		}
		else
		{
			ModelDrawer* mdp = &md;
			auto* models = &s.models;
			rg.AddPass("forward", [mdp, models](RenderGraph&)
			{
				DX11::BackBuffer->SetAsActiveTarget(DX11::DepthBuffer);
				for (ModelInstance& m : *models)
					mdp->DrawPbr(m);
			});
		}

		rg.Execute();
	}

	s.gpu.EndFrame();
	s.StepCostSweep();

	if (s.frame > s.warmupFrames && (s.benchFrames == 0 || s.frame <= s.benchFrames))
	{
		s.visibleInstances.push_back((double)(s.models.size() - md.GetLastCulledCount()));
		if (s.gpu.IsReady())
		{
			s.gpuFrameMs.push_back(s.gpu.GetFrameGpuMs());
			for (const auto& r : s.gpu.GetResults())
				s.gpuScopeMs[r.name].push_back(r.ms);
		}
	}

	md.SetCullFrustum(nullptr);

	gss.Pop();
	DX11::BackBuffer->SetAsActiveTarget();

	// DX11 only; the DX12 capture happens earlier in Render().
	const bool dx11Backend = !DX11::Rhi() || DX11::Rhi()->GetBackend() != rhi::Backend::DX12;
	if (dx11Backend && !s.screenshotPath.empty() && !s.screenshotTaken)
	{
		// BENCH_SHOT_FRAME pins the capture to a chosen frame instead of the end
		// of the run. The scripted camera is parameterised by fraction-of-run and
		// the orbit completes two full loops, so the default (benchFrames - 2)
		// always lands back at the starting angle -- which for Sponza is inside a
		// wall. Any other viewpoint needs an explicit frame.
		const int shotFrame = s.shotFrame > 0 ? s.shotFrame : (s.benchFrames > 3 ? s.benchFrames - 2 : 120);
		if (s.frame >= shotFrame)
		{
			s.screenshotTaken = true;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
			if (DX11::SwapChain && SUCCEEDED(DX11::SwapChain->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()))))
			{
				const std::wstring wpath(s.screenshotPath.begin(), s.screenshotPath.end());
				HRESULT hr = DirectX::SaveWICTextureToFile(DX11::Context, back.Get(),
					GUID_ContainerFormatPng, wpath.c_str(), nullptr, nullptr, true);
				INFO_PRINT("Sponza bench: screenshot -> %s (hr=0x%08X)", s.screenshotPath.c_str(), (unsigned)hr);
			}
		}
	}

	if (myImpl->frame > 0)
	{
		const auto now = std::chrono::high_resolution_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(now - s.cpuStart).count();
		if (s.frame > s.warmupFrames && (s.benchFrames == 0 || s.frame <= s.benchFrames))
			s.cpuMs.push_back(ms);
	}
}
