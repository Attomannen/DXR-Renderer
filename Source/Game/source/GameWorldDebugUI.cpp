#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: the ImGui debug / tuning panel.

void GameWorld::DrawDebugUI()
{
#ifndef _RETAIL
	Impl& s = *myImpl;
	if (!s.debugUiOpen) return;
	const bool uiCapture = s.benchFrames > 0 && s.bench.debugUi;
	if (uiCapture) { ImGui::GetIO().IniFilename = nullptr; ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always); }
	ImGui::SetNextWindowSize(ImVec2(540, 740), uiCapture ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(440, 380), ImVec2(900, 1200));
	ImGui::SetNextWindowPos(ImVec2(std::max(12.f, ImGui::GetIO().DisplaySize.x - 554.f), 12), uiCapture ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14,12));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8,5));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,7));
	if (ImGui::Begin("Debug###RenderSettings", &s.debugUiOpen))
	{
		ImGui::PushItemWidth(-175);
		if (ImGui::BeginTabBar("DebugTabs"))
		{
		if (ImGui::BeginTabItem("Scene"))
		{
		// --- scene picker: every *.tgs under the game data root, recursively ---
		// Scenes live in subfolders (e.g. data/Scenes/*.tgs), matching
		// FindFirstTgsScene()'s scan used at startup -- LoadSceneContent()
		// resolves names the same way (root / name + ".tgs"), so the listed
		// name must keep its subfolder-relative path, not just the stem.
		{
			static std::vector<std::string> sceneList;
			static bool scanned = false;
			if (!scanned)
			{
				scanned = true;
				std::error_code ec;
				const std::filesystem::path root = Tga::Settings::GameAssetRoot();
				for (const auto& de : std::filesystem::recursive_directory_iterator(root, ec))
				if (de.is_regular_file() && de.path().extension() == ".tgs")
				sceneList.push_back(std::filesystem::relative(de.path(), root, ec).replace_extension().generic_string());
				std::sort(sceneList.begin(), sceneList.end());
			}
			int cur = 0;
			for (int i = 0; i < (int)sceneList.size(); ++i)
			if (sceneList[i] == s.currentScene) cur = i;
			std::vector<const char*> items;
			for (auto& n : sceneList) items.push_back(n.c_str());
			if (!items.empty() && ImGui::Combo("Scene", &cur, items.data(), (int)items.size())
			&& sceneList[cur] != s.currentScene)
			{
				s.LoadSceneContent(sceneList[cur], false);
				s.frame = 0;
			}
		}
		ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Render Tuning"))
		{
		DeferredRenderer::Tunables* tun = s.deferred ? &s.deferred->GetTunables() : nullptr;
		const bool available = s.deferred && DX11::Rhi()->SupportsRaytracingTier11();
		ImGui::Text("%.1f FPS  |  %.2f ms", ImGui::GetIO().Framerate, 1000.f / std::max(1.f, ImGui::GetIO().Framerate));
		ImGui::SameLine();
		ImGui::TextColored(s.dxrRenderer ? ImVec4(.35f,1.f,.55f,1.f) : ImVec4(.65f,.8f,1.f,1.f), s.dxrRenderer ? "DXR" : "Raster");

		ImGui::BeginDisabled(!available);
		if (ImGui::Checkbox("DXR rendering", &s.dxrRenderer)) {
			s.deferred->SetDxrRenderer(s.dxrRenderer);
			if (s.dxrRenderer) { s.useDeferred=true; s.gbufChannel=0; s.giUseRT=true; s.StartGiPrime(); }
		}
		ImGui::EndDisabled();
		if (!available) ImGui::TextDisabled("DXR requires a DX12 device with Tier 1.1 support.");
		if (s.dxrRenderer && s.deferred && !s.deferred->IsDxrRenderer())
		ImGui::TextColored(ImVec4(1,.4f,.3f,1), "DXR shader is unavailable; check the log.");
		ImGui::Separator();
		const bool sealed = s.sealScene;
		auto beginTab = [&](const char* label) {
			const bool select = s.benchFrames > 0 && s.bench.debugTab == label;
			if (!ImGui::BeginTabItem(label, nullptr, select ? ImGuiTabItemFlags_SetSelected : 0)) return false;
			ImGui::BeginChild(label, ImVec2(0,0), false);
			return true;
		};
		auto endTab = [&]() { ImGui::EndChild(); ImGui::EndTabItem(); };
		if (ImGui::BeginTabBar("RenderSettingsTabs")) {
			if (beginTab("Render")) {
				if (s.dxrRenderer && available && tun) {
					ImGui::SeparatorText("Ray-traced direct lighting");
					ImGui::Checkbox("Direct lighting + ray shadows", &tun->dxrDirectLighting);
					ImGui::Checkbox("Environment diffuse + specular", &tun->dxrEnvironmentLighting);
					ImGui::TextDisabled("Edit sun and environment on the Lighting tab.");
					ImGui::SliderFloat("Ambient floor", &tun->dxrAmbientIntensity, 0.f, 1.f, "%.3f");
					ImGui::SeparatorText("Reflections and occlusion");
					ImGui::Checkbox("Ray-traced reflections", &tun->dxrReflections);
					ImGui::BeginDisabled(!tun->dxrReflections);
					ImGui::SliderFloat("Roughness cutoff", &tun->dxrReflectionRoughnessCutoff, 0.05f, 1.f, "%.2f");
					ImGui::SliderInt("Reflection rays / pixel", &tun->dxrReflectionSamples, 1, 4);
					ImGui::SetItemTooltip("0 = unbounded. A reflection ray that hits nothing traverses the entire BVH before falling back to the environment cube, so bounding this is a real win at grazing angles where rays skim far across the scene.");
					ImGui::EndDisabled();
					ImGui::Checkbox("Ray-traced ambient occlusion", &tun->dxrAmbientOcclusion);
					ImGui::BeginDisabled(!tun->dxrAmbientOcclusion);
					ImGui::SliderFloat("AO distance", &tun->dxrAoDistance, 5.f, 400.f, "%.0f wu");
					ImGui::SliderFloat("AO strength", &tun->dxrAoStrength, 0.f, 1.f, "%.2f");
					ImGui::SliderInt("AO rays / pixel", &tun->dxrAoSamples, 1, 8);
					ImGui::SetItemTooltip("Measured as the most expensive single term in the DXR frame. "
						"The temporal resolve converges low counts; raise only if AO looks noisy when still.");
					ImGui::EndDisabled();
					if (ImGui::Checkbox("Ray texture filtering", &tun->dxrTextureFiltering)) s.StartGiPrime();
					ImGui::SeparatorText("Resolution and ray budgets");
					ImGui::BeginDisabled(!tun->nrdEnabled);
					ImGui::Checkbox("Half-resolution AO + reflections (NRD checkerboard)", &tun->nrdCheckerboard);
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("Each pixel traces either AO or reflection rays, alternating every frame; NRD reconstructs full resolution. Requires the NRD denoiser.");
					ImGui::SliderInt("Sun shadow rays / pixel", &tun->dxrSunShadowSamples, 1, 4);
					ImGui::SetItemTooltip("Below 4 the sample pattern rotates per frame and the temporal resolve smooths the penumbra.");
					const char* fogResolutions[] = { "Full", "Half", "Quarter", "Eighth" };
					ImGui::Combo("Fog volume resolution", &tun->volumetricResolution, fogResolutions, IM_ARRAYSIZE(fogResolutions));
					ImGui::SliderInt("GI probes / frame", &s.giProbeBudget, 0, 256, s.giProbeBudget == 0 ? "auto" : "%d");
					ImGui::SetItemTooltip("RT GI probes re-traced per frame while the volume updates. Auto = 32768 rays per frame.");
					ImGui::TextDisabled("Overall ray-trace resolution: DLSS mode below (Quality = 67%%, Performance = 50%%).");
					ImGui::SeparatorText("Image stability");
					ImGui::Checkbox("Temporal anti-aliasing", &tun->taaEnabled);
					ImGui::BeginDisabled(!tun->taaEnabled);
					ImGui::Checkbox("Sub-pixel jitter", &tun->taaJitter);
					ImGui::SetItemTooltip("Halton sample offset. Off reproduces the old fixed grid: temporal accumulation still runs, but no geometric detail is recovered and DLSS quality suffers.");
					ImGui::EndDisabled();
					const char* dlssModes[] = { "Native temporal", "DLAA", "DLSS Quality", "DLSS Balanced", "DLSS Performance", "DLSS Ultra Performance" };
					if (ImGui::Combo("NVIDIA DLSS mode (RTX)", &tun->dlssMode, dlssModes, IM_ARRAYSIZE(dlssModes)))
					{
						tun->dlaaEnabled = tun->dlssMode == 1;
						if (tun->dlssMode >= 2) tun->rayReconstructionEnabled = false;
						s.deferred->RecreateDxrTargets();
						s.deferred->ResetTemporalHistory();
						s.StartGiPrime();
					}
					if (ImGui::Checkbox("NVIDIA NRD denoiser (diffuse + specular)", &tun->nrdEnabled))
					{
						if (tun->nrdEnabled) tun->rayReconstructionEnabled = false;
						s.deferred->ResetTemporalHistory();
					}
					ImGui::TextDisabled("Denoises ray-traced indirect diffuse and reflections before TAA or DLSS.");
					if (tun->nrdEnabled)
					{
						ImGui::Indent();
						const char* denoisers[] = { "REBLUR (low-sample input)", "RELAX (clean input)" };
						if (ImGui::Combo("Denoiser", &tun->nrdDenoiser, denoisers, 2)) s.deferred->ResetTemporalHistory();
						ImGui::SliderFloat("History length", &tun->nrdHistorySeconds, 0.05f, 1.0f, "%.2f s");
						ImGui::SetItemTooltip("Shorter = less smearing on moving objects and lights, more residual noise.");
						ImGui::SliderInt("Fast history (frames)", &tun->nrdFastHistoryFrames, 1, 16);
						ImGui::SetItemTooltip("The responsive history that clamps the long one. Lower reacts faster.");
						ImGui::Checkbox("Anti-lag", &tun->nrdAntilag);
						ImGui::Checkbox("Validation overlay", &tun->nrdValidation);
						ImGui::SetItemTooltip("NRD's debug view: checks motion vectors, depth, normals and history length. Best viewed with Tonemapper = None.");
						ImGui::Unindent();
					}
					ImGui::TextDisabled("DLSS SR modes render DXR at lower resolution and reconstruct HDR at display resolution.");
					if (tun->dlssMode > 0 || tun->dlaaEnabled)
					{
						const char* presets[] = { "Default", "J", "K", "L", "M" };
						ImGui::Combo("DLSS model preset", &tun->dlssPreset, presets, IM_ARRAYSIZE(presets));
						ImGui::SetItemTooltip("L measured slightly steadier than the default on a still camera.");
						ImGui::TextDisabled("DLSS keeps a slight sub-pixel wobble with jitter; native TAA is steadier.");
					}
					if ((tun->dlssMode > 0 || tun->dlaaEnabled) && !tun->nrdEnabled && !tun->rayReconstructionEnabled)
						ImGui::TextColored(ImVec4(1, .6f, .3f, 1), "DLSS keeps ray noise as detail: enable NRD or Ray Reconstruction.");
					if (ImGui::Checkbox("DLSS Ray Reconstruction denoiser (RTX)", &tun->rayReconstructionEnabled))
					{
						if (tun->rayReconstructionEnabled)
						{
							tun->nrdEnabled = false;
							tun->dlaaEnabled = true;
							tun->dlssMode = 1;
						}
						s.deferred->RecreateDxrTargets();
						s.deferred->ResetTemporalHistory();
						s.StartGiPrime();
					}
					ImGui::TextDisabled("Uses ray-traced radiance plus albedo, specular albedo, normal/roughness, depth and motion guides.");
					bool specularChanged = ImGui::Checkbox("Specular anti-aliasing", &tun->specularAaEnabled);
					ImGui::BeginDisabled(!tun->specularAaEnabled);
					specularChanged |= ImGui::SliderFloat("Specular AA strength", &tun->specularAaStrength, 0.0f, 1.0f, "%.2f");
					ImGui::EndDisabled();
					if (specularChanged) { s.deferred->ResetTemporalHistory(); s.StartGiPrime(); }
					ImGui::BeginDisabled(!tun->taaEnabled);
					ImGui::SliderFloat("TAA history weight", &tun->taaHistoryWeight, 0.0f, 0.95f, "%.2f");
					ImGui::SliderFloat("TAA stationary weight", &tun->taaStationaryWeight, 0.0f, 0.98f, "%.2f");

					ImGui::EndDisabled();

				} else if (!s.dxrRenderer) {
					ImGui::Checkbox("Deferred rendering", &s.useDeferred);
					ImGui::BeginDisabled(s.dxrRenderer);
					if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Checkbox("Directional CSM", &s.wantShadows);
						if (tun)
						{
							ImGui::SliderFloat("Normal offset", &tun->shadowNormalOffset, 0.f, 8.f, "%.1f tx");
							ImGui::SliderFloat("Depth bias", &tun->shadowDepthBias, 0.f, 20.f, "%.1f wu");
							ImGui::SliderFloat("Strength", &tun->shadowStrength, 0.f, 1.f);
							ImGui::Checkbox("Contact shadows", &tun->contactShadows);
							if (tun->contactShadows)
							{
								ImGui::SliderFloat("Contact length", &tun->contactLength, 2.f, 150.f, "%.0f wu");
								ImGui::SliderFloat("Contact thickness", &tun->contactThickness, 2.f, 100.f, "%.0f wu");
								ImGui::Checkbox("Show contact term", &tun->contactViz);
							}
							ImGui::Checkbox("Show cascades", &tun->shadowShowCascades);
							ImGui::Separator();
							ImGui::Checkbox("Point/spot shadows", &s.wantLocalShadows);
							if (s.wantLocalShadows)
							{
								ImGui::SliderInt("Max casters", &tun->localShadowMaxCasters, 0, 8);
								ImGui::SliderInt("Max point casters", &tun->localShadowMaxPoints, 0, tun->localShadowMaxCasters);
							}
						}
					}
					ImGui::EndDisabled();
					ImGui::BeginDisabled(s.dxrRenderer);
					if (ImGui::CollapsingHeader("SSR", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Checkbox("Enabled##ssr", &s.wantSSR);
						if (tun)
						{
							ImGui::BeginDisabled(!s.wantSSR);
							ImGui::SliderFloat("Max distance", &tun->ssrMaxDistance, 50.f, 4000.f, "%.0f");
							ImGui::SliderFloat("Thickness", &tun->ssrThickness, 2.f, 120.f, "%.0f");
							ImGui::SliderFloat("Roughness cutoff", &tun->ssrRoughnessCutoff, 0.05f, 1.f, "%.2f");
							ImGui::SliderFloat("Strength##ssr", &tun->ssrStrength, 0.f, 2.f, "%.2f");
							ImGui::SliderInt("March steps", &tun->ssrSteps, 8, 128);
							ImGui::SliderInt("Refine steps", &tun->ssrRefineSteps, 0, 8);
							ImGui::EndDisabled();
						}
					}
					ImGui::EndDisabled();
					ImGui::BeginDisabled(s.dxrRenderer);
					if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Checkbox("Enabled##ssao", &s.wantSSAO);
						if (tun)
						{
							ImGui::BeginDisabled(!s.wantSSAO);
							ImGui::SliderFloat("AO radius", &tun->ssaoRadius, 4.f, 200.f);
							ImGui::SliderFloat("AO bias", &tun->ssaoBias, 0.f, 4.f);
							ImGui::SliderFloat("AO intensity", &tun->ssaoIntensity, 0.f, 4.f);
							ImGui::SliderFloat("AO power", &tun->ssaoPower, 0.5f, 4.f);
							ImGui::EndDisabled();
						}
					}
					ImGui::EndDisabled();

				}
				endTab();
			}
			if (beginTab("Lighting")) {
				if (ImGui::CollapsingHeader("Scene lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (ImGui::Checkbox("Seal scene (no sun or sky)", &s.sealScene)) { s.StartGiPrime(); if(s.deferred) s.deferred->ResetTemporalHistory(); }
					ImGui::BeginDisabled(!s.giEnabled || sealed);
					ImGui::SliderFloat("Interior sky occlusion", &s.autoSeal,0.f,1.f,"%.2f");
					ImGui::EndDisabled();
				}
				if (ImGui::CollapsingHeader("Sun / directional", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::BeginDisabled(sealed);
					ImGui::SliderFloat("Pitch", &s.sunPitch, -89.f, 89.f, "%.1f deg");
					ImGui::SliderFloat("Yaw",   &s.sunYaw, -180.f, 180.f, "%.1f deg");
					ImGui::Checkbox("Colour from temperature", &s.sunUseTemperature);
					if (s.sunUseTemperature)
						ImGui::SliderFloat("Temperature", &s.sunTemperatureK, 1700.f, 12000.f, "%.0f K");
					else
						ImGui::ColorEdit3("Colour", s.sunColor);
					ImGui::SliderFloat("Illuminance", &s.sunIlluminanceLux, 0.f, 150000.f, "%.0f lux", ImGuiSliderFlags_Logarithmic);
					ImGui::TextDisabled("Clear noon ~100k lux, overcast ~10k, sunset ~400.");
					if (!s.dxrRenderer) ImGui::SliderFloat("Softness", &s.sunSoftness, 0.f, 1.f);
					ImGui::EndDisabled();
					if (sealed) ImGui::TextDisabled("Sun is disabled by scene sealing.");
				}
				if (ImGui::CollapsingHeader("Ambient / IBL"))
				{
					ImGui::BeginDisabled(sealed);
					ImGui::ColorEdit3("Ambient", s.ambientColor);
					if (s.deferred)
					{
						auto* skyTun = &s.deferred->GetTunables();
						bool physicalSky = skyTun->skyLuminanceNits > 0.f;
						if (ImGui::Checkbox("Physical sky", &physicalSky))
							skyTun->skyLuminanceNits = physicalSky ? 8000.f : 0.f;
						if (physicalSky)
						{
							ImGui::SliderFloat("Sky luminance", &skyTun->skyLuminanceNits, 0.001f, 30000.f, "%.3g cd/m2", ImGuiSliderFlags_Logarithmic);
							ImGui::TextDisabled("Clear day ~8000, overcast ~2000, dusk ~10, moonlit ~0.01.");
							const float measured = s.deferred->GetEnvironmentAverageLuminance();
							if (measured > 0.f) ImGui::TextDisabled("Environment map as authored: %.3g cd/m2", measured * Photometry::kNitsPerUnit);
							else ImGui::TextDisabled("Measuring environment map...");
						}
					}
					ImGui::SliderFloat("IBL scale", &s.ambientScale, 0.f, 4.f, "%.2f");
					static const char* kCubes[] = { "horizonCubeMap", "env_studio", "env_powerplant", "env_slipway" };
					if (ImGui::Combo("Cubemap", &s.cubemapIdx, kCubes, IM_ARRAYSIZE(kCubes)))
					{
						s.fallbackCube = GraphicsEngine::GetInstance()->GetTextureManager()
							.GetTexture((std::string("Textures/") + kCubes[s.cubemapIdx] + ".dds").c_str(),
							TextureSrgbMode::None);
						s.RebuildWorldEnvironmentPrefilter();
					}
					ImGui::EndDisabled();
					if (!s.dxrRenderer)
					{
						ImGui::Checkbox("Reflection probe", &s.probeEnabled);
						ImGui::SliderInt("Probe interval", &s.probeInterval, 1, 240);
						if (ImGui::Button("Recapture now")) s.probeCountdown = 0;
						ImGui::DragFloat3("Probe pos", &s.probePos.x, 5.f);
						ImGui::DragFloat3("Probe box (half)", &s.probeBox.x, 5.f, 1.f, 100000.f);
					}
					else ImGui::TextDisabled("DXR uses a GGX/diffuse prefiltered copy of the selected sky.");
				}
				if (ImGui::CollapsingHeader("Indirect lighting", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool giOn = s.giEnabled && (!s.dxrRenderer || !tun || tun->dxrIndirectGi);
					if (ImGui::Checkbox("Enabled##gi", &giOn)) {
						s.giEnabled = giOn;
						if (s.dxrRenderer && tun) { tun->dxrIndirectGi = giOn; s.giUseRT = true; }
						s.StartGiPrime();
					}
					ImGui::BeginDisabled(!giOn);
					ImGui::Text("%d x %d x %d = %d probes", s.giCx, s.giCy, s.giCz, s.giCx * s.giCy * s.giCz);
					ImGui::SliderFloat("Intensity##gi", &s.giIntensity, 0.f, 4.f, "%.2f");
					if (ImGui::TreeNode("Advanced probe settings")) {
						ImGui::SliderFloat("Hysteresis", &s.giHysteresis, 0.f, 0.99f, "%.2f");
						ImGui::SliderFloat("Firefly clamp", &s.giFireflyClamp, 1.f, 128.f, "%.1f HDR");
						if (s.dxrRenderer && tun)
						{
							ImGui::SliderFloat("Infinite bounce", &tun->dxrGiInfiniteBounce, 0.f, 2.f, "%.2f");
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Feeds each probe's own irradiance back into new probe\nupdates so light can bounce more than once. 0 disables\nit (single-bounce only); ~1 approximates a plausible\nsecond bounce. Ramps in naturally as the volume primes.");
						}
						ImGui::Text(s.giScheduler.IsPriming() ? "priming... probe %d / %d" : "primed (%d probes)",
						s.giScheduler.Cursor(), s.giCx * s.giCy * s.giCz);
						ImGui::SliderInt("Prime batch", &s.giPrimeBatch, 1, 32);
						ImGui::Checkbox("Keep updating (dynamic)", &s.giKeepUpdating);
						if (s.giKeepUpdating) ImGui::SliderInt("Trickle skip", &s.giFrameSkip, 1, 30);
						ImGui::DragFloat3("Volume origin", &s.giOrigin.x, 10.f);
						ImGui::DragFloat3("Probe spacing", &s.giSpacing.x, 5.f, 1.f, 100000.f);
						ImGui::Checkbox("Auto re-prime on light change", &s.giAutoReprime);
						ImGui::TreePop();
					}
					if (s.dxrRenderer) ImGui::SliderInt("Rays per probe", &s.giRTRayCount, 8, 512);
					ImGui::Checkbox("Show volume bounds", &s.giShowVolumeBounds);
					if (ImGui::Button("Re-prime volume")) s.StartGiPrime();
					if (s.deferred && !s.dxrRenderer) ImGui::Checkbox("Show GI term", &s.deferred->GetTunables().giViz);
					ImGui::EndDisabled();
				}
				if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (!s.dxrRenderer) ImGui::Checkbox("Clustered culling", &s.wantClustered);

					ImGui::Text("%d local light(s)", (int)s.pointLights.size());
					ImGui::Separator();

					const float kR2D = 57.29578f, kD2R = 0.01745329f;
					for (int i = 0; i < (int)s.pointLights.size(); ++i)
					{
						Impl::LightExtra* ex = (i < (int)s.lightExtra.size()) ? &s.lightExtra[i] : nullptr;
						const bool isSpot = ex && ex->spotCosOuter > 0.f;
						ImGui::PushID(i);
						if (ImGui::TreeNodeEx("l", 0, "%s %d",
						isSpot ? "Spot" : "Point", i))
						{
							ImGui::DragFloat3("Translation", &s.pointLights[i].position.x, 2.0f);
							if (isSpot)
							{
								if (ImGui::DragFloat3("Direction", &ex->spotDir.x, 0.02f, -1.f, 1.f))
								{
									float l = ex->spotDir.Length();
									if (l > 1e-4f) ex->spotDir = ex->spotDir / l;
								}
								float outerDeg = std::acos(std::clamp(ex->spotCosOuter, -1.f, 1.f)) * kR2D;
								float innerDeg = std::acos(std::clamp(ex->spotCosInner, -1.f, 1.f)) * kR2D;
								if (ImGui::SliderFloat("Outer", &outerDeg, 4.f, 80.f, "%.0f deg"))
								ex->spotCosOuter = std::cos(outerDeg * kD2R);
								if (ImGui::SliderFloat("Inner", &innerDeg, 2.f, 78.f, "%.0f deg"))
								ex->spotCosInner = std::cos(std::min(innerDeg, outerDeg - 1.f) * kD2R);
							}
							ImGui::ColorEdit3("Colour / intensity", &s.pointLights[i].color.r, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
							ImGui::DragFloat("Range", &s.pointLights[i].range, 10.f, 20.f, 20000.f, "%.0f");
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}

				endTab();
			}
			if (beginTab("Materials")) {
				if (ImGui::CollapsingHeader("Material preview", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Show debug sphere", &s.showDebugBall);
					if (s.debugBallValid)
					{
						DeferredRenderer::DebugMaterial& dm = s.debugMat;
						ImGui::ColorEdit3("Base colour", dm.baseColor);
						ImGui::SliderFloat("Roughness", &dm.roughness, 0.f, 1.f, "%.3f");
						ImGui::SliderFloat("Metalness", &dm.metalness, 0.f, 1.f, "%.3f");
						ImGui::SliderFloat("AO", &dm.ao, 0.f, 1.f, "%.3f");
						ImGui::ColorEdit3("Emissive colour", dm.emissiveColor);
						EmissiveLuminanceSlider("Emissive luminance", dm.emissiveStrength);
						ImGui::InputTextWithHint("##dbgtgmat", "path/to/foo.tgmat", s.dbgTgmatPath, sizeof(s.dbgTgmatPath));
						ImGui::SameLine();
						if (ImGui::Button("Load .tgmat##dbg"))
						LoadTgmatInto(s.dbgTgmatPath, s.debugMat);
						ImGui::Checkbox("Emits light (area light proxy)", &s.debugBallEmitsLight);
						if (s.debugBallEmitsLight)
						{
							ImGui::SliderFloat("Emissive light gain", &s.debugEmissiveLightGain, 0.f, 0.15f, "%.3f");
							if (!s.dxrRenderer) ImGui::Checkbox("Proxy casts shadow (costly)", &s.debugEmissiveCastShadow);
							else ImGui::TextDisabled("Proxy lights use ray-traced shadows.");
						}
						ImGui::Separator();
						ImGui::Checkbox("Follow camera", &s.debugBallFollowCam);
						ImGui::SliderFloat("Sphere radius", &s.debugBallRadius, 5.f, 400.f, "%.0f");
						if (!s.debugBallFollowCam)
						ImGui::DragFloat3("Sphere pos", &s.debugBallPos.x, 5.f);

						ImGui::Separator();
						ImGui::Checkbox("Orbiting spheres", &s.showOrbitBalls);
						if (s.showOrbitBalls)
						{
							ImGui::TextDisabled("share the material above");
							ImGui::SliderInt("Count", &s.orbitBallCount, 1, Impl::kMaxOrbitBalls);
							ImGui::SliderFloat("Orbit radius", &s.orbitPathRadius, 10.f, 8000.f, "%.0f");
							ImGui::SliderFloat("Orbit height", &s.orbitHeight, -2000.f, 2000.f, "%.0f");
							ImGui::SliderFloat("Ball radius", &s.orbitBallRadius, 4.f, 400.f, "%.0f");
							ImGui::SliderFloat("Orbit speed", &s.orbitSpeed, -3.f, 3.f, "%.2f rad/s");
							ImGui::Checkbox("Orbiting spheres emit light", &s.orbitBallsEmitLight);
						}
					}
					else ImGui::TextDisabled("Primitives/Sphere.fbx not loaded");
				}
				if (s.IsPillarTest() && ImGui::CollapsingHeader("Pillar Test material", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Override atlas material", &s.pillarMaterialOverride);
					if (s.pillarMaterialOverride)
					{
						ImGui::ColorEdit3("Base colour##pillar", s.pillarMat.baseColor);
						ImGui::SliderFloat("Roughness##pillar", &s.pillarMat.roughness, 0.f, 1.f, "%.3f");
						ImGui::SliderFloat("Metalness##pillar", &s.pillarMat.metalness, 0.f, 1.f, "%.3f");
						ImGui::SliderFloat("AO##pillar", &s.pillarMat.ao, 0.f, 1.f, "%.3f");
						ImGui::ColorEdit3("Emissive colour##pillar", s.pillarMat.emissiveColor);
						EmissiveLuminanceSlider("Emissive luminance##pillar", s.pillarMat.emissiveStrength);
					}
				}
				if (tun && ImGui::CollapsingHeader("Glass refraction", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::SliderFloat("Index of refraction", &tun->glassIor, 1.01f, 2.50f, "%.3f");
					ImGui::SliderFloat("Refraction strength", &tun->glassRefractionScale, 0.f, 3.f, "%.2f");
					ImGui::SliderFloat("Glass thickness (cm)", &tun->glassThickness, 0.f, 100.f, "%.1f");
					ImGui::SliderFloat("Absorption", &tun->glassAbsorption, 0.f, 4.f, "%.2f");
					ImGui::TextDisabled("Applies to materials matched by BENCH_TRANSPARENT_MATS.");
				}

				endTab();
			}
			if (beginTab("Atmosphere")) {
				ImGui::BeginDisabled(!s.useDeferred);
				if (tun && ImGui::CollapsingHeader("Atmosphere"))
				{
					bool atmosphereChanged = false;
					atmosphereChanged |= ImGui::Checkbox("Height fog", &tun->fogEnabled);
					ImGui::BeginDisabled(!tun->fogEnabled);
					atmosphereChanged |= ImGui::SliderFloat("Fog density / m", &tun->fogDensity, 0.f, 0.05f, "%.4f");
					atmosphereChanged |= ImGui::SliderFloat("Height falloff / m", &tun->fogHeightFalloff, 0.f, 0.2f, "%.3f");
					atmosphereChanged |= ImGui::DragFloat("Base height (m)", &tun->fogBaseHeight, 0.1f);
					atmosphereChanged |= ImGui::SliderFloat("Fog start (m)", &tun->fogStartDistance, 0.f, 50.f);
					atmosphereChanged |= ImGui::SliderFloat("Fog range (m)", &tun->fogMaxDistance, 10.f, 1000.f);
					atmosphereChanged |= ImGui::ColorEdit3("Fog colour (linear)", tun->fogColor);
					atmosphereChanged |= ImGui::Checkbox("Fog affects sky", &tun->fogAffectSky);
					atmosphereChanged |= ImGui::Checkbox("Volumetric sunlight", &tun->volumetricEnabled);
					ImGui::BeginDisabled(!tun->volumetricEnabled);
					atmosphereChanged |= ImGui::SliderFloat("Sun scattering", &tun->volumetricStrength, 0.f, 2.f);
					atmosphereChanged |= ImGui::SliderFloat("Forward scattering", &tun->volumetricAnisotropy, 0.f, 0.8f);
					atmosphereChanged |= ImGui::SliderFloat("Sunlight range (m)", &tun->volumetricDistance, 5.f, 300.f);
					atmosphereChanged |= ImGui::SliderInt("Sunlight steps", &tun->volumetricSteps, 8, 64);
					atmosphereChanged |= ImGui::Checkbox("Sun disk", &tun->sunDiskEnabled);
					ImGui::BeginDisabled(!tun->sunDiskEnabled);
					float sunDiskDegrees = tun->sunDiskAngularRadius * (180.f / 3.14159265f);
					if (ImGui::SliderFloat("Sun angular radius (deg)", &sunDiskDegrees, 0.05f, 2.0f, "%.2f")) {
						tun->sunDiskAngularRadius = sunDiskDegrees * (3.14159265f / 180.f);
						atmosphereChanged = true;
					}
					atmosphereChanged |= ImGui::SliderFloat("Sun disk intensity", &tun->sunDiskIntensity, 0.f, 100.f);
					ImGui::EndDisabled();
					ImGui::EndDisabled();
					atmosphereChanged |= ImGui::Combo("Atmosphere view", &tun->atmosphereDebugView, "Beauty\0Transmittance\0Scattered sunlight\0");
					ImGui::EndDisabled();
					if (atmosphereChanged && s.deferred) s.deferred->ResetTemporalHistory();
				}

				ImGui::EndDisabled();
				if (!s.useDeferred) ImGui::TextDisabled("Requires the deferred renderer.");
				endTab();
			}
			if (beginTab("Post FX")) {
				ImGui::BeginDisabled(!s.useDeferred);
				if (ImGui::CollapsingHeader("Post FX", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Enabled##postfx", &s.wantPostFx);
					if (tun)
					{
						ImGui::BeginDisabled(!s.wantPostFx);
						ImGui::Checkbox("Bloom##toggle", &tun->bloomEnabled);
						ImGui::SameLine();
						ImGui::TextDisabled(tun->bloomEnabled ? "(on)" : "(off - sliders inert)");
						ImGui::BeginDisabled(!tun->bloomEnabled);
						ImGui::SliderFloat("Bloom threshold", &tun->bloomThreshold, 0.1f, 8.f, "%.2f");
						ImGui::SliderFloat("Bloom knee", &tun->bloomKnee, 0.f, 1.f, "%.2f");
						ImGui::SliderFloat("Bloom intensity", &tun->bloomIntensity, 0.f, 0.5f, "%.3f");
						if (tun->bloomThreshold < 1.0f)
						ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
						"threshold this low blooms lit surfaces, not just highlights");
						ImGui::EndDisabled();
						ImGui::SeparatorText("Camera exposure");
						ImGui::Checkbox("Auto exposure (metered)", &tun->exposureAuto);
						if (tun->exposureAuto)
						{
							ImGui::DragFloatRange2("EV100 range", &tun->autoEvMin, &tun->autoEvMax, 0.1f, -6.f, 20.f, "min %.1f", "max %.1f");
							ImGui::SliderFloat("Adapt speed", &tun->exposureSpeed, 0.25f, 10.f, "%.2f");
						}
						else
						{
							ImGui::SliderFloat("Aperture", &tun->cameraAperture, 1.0f, 32.f, "f/%.1f", ImGuiSliderFlags_Logarithmic);
							float shutterDenominator = 1.f / tun->cameraShutter;
							if (ImGui::SliderFloat("Shutter", &shutterDenominator, 1.f, 8000.f, "1/%.0f s", ImGuiSliderFlags_Logarithmic))
								tun->cameraShutter = 1.f / std::max(shutterDenominator, 1e-3f);
							ImGui::SliderFloat("ISO", &tun->cameraIso, 50.f, 12800.f, "%.0f", ImGuiSliderFlags_Logarithmic);
							ImGui::Text("EV100 %.2f", Photometry::Ev100FromCamera(tun->cameraAperture, tun->cameraShutter, tun->cameraIso));
							ImGui::TextDisabled("Sunny 16: f/16, 1/125, ISO 100 (EV 15).");
						}
						ImGui::SliderFloat("EV comp", &tun->exposureComp, -5.f, 5.f, "%+.2f EV");
						ImGui::Checkbox("Pre-exposure (DXR)", &tun->preExposure);
						ImGui::SetItemTooltip("Keeps night and day scenes inside FP16's precise range.");
						const char* tonemappers[] = { "AgX", "AgX Punchy", "ACES (fitted)", "None (clip)" };
						ImGui::Combo("Tonemapper", &tun->tonemapper, tonemappers, 4);
						ImGui::EndDisabled();
					}
				}
				ImGui::EndDisabled();
				if (!s.useDeferred) ImGui::TextDisabled("Requires the deferred renderer.");
				endTab();
			}
			if (beginTab("Debug")) {
				ImGui::Checkbox("Performance overlay", &s.showPerfOverlay);
				ImGui::Checkbox("Light markers", &s.showLightMarkers);
				ImGui::TextDisabled("RMB look / WASD move / Shift fast / F5 save camera");
				if (s.dxrRenderer && tun) {
					ImGui::BeginDisabled(!tun->taaEnabled);
					ImGui::Combo("TAA view", &tun->taaDebugView, "Resolved\0Reprojected history\0History rejection\0");
					if (ImGui::Button("Reset TAA history")) s.deferred->ResetTemporalHistory();
					ImGui::EndDisabled();
					ImGui::Combo("Lighting view", &tun->dxrLightingView, "Beauty\0Ambient occlusion\0Environment\0Diffuse GI\0Albedo\0Material AO / roughness / metalness\0Texture mip\0Motion vectors / validity\0Inverse device depth\0Specular AA adjustment / roughness\0Raw sun visibility\0Geometric normal\0Shading normal\0Sun shading without shadows\0Sun geometric facing\0Raw GI amplified 5000x (debug)\0");

				} else {
					const char* views[] = {"Lit","Albedo","Normal","Roughness","Metalness","Baked AO","Emissive","Depth","SSAO"};
					ImGui::Combo("G-buffer view", &s.gbufChannel, views, IM_ARRAYSIZE(views));
					if (s.deferred && !s.dxrRenderer && ImGui::CollapsingHeader("Experimental raster / DXR integration"))
					{
						const bool dxrAvailable = DX11::Rhi()->SupportsRaytracingTier11();
						ImGui::BeginDisabled(!dxrAvailable);
						if (!dxrAvailable)
						{
							ImGui::TextDisabled("(DXR tier 1.1 not available on this device/backend)");
						}
						else
						{
							static bool dxrSunShadows = false;
							if (ImGui::Checkbox("DXR sun shadows", &dxrSunShadows)) s.deferred->SetDxrSunShadows(dxrSunShadows);
							ImGui::Checkbox("Show DXR sun visibility", &s.deferred->GetTunables().dxrSunShadowDebug);
							const bool rtGiAvailable = s.deferred->HasGiRT();
							ImGui::BeginDisabled(!rtGiAvailable);
							if (ImGui::Checkbox("Experimental DXR GI capture", &s.giUseRT))
							{
								// A source change must restart the volume; mixing old raster probes
								// with new ray-traced probes produces an invalid lighting result.
								s.StartGiPrime();
							}
							if (s.giUseRT) ImGui::SliderInt("GI rays per probe", &s.giRTRayCount, 8, 512);
							ImGui::TextDisabled("Authoritative frame: deferred HDR + %s",
							s.giUseRT ? "experimental DXR GI" : "raster GI");
							ImGui::EndDisabled();
							if (!rtGiAvailable)
							ImGui::TextDisabled("(GI probe volume not initialized -- enable Emissive GI first)");
						}
						ImGui::EndDisabled();
					}

				}
				endTab();
			}
			ImGui::EndTabBar();
		}
		ImGui::EndTabItem();
		}
		{
			const bool selectProfiler = s.benchFrames > 0 && s.bench.debugTab == "Profiler";
			if (ImGui::BeginTabItem("Profiler", nullptr, selectProfiler ? ImGuiTabItemFlags_SetSelected : 0))
			{
				ImGui::BeginChild("ProfilerScroll", ImVec2(0, 0), false);
				s.DrawProfilerTab();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
		}
		ImGui::PopItemWidth();
	}
	ImGui::End();
	ImGui::PopStyleVar(3);

	// --- GI probe volume bounds, projected onto the screen as a pink box ---
	// A LineDrawer-based 3D world-space box was tried first and drawn earlier
	// in the frame (right after SetCamera), but it was never visible: this
	// engine's DXR path renders the whole frame via a full-screen compute
	// dispatch that overwrites the color target afterward rather than
	// compositing on top of prior raster draws, so anything drawn before it
	// gets silently stomped. This overlay instead uses the exact same
	// screen-space projection as the point-light markers just below, drawn
	// from ImGui's background draw list -- which is composited after the
	// game's render pass regardless of which renderer (raster or DXR) was
	// used, so it is actually visible either way. Also cheaper: one CPU-side
	// matrix multiply per corner instead of 12 separate GPU draw calls.
	if (s.giShowVolumeBounds && s.giEnabled)
	{
		const Matrix4x4f viewProj = Matrix4x4f::GetFastInverse(s.camera.GetTransform()) * s.camera.GetProjection();
		const ImVec2 disp = ImGui::GetIO().DisplaySize;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();

		// giOrigin is probe (0,0,0)'s position, already inset half a cell from
		// the volume's true edge (see the auto-sizing comment where giOrigin
		// is computed), so the box spans from half a cell before the first
		// probe to half a cell past the last one on each axis.
		const Vector3f half = s.giSpacing * 0.5f;
		const Vector3f boxMin = s.giOrigin - half;
		const Vector3f boxMax = s.giOrigin + Vector3f{
			s.giSpacing.x * (float)(s.giCx - 1), s.giSpacing.y * (float)(s.giCy - 1), s.giSpacing.z * (float)(s.giCz - 1) } + half;
		const Vector3f corners[8] = {
			{ boxMin.x, boxMin.y, boxMin.z }, { boxMax.x, boxMin.y, boxMin.z },
			{ boxMax.x, boxMin.y, boxMax.z }, { boxMin.x, boxMin.y, boxMax.z },
			{ boxMin.x, boxMax.y, boxMin.z }, { boxMax.x, boxMax.y, boxMin.z },
			{ boxMax.x, boxMax.y, boxMax.z }, { boxMin.x, boxMax.y, boxMax.z },
		};

		bool visible[8]; ImVec2 screen[8];
		for (int i = 0; i < 8; ++i)
		{
			const Vector3f& p = corners[i];
			Vector4f clip = Vector4f(p.x, p.y, p.z, 1.f) * viewProj;
			visible[i] = clip.w > 0.001f;
			if (visible[i])
			{
				const float nx = clip.x / clip.w, ny = clip.y / clip.w;
				screen[i] = { (nx * 0.5f + 0.5f) * disp.x, (0.5f - ny * 0.5f) * disp.y };
			}
		}
		// 4 bottom edges, 4 top edges, 4 verticals connecting them.
		static const int kEdges[12][2] = {
			{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
		};
		const ImU32 pink = IM_COL32(255, 0, 255, 255);
		for (const auto& edge : kEdges)
			if (visible[edge[0]] && visible[edge[1]])
				dl->AddLine(screen[edge[0]], screen[edge[1]], pink, 2.f);
	}

	// --- world-space light markers projected onto the screen ---
	if (s.showLightMarkers && !s.pointLights.empty())
	{
		const Matrix4x4f viewProj = Matrix4x4f::GetFastInverse(s.camera.GetTransform()) * s.camera.GetProjection();
		const ImVec2 disp = ImGui::GetIO().DisplaySize;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		for (int i = 0; i < (int)s.pointLights.size(); ++i)
		{
			const Vector3f p = s.pointLights[i].position;
			Vector4f clip = Vector4f(p.x, p.y, p.z, 1.f) * viewProj;
			if (clip.w <= 0.001f) continue;
			const float nx = clip.x / clip.w, ny = clip.y / clip.w;
			if (nx < -1.3f || nx > 1.3f || ny < -1.3f || ny > 1.3f) continue;
			const ImVec2 sp{ (nx * 0.5f + 0.5f) * disp.x, (0.5f - ny * 0.5f) * disp.y };

			Impl::LightExtra* ex = (i < (int)s.lightExtra.size()) ? &s.lightExtra[i] : nullptr;
			const bool isSpot = ex && ex->spotCosOuter > 0.f;
			const ImU32 col = isSpot ? IM_COL32(255, 210, 90, 255) : IM_COL32(120, 200, 255, 255);
			dl->AddCircle(sp, 7.f, col, 12, 2.f);
			dl->AddLine(ImVec2(sp.x - 11, sp.y), ImVec2(sp.x + 11, sp.y), col, 1.5f);
			dl->AddLine(ImVec2(sp.x, sp.y - 11), ImVec2(sp.x, sp.y + 11), col, 1.5f);
			char lbl[32]; snprintf(lbl, sizeof(lbl), "%s %d", isSpot ? "spot" : "pt", i);
			dl->AddText(ImVec2(sp.x + 12, sp.y - 6), col, lbl);

			if (isSpot)
			{
				const Vector3f tip = p + ex->spotDir * 90.f;
				Vector4f tc = Vector4f(tip.x, tip.y, tip.z, 1.f) * viewProj;
				if (tc.w > 0.001f)
				{
					const ImVec2 tsp{ (tc.x / tc.w * 0.5f + 0.5f) * disp.x, (0.5f - tc.y / tc.w * 0.5f) * disp.y };
					dl->AddLine(sp, tsp, col, 2.f);
				}
			}
		}
	}
#endif
}
