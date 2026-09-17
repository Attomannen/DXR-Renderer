#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorld.h"
#include "CubemapPrefilter.h"
#include <cstdio>
#include <tge/render/DeferredRenderer.h>
#include <tge/render/RayTracingMaterialTable.h>
#include <tge/graphics/RenderTarget.h>
#include <tge/graphics/DepthBuffer.h>
#include <tge/render/RenderGraph.h>
#include <tge/render/RenderResourcePool.h>

#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/graphics/DX11.h>
#include <tge/graphics/Camera.h>
#include <tge/graphics/AmbientLight.h>
#include <tge/graphics/DirectionalLight.h>
#include <tge/graphics/PointLight.h>
#include <tge/math/Photometry.h>
#include <tge/debugging/CpuProfiler.h>
#include <tge/drawers/ModelDrawer.h>
#include <tge/render/GpuProfiler.h>
#include <tge/model/Model.h>
#include <tge/model/ModelFactory.h>
#include <tge/model/ModelInstance.h>
#include <tge/texture/TextureManager.h>
#include <tge/texture/texture.h>
#include <tge/input/InputManager.h>
#include <tge/application.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>
#include <tge/EngineDefines.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <DirectXTex/ScreenGrab/ScreenGrab11.h>
#pragma comment(lib, "windowscodecs.lib")

#include <nlohmann/json.hpp>

#ifndef _RETAIL
#include <imgui/imgui.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <vector>

using namespace Tga;

GameWorld* GameWorld::ourInstance = nullptr;

namespace
{
	std::string EnvStr(const char* name, const char* def)
	{
		const char* v = std::getenv(name);
		return v ? std::string(v) : std::string(def);
	}
	int   EnvInt(const char* name, int def)   { const char* v = std::getenv(name); return v ? std::atoi(v) : def; }
	float Deg2Rad(float d) { return d * 0.01745329252f; }

	std::string LowerStr(std::string s)
	{
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}
	// "glass.003" -> "glass" : drop a trailing ".<digits>" (Blender dedup suffix)
	std::string StripDupSuffix(const std::string& s)
	{
		const size_t dot = s.rfind('.');
		if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size()) return s;
		for (size_t i = dot + 1; i < s.size(); ++i)
			if (!std::isdigit((unsigned char)s[i])) return s;
		return s.substr(0, dot);
	}
	bool MatchesAny(const std::string& matName, const std::vector<std::string>& keys)
	{
		const std::string n = StripDupSuffix(LowerStr(matName));
		for (const std::string& k : keys)
			if (!k.empty() && n.find(k) != std::string::npos) return true;
		return false;
	}
	float Rad2Deg(float r) { return r * 57.2957795131f; }

	namespace fs = std::filesystem;
	using json = nlohmann::json;

	// Sentinel scene name for the procedural "engine room" (no .tgs on disk).

	// A fixed-parameter PBR material, matching DeferredRenderer::DebugMaterial +
	// optional [C,N,M,FX] texture map paths. Serialised as .tgmat JSON by the
	// GameEditor's Material Editor; loaded here for the debug sphere.
	struct MaterialDef
	{
		std::string surfaceType = "Opaque"; // Opaque, Masked, Transparent; authored in .tgmat
		float alphaCutoff      = 0.33f;
		float baseColor[3]     = { 0.8f, 0.8f, 0.8f };
		float roughness        = 0.5f;
		float metalness        = 0.0f;
		float ao               = 1.0f;
		float emissiveColor[3] = { 0.0f, 0.0f, 0.0f };
		float emissiveStrength = 0.0f;
		std::array<std::string, 4> maps{ "", "", "", "" };
	};

#ifndef _RETAIL
	// Edits an emissive strength (scene units) as surface luminance in cd/m².
	bool EmissiveLuminanceSlider(const char* aLabel, float& aStrength)
	{
		float nits = aStrength * Photometry::kNitsPerUnit;
		if (!ImGui::SliderFloat(aLabel, &nits, 0.f, 1.0e7f, "%.0f cd/m2", ImGuiSliderFlags_Logarithmic)) return false;
		aStrength = Photometry::NitsToUnits(nits);
		return true;
	}
#endif

	bool LoadTgmat(const fs::path& path, MaterialDef& out)
	{
		std::ifstream in(path);
		if (!in) { ERROR_PRINT("tgmat: cannot open %s", path.string().c_str()); return false; }
		json j; try { in >> j; } catch (const std::exception& e) { ERROR_PRINT("tgmat: parse %s: %s", path.string().c_str(), e.what()); return false; }
		auto arr3 = [](const json& a, float* v) {
			if (a.is_array() && a.size() >= 3) { v[0] = a[0].get<float>(); v[1] = a[1].get<float>(); v[2] = a[2].get<float>(); }
		};
		if (j.contains("baseColor"))     arr3(j["baseColor"], out.baseColor);
		if (j.contains("emissiveColor")) arr3(j["emissiveColor"], out.emissiveColor);
		out.surfaceType      = j.value("surfaceType", out.surfaceType);
		out.alphaCutoff      = j.value("alphaCutoff", out.alphaCutoff);
		out.roughness        = j.value("roughness", out.roughness);
		out.metalness        = j.value("metalness", out.metalness);
		out.ao               = j.value("ao", out.ao);
		out.emissiveStrength = j.value("emissiveStrength", out.emissiveStrength);
		// Physical alternative: surface luminance in cd/m² (a lit phone screen
		// is ~500, a frosted bulb ~100 000).
		if (j.contains("emissiveLuminance"))
			out.emissiveStrength = Photometry::NitsToUnits(j["emissiveLuminance"].get<float>());
		if (j.contains("maps") && j["maps"].is_object())
		{
			const json& m = j["maps"];
			const char* keys[4] = { "albedo", "normal", "orm", "fx" };
			for (int i = 0; i < 4; ++i)
			{
				std::string s = m.value(keys[i], std::string());
				std::replace(s.begin(), s.end(), '\\', '/');
				out.maps[i] = s;
			}
		}
		return true;
	}

	// Copy the fixed PBR params of a .tgmat onto a DebugMaterial (texture maps are
	// ignored by the flat GBufferDebugMatPS path used for the room / debug sphere).
	bool LoadTgmatInto(const char* path, DeferredRenderer::DebugMaterial& dm)
	{
		MaterialDef md;
		if (!path || !*path || !LoadTgmat(path, md)) return false;
		for (int i = 0; i < 3; ++i) { dm.baseColor[i] = md.baseColor[i]; dm.emissiveColor[i] = md.emissiveColor[i]; }
		dm.roughness = md.roughness; dm.metalness = md.metalness; dm.ao = md.ao;
		dm.emissiveStrength = md.emissiveStrength;
		return true;
	}

	// One renderable from a .tgo / scene object: model + one .tgmat asset per
	// mesh/material + a world transform.
	struct SceneEntry
	{
		std::string fbx;
		std::vector<std::string> materials;
		Matrix4x4f transform;                               // identity by default
	};

	// Pull the "Model" property out of a .tgo's "properties" array (or a scene
	// object file that carries the property inline).
	bool ParseModelProperty(const json& propsHolder, SceneEntry& out)
	{
		if (!propsHolder.contains("properties")) return false;
		for (const json& p : propsHolder["properties"])
		{
			if (p.value("type", "") != "Model" && p.value("name", "") != "Model") continue;
			const json& v = p["value"];
			out.fbx = v.value("path", "");
			// tgo paths use backslashes; normalise for ResolveAssetPath
			std::replace(out.fbx.begin(), out.fbx.end(), '\\', '/');
			out.materials.clear();
			if (v.contains("materials"))
			{
				for (const json& material : v["materials"])
				{
					std::string path = material.get<std::string>();
					std::replace(path.begin(), path.end(), '\\', '/');
					out.materials.push_back(path);
				}
			}
			return !out.fbx.empty();
		}
		return false;
	}

	std::optional<SceneEntry> LoadTgo(const fs::path& tgoPath)
	{
		std::ifstream in(tgoPath);
		if (!in) { ERROR_PRINT("bench: cannot open tgo %s", tgoPath.string().c_str()); return std::nullopt; }
		json j; try { in >> j; } catch (const std::exception& e) { ERROR_PRINT("bench: tgo parse: %s", e.what()); return std::nullopt; }
		SceneEntry e;
		if (!ParseModelProperty(j, e)) { ERROR_PRINT("bench: no Model property in %s", tgoPath.string().c_str()); return std::nullopt; }
		return e;
	}

	// Scenes are the runtime entry point. When a caller has not selected one,
	// pick the first one in a stable order instead of assuming a sample model is
	// present in the asset tree.
	std::optional<std::string> FindFirstTgsScene()
	{
		const fs::path root = Tga::Settings::GameAssetRoot();
		std::error_code ec;
		std::vector<fs::path> scenes;
		for (const fs::directory_entry& item : fs::recursive_directory_iterator(root, ec))
		{
			if (item.is_regular_file() && LowerStr(item.path().extension().string()) == ".tgs")
				scenes.push_back(item.path());
		}
		if (ec)
		{
			ERROR_PRINT("bench: cannot scan scenes under %s: %s", root.string().c_str(), ec.message().c_str());
			return std::nullopt;
		}
		if (scenes.empty()) return std::nullopt;

		std::sort(scenes.begin(), scenes.end());
		return fs::relative(scenes.front(), root, ec).replace_extension().generic_string();
	}

	// Load a .tgs scene: <gameRoot>/<name>.tgs + its <name>.leveldata/ folder of
	// object files. Each object references a .tgo (via "path") or carries the
	// Model property inline, plus translation/rotation/scale.
	std::vector<SceneEntry> LoadTgs(const std::string& name)
	{
		std::vector<SceneEntry> out;
		const fs::path root = fs::path(Tga::Settings::GameAssetRoot());
		const fs::path scenePath = root / fs::path(name).replace_extension(".tgs");
		fs::path levelData = scenePath;
		levelData.replace_extension(".leveldata");
		std::error_code ec;
		if (!fs::exists(levelData, ec))
		{
			ERROR_PRINT("bench: no leveldata for scene '%s'", name.c_str());
			return out;
		}

		// Lazily built once per scene load (only if an object actually needs
		// "object-definition" resolution) and reused for every object, instead of
		// re-walking the entire asset tree with recursive_directory_iterator for
		// each individual object -- that was an O(objects * assetTreeSize) scan
		// that dominated load time on scenes with many objects.
		std::unordered_map<std::string, fs::path> tgoByStem;
		bool tgoByStemBuilt = false;
		auto buildTgoIndex = [&]()
		{
			if (tgoByStemBuilt) return;
			tgoByStemBuilt = true;
			std::error_code scanEc;
			for (const fs::directory_entry& de : fs::recursive_directory_iterator(root, scanEc))
			{
				if (de.is_regular_file() && de.path().extension() == ".tgo")
					tgoByStem.emplace(de.path().stem().string(), de.path());
			}
			if (scanEc)
				ERROR_PRINT("bench: cannot scan object-definitions under %s: %s", root.string().c_str(), scanEc.message().c_str());
		};

		for (const fs::directory_entry& item : fs::directory_iterator(levelData, ec))
		{
			if (!item.is_regular_file() || item.path().has_extension()) continue;
			std::ifstream in(item.path());
			if (!in) continue;
			json obj; try { in >> obj; } catch (...) { continue; }

			SceneEntry e;
			bool haveModel = ParseModelProperty(obj, e);
			if (!haveModel && obj.contains("path") && !obj["path"].get<std::string>().empty())
			{
				std::string tgoRel = obj["path"].get<std::string>();
				std::replace(tgoRel.begin(), tgoRel.end(), '\\', '/');
				if (auto le = LoadTgo(root / tgoRel)) { e = *le; haveModel = true; }
			}
			// The real editor format: "object-definition": "<name>" resolves to
			// the <name>.tgo object definition anywhere under the game data root.
			if (!haveModel && obj.contains("object-definition"))
			{
				const std::string defName = obj["object-definition"].get<std::string>();
				if (!defName.empty())
				{
					buildTgoIndex();
					auto found = tgoByStem.find(defName);
					if (found != tgoByStem.end()) { if (auto le = LoadTgo(found->second)) { e = *le; haveModel = true; } }
					else ERROR_PRINT("bench: object-definition '%s' not found under %s",
						defName.c_str(), root.string().c_str());
				}
			}
			if (!haveModel) continue;

			Vector3f t{ 0,0,0 }, r{ 0,0,0 }, sc{ 1,1,1 };
			auto arr3 = [](const json& a, Vector3f& v) {
				if (a.is_array() && a.size() >= 3) v = { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() };
			};
			if (obj.contains("translation")) arr3(obj["translation"], t);
			if (obj.contains("rotation"))    arr3(obj["rotation"], r);
			if (obj.contains("scale"))       arr3(obj["scale"], sc);

			Matrix4x4f m = Matrix4x4f::CreateFromScale(sc) * Matrix4x4f::CreateFromRollPitchYaw(r);
			m.SetPosition(t);
			e.transform = m;
			out.push_back(std::move(e));
		}
		return out;
	}
}

struct GameWorld::Impl
{
	// ---- config
	int   benchFrames = 0;
	int   warmupFrames = 60;
	int   sponzaCopies = 1;
	std::string reportPath = "bench_report.json";

	// ---- scene
	std::vector<ModelInstance> models;
	// Per-model sub-mesh split: indices whose material is flagged transparent go
	// to the forward transparent pass, the rest to the deferred G-buffer.
	std::vector<std::vector<int>> opaqueMeshes;
	std::vector<std::vector<int>> transparentMeshes;
	std::vector<std::string> transparentMatKeys;   // lowercased substrings
	bool anyTransparent = false;
	std::vector<PointLight> pointLights;
	// Parallel to pointLights: spot-cone params for the deferred path. A default
	// entry (spotCosOuter <= 0) means "plain point/area light".
	struct LightExtra
	{
		Vector3f spotDir{ 0.f, -1.f, 0.f };
		float spotCosOuter = -1.f;
		float spotCosInner = -1.f;
	};
	std::vector<LightExtra> lightExtra;
	DirectionalLight dirLight;
	AmbientLight ambient;
	Camera camera;
	// The swap chain may change size while the game is running.  Keep the
	// camera projection in sync with it so a wider/taller window changes the
	// visible frustum instead of distorting the existing image.
	Vector2ui cameraProjectionSize{ 0, 0 };
	std::unique_ptr<InputManager> input;

	Vector3f sceneCenter{ 0,0,0 };
	Vector3f sceneExtents{ 1000,1000,1000 };
	float    orbitRadius = 800.f;

	// ---- free-fly state
	Vector3f camPos{ 0, 200, -800 };
	Vector3f camRot{ 10, 0, 0 };          // pitch, yaw, roll (deg)
	bool     mouseTrapped = false;
	float    flySpeed = 600.f;

	// ---- bench state
	int   frame = 0;
	bool  reportWritten = false;
	std::vector<double> frameMs;          // engine delta time (full frame incl. present)
	std::vector<double> cpuMs;            // update+render CPU time
	std::vector<int>    drawCalls;
	std::chrono::high_resolution_clock::time_point cpuStart;
	int   meshDrawTotal = 0;             // sub-meshes submitted per frame
	double modelLoadMs = 0.0;           // FBX import time for the base model
	double firstFrameMs = 0.0;          // frame 0 wall time (texture streaming / PSO warmup)
	bool  frustumCull = true;
	std::vector<Vector3f> instanceOffsets;
	std::vector<double> visibleInstances;   // per measured frame

	DeferredRenderer* deferred = nullptr;   // engine-owned; see GraphicsEngine::GetDeferredRenderer()
	std::map<const ModelInstance*, Matrix4x4f> previousRayTransforms;
	bool useDeferred = true;
	int  gbufChannel = 0;   // 0 = normal output, 1..8 = G-buffer debug view

	// ---- live-tweak state (debug UI, free-fly only)
	float sunPitch = 55.f, sunYaw = -35.f;
	float sunColor[3] = { 1.0f, 0.96f, 0.88f };
	float sunSoftness = 0.f;
	// Physical sun. Scene files store sunIntensity as a multiple of a 100 000
	// lux clear-sky sun, which is what the renderer's scene units are built on
	// (see Photometry.h), so sunIntensity 1 == 100 000 lux.
	float sunIlluminanceLux = 100000.0f;
	float sunTemperatureK = 5800.0f;
	bool  sunUseTemperature = false;   // false: use the authored sunColor
	Vector3f SunColor() const
	{
		return sunUseTemperature ? Photometry::BlackbodyToLinearSrgb(sunTemperatureK)
			: Vector3f{ sunColor[0], sunColor[1], sunColor[2] };
	}
	float SunIntensity() const { return sunIlluminanceLux / 100000.0f; }
	float ambientColor[3] = { 0.35f, 0.42f, 0.55f };
	float ambientScale = 1.0f;   // BENCH_AMBIENT env; scales the ambient/IBL term
	int   cubemapIdx = 0;        // panel Cubemap combo

	// --- reflection probe (Phase 5 stage A): one probe, re-captured every N frames ---
	std::unique_ptr<CubemapPrefilter> probePrefilter;
	CubemapData probeBase, probePrefiltered;
	// The authored sky stays at mip 0 in this cube, while its remaining mips
	// contain GGX specular and cosine-convolved diffuse lighting. Unlike the
	// dynamic local probe, it is safe to use for the DXR sky as well as IBL.
	CubemapData worldEnvironmentPrefiltered;
	std::unique_ptr<RenderTarget> probeFaceRt;
	std::unique_ptr<DepthBuffer>  probeFaceDepth;
	TextureResource* fallbackCube = nullptr;   // the env_* cube, used when the probe is off
	Vector3f probePos{ 0.f, 0.f, 0.f };
	Vector3f probeBox{ 1000.f, 1000.f, 1000.f };   // influence-box half-extents (box parallax)
	bool  probeEnabled = true;

	// --- emissive-GI irradiance volume (Phase 6 stage 1) ---
	CubemapData giCube, giDepthCube;
	std::unique_ptr<RenderTarget> giFaceRt;
	std::unique_ptr<DepthBuffer>  giFaceDepth;
	Vector3f giOrigin{ 0.f, 0.f, 0.f };
	Vector3f giSpacing{ 200.f, 200.f, 200.f };
	int   giCx = 8, giCy = 4, giCz = 8;
	float giIntensity = 1.6f;
	float giHysteresis = 0.94f;
	float giFireflyClamp = 12.0f;
	bool  giEnabled = true;
	bool  giShowVolumeBounds = true;   // draws the probe grid's outer bounds as a wireframe box
	int   giCursor = 0;
	int   giPrimeBatch = 8;     // probes/frame while first populating the volume
	int   giTrickle = 4;        // bounded RT probes/frame after priming
	int   giFrameSkip = 1;      // DXR updates every frame; raster retains a cheaper cadence below
	int   giSkipCount = 0;
	bool  giPriming = true;
	bool  giKeepUpdating = true;
	bool  giAutoReprime = true;   // re-prime when the sun / ambient changes
	float giLightHash = 0.f;
	int   giLightingRefreshProbeBudget = 0; // one fast-response sweep after a light edit
	static constexpr int kGiFaceRes = 16;
	int   probeInterval = 30;      // recapture cadence (frames); BENCH_PROBE_INTERVAL
	int   probeCountdown = 0;
	static constexpr int kProbeRes = 256;

	bool RebuildWorldEnvironmentPrefilter()
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

	// --- material preview debug sphere ---
	ModelInstance debugBall;
	bool debugBallValid = false;
	bool showDebugBall = false;
	DeferredRenderer::DebugMaterial debugMat;
	bool pillarMaterialOverride = true;
	DeferredRenderer::DebugMaterial pillarMat;
	Vector3f debugBallPos{ 0.f, 0.f, 0.f };
	float debugBallRadius = 45.f;      // desired world-space radius
	float debugBallModelRadius = 1.f;  // FBX bounds radius (from the model)
	bool debugBallFollowCam = true;
	bool debugBallEmitsLight = true;   // emissive -> real area light
	float debugEmissiveLightGain = 0.03f;
	bool debugEmissiveCastShadow = false;   // proxy light casts a (costly) cube shadow

	// Orbiting material-preview spheres: share debugMat with the debug ball so you
	// can watch several identical spheres light each other / the room as they move.
	static constexpr int kMaxOrbitBalls = 24;
	std::vector<ModelInstance> orbitBalls;   // pool, sized kMaxOrbitBalls at load
	bool  showOrbitBalls = false;
	int   orbitBallCount = 4;
	float orbitBallRadius = 60.f;    // world radius of each orbiting sphere
	float orbitPathRadius = 0.f;     // 0 => auto from scene extents
	float orbitHeight = 0.f;         // vertical offset from scene centre
	float orbitSpeed = 0.4f;         // radians / second
	bool  orbitBallsEmitLight = true;
	float orbitAngle = 0.f;          // accumulated orbit phase (radians)
	float animTime = 0.f;            // wall-clock seconds, advanced in Update()
	// Last positions consumed by the GI cache. They intentionally update after
	// tracing, so dynamic-probe scheduling can dirty both sides of an emitter's
	// movement on the following frame.
	std::vector<Vector3f> giPreviousEmissivePositions;
	std::vector<uint8_t> giDynamicDirtyMask;
	uint32_t giDynamicProbeCursor = 0;

	bool sealScene = false;
	// Procedural interior detection: fade sun + sky-IBL by GI-probe sky visibility.
	// 0 = off, 1 = full. Needs GI enabled + the volume covering the play space.
	float autoSeal = 0.f;
	char dbgTgmatPath[260] = "";
	bool  wantShadows = true, wantSSAO = true, wantClustered = true, wantPostFx = true;
	bool  wantLocalShadows = true;   // point/spot shadow atlas
	bool  wantSSR = true;
	// The active renderer mode. DXR owns primary shading when true; raster
	// lighting options are visibly disabled rather than silently mixed in.
	bool  dxrRenderer = true;
	// Raster probe capture is the authoritative shipping path while the DXR
	// integration is rebuilt.  The old inline path mixed a separate lighting
	// model into the same SH buffer as the raster capture, so its output could
	// not be compared or tuned reliably.  DXR remains available as an explicit
	// validation mode, but can no longer replace the frame by default.
	bool  giUseRT = true;
	bool  giBatchProbes = true;   // BENCH_GI_BATCH=0 reverts to one dispatch per probe (A/B)
	int   giRTRayCount = 256;
	int   giProbeBudget = 0;           // RT probes traced per frame while updating; 0 = auto (32k rays)
	// RT probe capture waits for the current frame's TLAS construction.
	bool  giRtCapturePending = false;
	bool  debugUiOpen = true;
	bool  showLightMarkers = true;
	bool  showPerfOverlay = true;

	// ---- feature cost sweep (Profiler tab) ----
	// Turns one feature off at a time and measures the median GPU frame time,
	// which splits the single "Ray trace + shade" dispatch into its parts.
	struct CostProbe
	{
		const char* name;
		std::function<bool()> isOn;           // false: skip, nothing to measure
		std::function<void(bool)> set;        // true restores the original setting
	};
	struct CostResult { const char* name; float frameMs; float deltaMs; bool skipped; float spreadMs = 0.f; };
	std::vector<CostProbe> costProbes;
	std::vector<CostResult> costResults;
	std::vector<float> costSamples;
	std::vector<float> costOnMs;
	int   costProbe = -2;                     // -2 idle, >= 0 probe index
	int   costFrame = 0;
	bool  costPhaseOff = false;               // measuring with the probe turned off
	int   costCycle = 0;                      // on/off alternations done for this probe
	float costCurrentOnMs = 0.f, costCurrentOffMs = 0.f;
	std::vector<float> costDeltas;
	float costBaselineMs = 0.f;
	static constexpr int kCostWarmupFrames = 20;
	static constexpr int kCostMeasureFrames = 40;
	static constexpr int kCostCycles = 3;
	std::string screenshotPath;
	int  shotFrame = 0;   // BENCH_SHOT_FRAME; 0 = default (end of run)
	bool screenshotTaken = false;

	GpuProfiler gpu;
	std::map<std::string, std::vector<double>> gpuScopeMs;   // scope name -> per-frame ms
	std::vector<double> gpuFrameMs;

	float modelRotX = 0.f;   // BENCH_ROT_X degrees about X, e.g. -90 to bring Z-up content to Y-up

	void ComputeBounds(const std::shared_ptr<Model>& model)
	{
		if (const char* r = std::getenv("BENCH_ROT_X")) modelRotX = (float)atof(r);

		const float rad = Deg2Rad(modelRotX);
		const float cs = std::cos(rad), sn = std::sin(rad);
		auto rot = [&](Vector3f v) { return Vector3f{ v.x, v.y * cs - v.z * sn, v.y * sn + v.z * cs }; };

		Vector3f mn{ 1e9f, 1e9f, 1e9f }, mx{ -1e9f, -1e9f, -1e9f };
		for (const auto& md : model->GetMeshDataList())
		{
			const Vector3f c = md.bounds.center;
			const Vector3f e = md.bounds.boxExtents;
			for (int sx = -1; sx <= 1; sx += 2)
			for (int sy = -1; sy <= 1; sy += 2)
			for (int sz = -1; sz <= 1; sz += 2)
			{
				const Vector3f p = rot({ c.x + sx * e.x, c.y + sy * e.y, c.z + sz * e.z });
				mn = { std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z) };
				mx = { std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z) };
			}
		}
		if (mn.x > mx.x) { mn = { -1000,-1000,-1000 }; mx = { 1000,1000,1000 }; }
		sceneCenter  = (mn + mx) * 0.5f;
		sceneExtents = (mx - mn) * 0.5f;
	}

	// With a saved viewpoint (bench_camera.json): "fixed" holds it exactly (still
	// image), "spin" holds the position and sweeps yaw in place, "orbit" circles
	// the saved point. Default when a camera is saved is "spin". No saved camera
	// -> auto orbit around a point low in the scene.
	enum class CamMode { Fixed, Spin, Orbit } camMode = CamMode::Orbit;
	float camSpinDeg = 35.f;   // BENCH_SPIN: half-sweep for "spin" mode
	float camOrbitHeight = -1.f;  // BENCH_ORBIT_HEIGHT: fraction of sceneExtents.y; <0 = oscillate
	bool  orbitRoom = false;   // BENCH_CAM=room: orbit the scene centre even with a saved camera

	void SetScriptedCamera()
	{
		// BENCH_FREEZE_FRAME=N holds the scripted camera where it was at frame N,
		// for judging temporal stability with a truly still view.
		static const int freezeFrame = EnvInt("BENCH_FREEZE_FRAME", 0);
		const int cameraFrame = freezeFrame > 0 ? std::min(frame, freezeFrame) : frame;
		const float u = benchFrames > 1 ? (float)cameraFrame / (float)(benchFrames - 1) : 0.f;

		if (camLoaded && camMode == CamMode::Fixed)
		{
			camera.GetTransform().SetRotation(camRot);
			camera.GetTransform().SetPosition(camPos);
			return;
		}
		if (camLoaded && camMode == CamMode::Spin)
		{
			// starts and ends at the saved yaw (so the screenshot frame matches),
			// sweeps +/- camSpinDeg through the run
			const float yawOff = camSpinDeg * std::sin(u * 6.28318530718f);
			camera.GetTransform().SetRotation(Vector3f{ camRot.x, camRot.y + yawOff, camRot.z });
			camera.GetTransform().SetPosition(camPos);
			return;
		}

		const float ang = u * 6.28318530718f * 2.0f;   // two full orbits over the run

		// Orbit around the saved viewpoint if there is one (unless BENCH_CAM=room),
		// else a point ~1/4 up from the floor (not the bounds centre, which can sit high).
		const Vector3f look = (camLoaded && !orbitRoom) ? camPos
			: sceneCenter + Vector3f{ 0, -sceneExtents.y * 0.25f, 0 };
		// BENCH_ORBIT_HEIGHT pins the orbit height (fraction of scene half-height)
		// instead of letting it oscillate over the run, so camera height can be held
		// constant while something else is A/B tested against it.
		const float orbitY = camOrbitHeight >= 0.f ? camOrbitHeight : (0.10f + 0.08f * std::sin(ang * 0.5f));
		Vector3f pos = look + Vector3f{ std::cos(ang) * orbitRadius,
		                                sceneExtents.y * orbitY,
		                                std::sin(ang) * orbitRadius };

		Vector3f dir = look - pos;
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len > 1e-4f) dir = dir * (1.0f / len);

		const float yaw   = Rad2Deg(std::atan2(dir.x, dir.z));
		const float pitch = Rad2Deg(-std::asin(std::clamp(dir.y, -1.f, 1.f)));

		camera.GetTransform().SetRotation(Vector3f{ pitch, yaw, 0 });
		camera.GetTransform().SetPosition(pos);
	}

	void UpdateFreeFly(float dt)
	{
		if (!input) return;
		input->Update();

#ifndef _RETAIL
		// Don't drive the camera while the tuning panel has the cursor / keyboard.
		const bool uiMouse = ImGui::GetIO().WantCaptureMouse;
		const bool uiKeys  = ImGui::GetIO().WantCaptureKeyboard;
#else
		const bool uiMouse = false, uiKeys = false;
#endif
		if (uiMouse) dt = 0.f;   // freeze movement/look; still process F-key shortcuts below

		if (!uiMouse && input->IsKeyPressed(VK_RBUTTON) && !mouseTrapped)
		{
			input->HideMouse(); input->CaptureMouse(); mouseTrapped = true;
		}
		if (input->IsKeyReleased(VK_RBUTTON) && mouseTrapped)
		{
			input->ShowMouse(); input->ReleaseMouse(); mouseTrapped = false;
		}

		Matrix4x4f rot = Matrix4x4f::CreateFromRollPitchYaw(camRot);
		Vector3f fwd = rot.GetForward();
		Vector3f right = rot.GetRight();
		Vector3f move{ 0,0,0 };
		if (input->IsKeyHeld('W')) move = move + fwd;
		if (input->IsKeyHeld('S')) move = move - fwd;
		if (input->IsKeyHeld('D')) move = move + right;
		if (input->IsKeyHeld('A')) move = move - right;
		if (input->IsKeyHeld('E')) move.y += 1.f;
		if (input->IsKeyHeld('Q')) move.y -= 1.f;
		const float speed = flySpeed * (input->IsKeyHeld(VK_SHIFT) ? 4.f : .4f);
		camPos = camPos + move * speed * dt;

		if (mouseTrapped && !uiMouse)
		{
			const Vector2f md = input->GetMouseDelta();
			camRot.y += md.x * 0.15f;
			camRot.x += md.y * 0.15f;
			camRot.x = std::clamp(camRot.x, -89.f, 89.f);
		}
		if (input->IsKeyPressed(VK_OEM_3)) debugUiOpen = !debugUiOpen;   // ` / ~ toggles the panel
		if (!uiKeys && input->IsKeyPressed(VK_F5)) SaveCamera();
		if (!uiKeys && input->IsKeyPressed(VK_F9)) { if (LoadCamera()) INFO_PRINT("bench: camera reloaded"); }
		if (!uiKeys && input->IsKeyPressed(VK_F6))
			INFO_PRINT("bench: camera pos (%.1f, %.1f, %.1f)  rot (%.2f, %.2f, %.2f)",
				camPos.x, camPos.y, camPos.z, camRot.x, camRot.y, camRot.z);
		if (input->IsKeyPressed(VK_ESCAPE)) PostQuitMessage(0);

		camera.GetTransform().SetRotation(camRot);
		camera.GetTransform().SetPosition(camPos);
	}

	std::string camFile = "bench_camera.json";
	bool camLoaded = false;   // a saved viewpoint is available

	void SaveCamera()
	{
		std::ofstream o(camFile);
		o << "{ \"pos\": [" << camPos.x << ", " << camPos.y << ", " << camPos.z
		  << "], \"rot\": [" << camRot.x << ", " << camRot.y << ", " << camRot.z << "] }\n";
		o.close();
		INFO_PRINT("bench: camera saved -> %s   pos (%.1f, %.1f, %.1f)  rot (%.2f, %.2f, %.2f)",
			camFile.c_str(), camPos.x, camPos.y, camPos.z, camRot.x, camRot.y, camRot.z);
	}

	bool LoadCamera()
	{
		std::ifstream in(camFile);
		if (!in) in.open(fs::path(Settings::GameAssetRoot()) / camFile);
		if (!in) return false;
		std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		float p[3] = { 0,0,0 }, r[3] = { 0,0,0 };
		size_t pp = s.find("\"pos\"");
		size_t rp = s.find("\"rot\"");
		if (pp == std::string::npos || rp == std::string::npos) return false;
		if (std::sscanf(s.c_str() + s.find('[', pp), "[ %f , %f , %f", &p[0], &p[1], &p[2]) != 3) return false;
		if (std::sscanf(s.c_str() + s.find('[', rp), "[ %f , %f , %f", &r[0], &r[1], &r[2]) != 3) return false;
		camPos = { p[0], p[1], p[2] };
		camRot = { r[0], r[1], r[2] };
		camLoaded = true;
		return true;
	}

	// Local lights are scene data. This reads the selected .tgs's
	// `lighting.lights` array only; a scene without that array has no local lights.
	bool LoadLights(const std::string& file, float sceneRadius, float exposure)
	{
		std::ifstream in(file);
		if (!in) return false;
		nlohmann::json j;
		try { in >> j; } catch (...) { ERROR_PRINT("bench: bad light file %s", file.c_str()); return false; }
		if (j.contains("lighting")) j = j["lighting"];
		if (!j.contains("lights") || !j["lights"].is_array()) return false;

		auto a3 = [](const nlohmann::json& v, Vector3f d) {
			if (v.is_array() && v.size() >= 3)
				return Vector3f{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
			return d;
		};

		int n = 0, skipped = 0;
		for (const nlohmann::json& L : j["lights"])
		{
			if (n >= DeferredRenderer::kMaxLights) { ++skipped; continue; }
			PointLight p;
			const Vector3f pos = a3(L.value("pos", L.value("position", nlohmann::json::array({0,0,0}))), Vector3f{0,0,0});
			const Vector3f col = L.contains("color") ? a3(L["color"], Vector3f{ 1,1,1 }) : Vector3f{ 1,1,1 };
			const float intensity = L.value("intensity", 6.0f) * exposure;
			p.position = pos;
			p.color  = Color{ col.x * intensity, col.y * intensity, col.z * intensity, 1.f };
			p.range  = L.value("range",  sceneRadius * 1.2f);
			p.radius = L.value("radius", 20.0f);
			pointLights.push_back(p);

			// Optional spot cone: "spot": { "dir":[x,y,z], "outer":<deg>, "inner":<deg> }
			LightExtra ex;
			if (L.contains("spot") && L["spot"].is_object())
			{
				const nlohmann::json& sp = L["spot"];
				Vector3f dir = a3(sp.value("dir", nlohmann::json::array({ 0, -1, 0 })), Vector3f{ 0,-1,0 });
				float len = dir.Length();
				ex.spotDir = len > 1e-4f ? dir / len : Vector3f{ 0,-1,0 };
				const float outer = sp.value("outer", 35.0f) * 3.14159265f / 180.0f;
				const float inner = sp.value("inner", outer * 0.7f * 180.0f / 3.14159265f) * 3.14159265f / 180.0f;
				ex.spotCosOuter = std::cos(outer);
				ex.spotCosInner = std::cos(std::min(inner, outer - 0.01f));
			}
			if (L.contains("spot") && L["spot"].is_boolean() && L["spot"].get<bool>()) {
				Vector3f dir = a3(L.value("direction", nlohmann::json::array({0,-1,0})), Vector3f{0,-1,0});
				const float len = dir.Length();
				ex.spotDir = len > 1e-4f ? dir / len : Vector3f{0,-1,0};
				const float outer = std::clamp(L.value("outerAngle", 35.0f), 0.1f, 89.9f);
				const float inner = std::clamp(L.value("innerAngle", 20.0f), 0.0f, outer);
				ex.spotCosOuter = std::cos(outer * 3.14159265f / 180.0f);
				ex.spotCosInner = std::cos(inner * 3.14159265f / 180.0f);
			}
			// Photometric alternatives to the unitless "intensity": luminous
			// intensity in candela, or flux in lumens spread over the cone.
			if (L.contains("candela") || L.contains("lumens"))
			{
				const float candela = L.contains("candela") ? L["candela"].get<float>()
					: ex.spotCosOuter > -1.f
					? Photometry::SpotLumensToCandela(L["lumens"].get<float>(), std::acos(ex.spotCosOuter))
					: Photometry::PointLumensToCandela(L["lumens"].get<float>());
				const float units = Photometry::CandelaToUnits(candela) * exposure;
				pointLights.back().color = Color{ col.x * units, col.y * units, col.z * units, 1.f };
			}
			lightExtra.push_back(ex);
			++n;
		}
		INFO_PRINT("bench: %d authored light(s) from %s%s", n, file.c_str(),
			skipped ? "  (some skipped: over kMaxLights)" : "");
		return true;
	}

	std::string currentScene = "TEST";

	bool IsPillarTest() const { return fs::path(currentScene).stem().string() == "PillarTest"; }

	// Begin a fresh GI prime: wipe the SH buffer so the sweep is deterministic
	// (no history blended in -- fixes "re-prime gives a different result each time").
	void StartGiPrime()
	{
		giCursor = 0;
		giPriming = true;
		giDynamicDirtyMask.clear();
		giPreviousEmissivePositions.clear();
		if (deferred && deferred->HasGi()) deferred->ClearGi();
	}

	// (Re)load everything that depends on the scene: instances, sub-mesh split,
	// bounds, the light rig, and the start camera. Safe to call at runtime to
	// switch scenes (free-fly). aEnv = honour BENCH_* overrides (Init only).
	bool LoadSceneContent(const std::string& sceneName, bool aEnv)
	{
		// Every mesh/texture upload below shares GPU submissions.
		struct UploadBatch
		{
			UploadBatch() { if (rhi::IDevice* d = DX11::Rhi()) d->BeginUploadBatch(); }
			~UploadBatch() { if (rhi::IDevice* d = DX11::Rhi()) d->EndUploadBatch(); }
		} uploadBatch;
		previousRayTransforms.clear();
		if (deferred) deferred->ResetTemporalHistory();
		if (sceneName != currentScene) {
			sunPitch = 55.f; sunYaw = -35.f; sunIlluminanceLux = 100000.f; sunUseTemperature = false;
			sunColor[0] = 1.f; sunColor[1] = 0.96f; sunColor[2] = 0.88f;
			ambientColor[0] = 0.35f; ambientColor[1] = 0.42f; ambientColor[2] = 0.55f;
		}
		currentScene = sceneName;
		std::ifstream lightingFile(fs::path(Settings::GameAssetRoot()) / fs::path(sceneName).replace_extension(".tgs"));
		if (lightingFile) {
			try {
				json document; lightingFile >> document;
				if (document.contains("lighting")) {
					const auto& lighting = document["lighting"];
					sunPitch = lighting.value("sunPitch", sunPitch);
					sunYaw = lighting.value("sunYaw", sunYaw);
					sunIlluminanceLux = lighting.value("sunIlluminance", lighting.value("sunIntensity", SunIntensity()) * 100000.0f);
					if (lighting.contains("sunTemperature"))
					{
						sunTemperatureK = lighting["sunTemperature"].get<float>();
						sunUseTemperature = true;
					}
					for (int i = 0; i < 3; ++i) {
						if (lighting.contains("sunColor") && lighting["sunColor"].size() == 3) sunColor[i] = lighting["sunColor"][i].get<float>();
						if (lighting.contains("ambientColor") && lighting["ambientColor"].size() == 3) ambientColor[i] = lighting["ambientColor"][i].get<float>();
					}
				}
			} catch (const std::exception& e) { ERROR_PRINT("Scene lighting: %s", e.what()); }
		}

		models.clear();
		opaqueMeshes.clear();
		transparentMeshes.clear();
		instanceOffsets.clear();
		pointLights.clear();
		lightExtra.clear();
		anyTransparent = false;

		ModelFactory& mf = ModelFactory::GetInstance();
		auto& texMgr = GraphicsEngine::GetInstance()->GetTextureManager();

		std::vector<SceneEntry> entries;
		if (!sceneName.empty())
		{
			entries = LoadTgs(sceneName);
			INFO_PRINT("bench: scene '%s' -> %zu object(s)", sceneName.c_str(), entries.size());
		}
		if (entries.empty())
		{
			ERROR_PRINT("bench: scene '%s' contains no loadable objects", sceneName.c_str());
			return false;
		}

		// Decode every texture the scene's materials reference in parallel
		// before the (main-thread) per-instance texture assignment below.
		{
			std::vector<std::string> texturePaths;
			for (const SceneEntry& e : entries)
				for (const std::string& materialFile : e.materials)
				{
					MaterialDef material;
					if (materialFile.empty() || !LoadTgmat(fs::path(Settings::GameAssetRoot()) / materialFile, material)) continue;
					for (const std::string& map : material.maps)
						if (!map.empty()) texturePaths.push_back(map);
				}
			texMgr.PrefetchTextures(texturePaths);
		}
		struct PrefetchCleanup { TextureManager& t; ~PrefetchCleanup() { t.ClearPrefetchedTextures(); } } prefetchCleanup{ texMgr };

		const auto tLoad0 = std::chrono::high_resolution_clock::now();
		const int side = (int)std::ceil(std::sqrt((double)sponzaCopies));
		const bool tileCopies = (entries.size() == 1);

		for (const SceneEntry& e : entries)
		{
			std::shared_ptr<Model> model = mf.GetModel(e.fbx.c_str());
			if (!model) { ERROR_PRINT("bench: failed to load '%s'", e.fbx.c_str()); continue; }
			const int meshCount = std::min((int)model->GetMeshCount(), MAX_MESHES_PER_MODEL);
			// Surface type is authored per scene material.  Keep the legacy name
			// override as a fallback for old scenes, but never let it be the only
			// route by which a .tgmat becomes transparent in raster or DXR.
			std::vector<bool> authoredTransparent(meshCount, false);
			// Masked (real alpha cutout, e.g. foliage/fences) is distinct from
			// Opaque so AcceptRayTriangle can skip the texture sample entirely
			// for ordinary opaque geometry -- see FixedMaterial::kRayOpaque.
			std::vector<bool> authoredMasked(meshCount, false);
			for (int m = 0; m < meshCount && m < (int)e.materials.size(); ++m)
			{
				if (e.materials[m].empty()) continue;
				MaterialDef material;
				if (!LoadTgmat(fs::path(Settings::GameAssetRoot()) / e.materials[m], material)) continue;
				const std::string st = LowerStr(material.surfaceType);
				authoredTransparent[m] = st == "transparent";
				authoredMasked[m] = st == "masked";
			}

			const int copies = tileCopies ? sponzaCopies : 1;
			const float sizeXZ0 = std::max(sceneExtents.x, sceneExtents.z) * 2.f;
			const float step = sizeXZ0 * 1.15f;

			for (int i = 0; i < copies; ++i)
			{
				ModelInstance mi;
				mi.Init(model);

				for (int m = 0; m < meshCount && m < (int)e.materials.size(); ++m)
				{
					if (e.materials[m].empty()) continue;
					MaterialDef material;
					const fs::path materialPath = fs::path(Settings::GameAssetRoot()) / e.materials[m];
					if (!LoadTgmat(materialPath, material)) continue;
					for (int j = 0; j < 4; ++j)
					{
						if (material.maps[j].empty()) continue;
						const TextureSrgbMode sm = (j == 0) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
						if (Tga::Texture* t = texMgr.GetTexture(material.maps[j].c_str(), sm)) mi.SetTexture(m, j, t);
					}
				}

				const int gx = i % side, gz = i / side;
				const float ox = tileCopies ? (gx - (side - 1) * 0.5f) * step : 0.f;
				const float oz = tileCopies ? (gz - (side - 1) * 0.5f) * step : 0.f;
				Matrix4x4f xf = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ modelRotX, 0.f, 0.f }) * e.transform;
				xf.SetPosition(xf.GetPosition() + Vector3f{ ox, 0.f, oz });
				mi.SetTransform(xf);
				models.push_back(mi);
				instanceOffsets.push_back(Vector3f{ ox, 0.f, oz });

				std::vector<int> op, tr;
				for (int m = 0; m < meshCount; ++m)
				{
					const char* mat = model->GetMaterialName(m).GetString();
					const bool transparent = authoredTransparent[m] || MatchesAny(mat ? mat : "", transparentMatKeys);
					// The same classification controls the raster forward pass and
					// the material record consulted by every inline RayQuery.  Forward
					// alpha blend cannot provide a reliable hit distance/transmittance,
					// so let it composite after DXR instead of treating glass as opaque.
					using FM = RayTracingMaterialTable::FixedMaterial;
					const uint32_t rayVisibility = transparent ? FM::kRayTransparent
						: authoredMasked[m] ? FM::kRayMasked : FM::kRayOpaque;
					RayTracingMaterialTable::SetRayVisibility(model->GetMeshData(m).rayGeometry.materialIndex, rayVisibility);
					(transparent ? tr : op).push_back(m);
				}
				if (!tr.empty()) anyTransparent = true;
				opaqueMeshes.push_back(std::move(op));
				transparentMeshes.push_back(std::move(tr));
			}
		}
		modelLoadMs = std::chrono::duration<double, std::milli>(
			std::chrono::high_resolution_clock::now() - tLoad0).count();
		if (models.empty()) { ERROR_PRINT("bench: no instances created"); return false; }

		// Scene bounds = union of every instance's world-space AABB.
		{
			Vector3f mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
			for (const ModelInstance& mi : models)
			{
				if (!mi.GetModel()) continue;
				const Tga::BoxSphereBounds& b = mi.GetModel()->GetBounds();
				const Matrix4x4f& w = mi.GetTransform();
				for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				for (int sz = -1; sz <= 1; sz += 2)
				{
					Vector4f c(b.center.x + sx * b.boxExtents.x, b.center.y + sy * b.boxExtents.y, b.center.z + sz * b.boxExtents.z, 1.f);
					Vector4f p = c * w;
					mn = { std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z) };
					mx = { std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z) };
				}
			}
			if (mn.x <= mx.x)
			{
				sceneCenter = (mn + mx) * 0.5f;
				sceneExtents = (mx - mn) * 0.5f;
			}
		}

		// Material-preview debug sphere (Source/Game/data/Primitives/Sphere.fbx).
		if (!debugBallValid)
		{
			if (std::shared_ptr<Model> sph = mf.GetModel("Primitives/Sphere.fbx"))
			{
				debugBall.Init(sph);
				debugBallValid = true;
				debugBallModelRadius = std::max(sph->GetBounds().radius, 0.001f);
				orbitBalls.clear();
				orbitBalls.resize(kMaxOrbitBalls);
				for (ModelInstance& mi : orbitBalls) mi.Init(sph);
			}
			else ERROR_PRINT("material preview: Primitives/Sphere.fbx not found");
		}
		debugBallPos = sceneCenter - Vector3f{ 0.f, sceneExtents.y * 0.2f, 0.f };

		// Auto-size the orbit path / sphere radius to whatever scene just loaded.
		orbitPathRadius = std::max(sceneExtents.x, sceneExtents.z) * 0.55f;
		orbitBallRadius = std::clamp(orbitPathRadius * 0.10f, 8.f, 400.f);
		orbitHeight = 0.f;

		const float sizeXZ = std::max(sceneExtents.x, sceneExtents.z) * 2.f;
		orbitRadius = std::clamp(std::max(sceneExtents.x, sceneExtents.z) * 0.42f, 150.f, 6000.f);
		if (aEnv) if (const char* r = std::getenv("BENCH_ORBIT")) orbitRadius = std::max(1.f, (float)atof(r));
		flySpeed = std::clamp(sizeXZ * 0.35f, 400.f, 6000.f);
		camPos   = sceneCenter + Vector3f{ 0, sceneExtents.y * 0.1f, -orbitRadius };

		const float sceneRadius = std::sqrt(sceneExtents.x * sceneExtents.x
			+ sceneExtents.y * sceneExtents.y + sceneExtents.z * sceneExtents.z);
		float exposure = 1.0f;
		if (aEnv) if (const char* e = std::getenv("BENCH_EXPOSURE")) exposure = std::max(0.01f, (float)atof(e));

		const fs::path tgsPath = fs::path(Settings::GameAssetRoot()) / fs::path(sceneName).replace_extension(".tgs");
		LoadLights(tgsPath.string(), sceneRadius, exposure);

		// Start camera: the scene's placed camera file if present, else orbit-derived.
		const std::string defCamFile = sceneName.empty() ? std::string("bench_camera.json")
		                                                 : ("bench_camera_" + sceneName + ".json");
		camFile = aEnv ? EnvStr("BENCH_CAMFILE", defCamFile.c_str()) : defCamFile;
		if (aEnv) if (const char* sp = std::getenv("BENCH_SPIN")) camSpinDeg = (float)atof(sp);
		camLoaded = false;
		if (LoadCamera())
		{
			if (aEnv)
			{
				const std::string mode = EnvStr("BENCH_CAM", sceneName.empty() ? "spin" : "fixed");
				if      (mode == "fixed") camMode = CamMode::Fixed;
				else if (mode == "orbit") camMode = CamMode::Orbit;
				else if (mode == "room")  { camMode = CamMode::Orbit; orbitRoom = true; }
				else                      camMode = CamMode::Spin;
			}
			INFO_PRINT("bench: camera '%s'", camFile.c_str());
		}
		camera.GetTransform().SetPosition(camPos);
		camera.GetTransform().SetRotation(camRot);
		return true;
	}

	void WriteReport()
	{
		if (reportWritten) return;
		reportWritten = true;

		auto stats = [](std::vector<double> v)
		{
			std::sort(v.begin(), v.end());
			const size_t n = v.size();
			double sum = std::accumulate(v.begin(), v.end(), 0.0);
			double mean = n ? sum / n : 0.0;
			double var = 0.0; for (double x : v) var += (x - mean) * (x - mean);
			var = n ? var / n : 0.0;
			auto pct = [&](double p) { return n ? v[std::min(n - 1, (size_t)(p * (n - 1) + 0.5))] : 0.0; };
			return std::tuple<double, double, double, double, double, double, double, double>(
				mean, std::sqrt(var), n ? v.front() : 0.0, n ? v.back() : 0.0,
				pct(0.01), pct(0.50), pct(0.95), pct(0.99));
		};

		auto [fMean, fStd, fMin, fMax, fP1, fP50, fP95, fP99] = stats(frameMs);
		auto [cMean, cStd, cMin, cMax, cP1, cP50, cP95, cP99] = stats(cpuMs);
		double dcMean = 0.0;
		if (!drawCalls.empty())
			dcMean = std::accumulate(drawCalls.begin(), drawCalls.end(), 0.0) / drawCalls.size();
		double visMean = 0.0;
		if (!visibleInstances.empty())
			visMean = std::accumulate(visibleInstances.begin(), visibleInstances.end(), 0.0) / visibleInstances.size();

		const Vector2ui res = Application::GetInstance()->GetRenderSize();
		size_t subMeshes = 0;
		for (auto& m : models) if (m.GetModel()) subMeshes += m.GetModel()->GetMeshCount();

		std::ofstream o(reportPath);
		o.setf(std::ios::fixed); o.precision(4);
		o << "{\n";
		o << "  \"scene\": \"" << currentScene << "\",\n";
		o << "  \"renderer\": \"" << (useDeferred ? "deferred" : "forward") << "\",\n";
		o << "  \"model_load_ms\": " << modelLoadMs << ",\n";
		o << "  \"first_frame_ms\": " << firstFrameMs << ",\n";
		o << "  \"resolution\": [" << res.x << ", " << res.y << "],\n";
		o << "  \"vsync\": " << (Settings::GetApplicationConfiguration().enableVSync ? "true" : "false") << ",\n";
		o << "  \"sponza_copies\": " << sponzaCopies << ",\n";
		o << "  \"model_instances\": " << models.size() << ",\n";
		o << "  \"frustum_cull\": " << (frustumCull ? "true" : "false") << ",\n";
		o << "  \"visible_instances_mean\": " << visMean << ",\n";
		o << "  \"sub_meshes_total\": " << subMeshes << ",\n";
		o << "  \"point_lights\": " << pointLights.size() << ",\n";
		o << "  \"frames_measured\": " << frameMs.size() << ",\n";
		o << "  \"warmup_frames\": " << warmupFrames << ",\n";
		o << "  \"frame_ms\":  { \"mean\": " << fMean << ", \"std\": " << fStd
		  << ", \"min\": " << fMin << ", \"max\": " << fMax
		  << ", \"p1\": " << fP1 << ", \"p50\": " << fP50 << ", \"p95\": " << fP95 << ", \"p99\": " << fP99 << " },\n";
		o << "  \"cpu_ms\":    { \"mean\": " << cMean << ", \"std\": " << cStd
		  << ", \"min\": " << cMin << ", \"max\": " << cMax
		  << ", \"p1\": " << cP1 << ", \"p50\": " << cP50 << ", \"p95\": " << cP95 << ", \"p99\": " << cP99 << " },\n";
		o << "  \"fps_mean\": " << (fMean > 0 ? 1000.0 / fMean : 0.0) << ",\n";
		o << "  \"draw_calls_mean\": " << dcMean << ",\n";

		double gpuFrame = 0.0;
		if (!gpuFrameMs.empty())
			gpuFrame = std::accumulate(gpuFrameMs.begin(), gpuFrameMs.end(), 0.0) / gpuFrameMs.size();
		o << "  \"gpu_ms\": { \"frame\": " << gpuFrame;
		for (const auto& [name, samples] : gpuScopeMs)
		{
			double m = samples.empty() ? 0.0
				: std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
			o << ", \"" << name << "\": " << m;
		}
		o << " },\n";
		if (!costResults.empty())
		{
			o << "  \"feature_costs_ms\": { \"baseline\": " << costBaselineMs;
			for (const CostResult& r : costResults)
				if (!r.skipped) o << ", \"" << r.name << "\": [" << r.deltaMs << ", " << r.spreadMs << "]";
			o << " },\n";
		}
		o << "  \"cpu_scopes_ms\": {";
		{
			bool first = true;
			for (const CpuProfiler::ScopeStats* st : CpuProfiler::Get().GetStats())
			{
				o << (first ? " " : ", ") << "\"" << std::string(st->depth * 2, ' ') << st->name << "\": " << st->Average();
				first = false;
			}
		}
		o << " }\n";
		o << "}\n";
		o.close();

		INFO_PRINT("=== Sponza bench: %zu frames | frame %.3f ms (%.1f fps) | cpu %.3f ms | %.0f draw calls | report -> %s",
			frameMs.size(), fMean, fMean > 0 ? 1000.0 / fMean : 0.0, cMean, dcMean, reportPath.c_str());
	}

	void BuildCostProbes()
	{
		costProbes.clear();
		if (!deferred) return;
		DeferredRenderer::Tunables* t = &deferred->GetTunables();
		auto flag = [&](const char* name, bool* value)
		{
			const bool original = *value;
			costProbes.push_back({ name, [=]() { return original; }, [=](bool on) { *value = on ? original : false; } });
		};
		auto reduce = [&](const char* name, int* value, int reduced)
		{
			const int original = *value;
			costProbes.push_back({ name, [=]() { return original > reduced; }, [=](bool on) { *value = on ? original : reduced; } });
		};
		flag("Reflections", &t->dxrReflections);
		reduce("Reflection samples -> 1", &t->dxrReflectionSamples, 1);
		flag("Ambient occlusion", &t->dxrAmbientOcclusion);
		reduce("AO samples -> 1", &t->dxrAoSamples, 1);
		flag("Direct light + shadows", &t->dxrDirectLighting);
		flag("Indirect GI lookup", &t->dxrIndirectGi);
		flag("Environment light", &t->dxrEnvironmentLighting);
		flag("Texture filtering (ray cones)", &t->dxrTextureFiltering);
		flag("Specular AA", &t->specularAaEnabled);
		flag("NRD denoiser", &t->nrdEnabled);
		if (t->nrdEnabled)
		{
			const bool original = t->nrdCheckerboard;
			costProbes.push_back({ "Full-res AO + reflections (vs checkerboard)", [=]() { return !original; },
				[=](bool on) { t->nrdCheckerboard = on ? original : true; } });
		}
		reduce("Sun shadow rays -> 1", &t->dxrSunShadowSamples, 1);
		{
			const int original = t->volumetricResolution;
			costProbes.push_back({ "Fog volume at quarter res", [=]() { return original < 2; },
				[=](bool on) { t->volumetricResolution = on ? original : 2; } });
		}
		flag("TAA", &t->taaEnabled);
		flag("DLAA", &t->dlaaEnabled);
		flag("Fog", &t->fogEnabled);
		flag("Volumetric sunlight", &t->volumetricEnabled);
		flag("Bloom", &t->bloomEnabled);
		flag("GI probe updates", &giKeepUpdating);
		reduce("GI rays per probe -> 32", &giRTRayCount, 32);
	}

	void StartCostSweep()
	{
		BuildCostProbes();
		costResults.clear();
		costSamples.clear();
		costOnMs.clear();
		costProbe = -1;
		costFrame = 0;
		costPhaseOff = false;
		AdvanceCostProbe();
	}

	void StopCostSweep()
	{
		if (costProbe >= 0 && costProbe < (int)costProbes.size()) costProbes[costProbe].set(true);
		costProbe = -2;
	}

	static float Percentile(std::vector<float> v, float q)
	{
		if (v.empty()) return 0.f;
		std::sort(v.begin(), v.end());
		return v[std::min(v.size() - 1, size_t(q * float(v.size())))];
	}
	static float Median(std::vector<float> v) { return Percentile(std::move(v), 0.5f); }

	// Moves to the next probe with something to turn off; skipped ones are
	// recorded as such.
	void AdvanceCostProbe()
	{
		for (++costProbe; costProbe < (int)costProbes.size(); ++costProbe)
		{
			if (costProbes[costProbe].isOn()) { costPhaseOff = false; costCycle = 0; costDeltas.clear(); return; }
			costResults.push_back({ costProbes[costProbe].name, 0.f, 0.f, true });
		}
		costProbe = -2;
		if (!costOnMs.empty()) costBaselineMs = Median(costOnMs);
		std::stable_sort(costResults.begin(), costResults.end(),
			[](const CostResult& a, const CostResult& b) { return a.deltaMs > b.deltaMs; });
	}

	// Each probe measures "on" and then "off" back to back, so slow drift
	// (thermals, background load) cancels out of the difference.
	void StepCostSweep()
	{
		if (costProbe < 0 || !gpu.IsReady()) return;
		if (++costFrame > kCostWarmupFrames) costSamples.push_back(float(gpu.GetFrameGpuMs()));
		if (costFrame < kCostWarmupFrames + kCostMeasureFrames) return;

		// Lower quartile: the steady-state frame, ignoring intermittent spikes
		// such as GI probe batches or shader/PSO hitches.
		const float ms = Percentile(costSamples, 0.25f);
		costSamples.clear();
		costFrame = 0;
		CostProbe& probe = costProbes[costProbe];
		if (!costPhaseOff)
		{
			costCurrentOnMs = ms;
			costOnMs.push_back(ms);
			probe.set(false);
			costPhaseOff = true;
			return;
		}
		probe.set(true);
		costPhaseOff = false;
		costDeltas.push_back(costCurrentOnMs - ms);
		costCurrentOffMs = ms;
		if (++costCycle < kCostCycles) return;
		const auto [lo, hi] = std::minmax_element(costDeltas.begin(), costDeltas.end());
		costResults.push_back({ probe.name, costCurrentOffMs, Median(costDeltas), false, (*hi - *lo) * 0.5f });
		AdvanceCostProbe();
	}

#ifndef _RETAIL
	static const char* ScopeName(const char* n) { return n ? n : "?"; }
	static const char* ScopeName(const std::string& n) { return n.c_str(); }

	template <class Stats>
	static void DrawScopeTable(const char* id, const std::vector<const Stats*>& stats, float frameMs, std::string& report)
	{
		if (!ImGui::BeginTable(id, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
			return;
		ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.8f);
		ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		ImGui::TableHeadersRow();
		for (const Stats* st : stats)
		{
			const float avg = st->Average();
			const float frac = frameMs > 0.f ? std::clamp(avg / frameMs, 0.f, 1.f) : 0.f;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Indent(float(st->depth) * 12.f + 0.01f);
			ImGui::TextUnformatted(ScopeName(st->name));
			ImGui::Unindent(float(st->depth) * 12.f + 0.01f);
			ImGui::TableNextColumn(); ImGui::Text("%.3f", st->Last());
			ImGui::TableNextColumn();
			const ImVec4 hot = avg > 2.0f ? ImVec4(1, .45f, .35f, 1) : avg > 0.5f ? ImVec4(1, .85f, .4f, 1) : ImVec4(.85f, .85f, .85f, 1);
			ImGui::TextColored(hot, "%.3f", avg);
			ImGui::TableNextColumn(); ImGui::Text("%.3f", st->Max());
			ImGui::TableNextColumn();
			char overlay[16];
			std::snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.f);
			ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);

			char line[256];
			std::snprintf(line, sizeof(line), "%*s%-40s last %8.3f  avg %8.3f  max %8.3f ms\n",
				st->depth * 2, "", ScopeName(st->name), st->Last(), avg, st->Max());
			report += line;
		}
		ImGui::EndTable();
	}

	static void DrawHistory(const char* label, const float* history, int offset, int count, float target)
	{
		if (count <= 0) return;
		float peak = target;
		for (int i = 0; i < count; ++i) peak = std::max(peak, history[i]);
		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "%s (max %.2f ms)", label, peak);
		ImGui::PlotLines("##history", history, count, count == CpuProfiler::kHistory ? offset : 0, overlay, 0.f, peak * 1.1f, ImVec2(-FLT_MIN, 64));
	}

	void DrawProfilerTab()
	{
		std::string report;
		CpuProfiler& cpu = CpuProfiler::Get();
		auto historyAverage = [](const float* h, int count)
		{
			double sum = 0.0;
			for (int i = 0; i < count; ++i) sum += h[i];
			return count ? float(sum / count) : 0.f;
		};
		// The frame scope includes the time the CPU sits blocked on the GPU;
		// report CPU work without it.
		float gpuWaitMs = 0.f;
		for (const CpuProfiler::ScopeStats* st : cpu.GetStats())
			if (st->depth == 0 && std::string_view(st->name) == "Device begin frame (GPU wait)") gpuWaitMs = st->Average();
		const float cpuFrameMs = historyAverage(cpu.GetFrameHistory(), cpu.GetFrameHistoryCount());
		const float cpuWorkMs = std::max(cpuFrameMs - gpuWaitMs, 0.f);
		const float gpuMs = historyAverage(gpu.GetFrameHistory(), gpu.GetFrameHistoryCount());
		ImGui::Text("Frame %.2f ms (%.0f fps)   CPU work %.2f ms   GPU %.2f ms   (averages)",
			ImGui::GetIO().DeltaTime * 1000.f, ImGui::GetIO().Framerate, cpuWorkMs, gpuMs);
		{
			char line[160];
			std::snprintf(line, sizeof(line), "Frame %.2f ms | CPU %.2f ms | GPU %.2f ms\n", ImGui::GetIO().DeltaTime * 1000.f, cpuWorkMs, gpuMs);
			report += line;
		}
		if (rhi::IDevice* dev = DX11::Rhi())
		{
			uint64_t usage = 0, budget = 0;
			if (dev->QueryVideoMemory(usage, budget))
			{
				const float frac = budget ? float(double(usage) / double(budget)) : 0.f;
				char overlay[64];
				std::snprintf(overlay, sizeof(overlay), "VRAM %.2f / %.2f GB", usage / 1073741824.0, budget / 1073741824.0);
				ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
				if (frac > 0.9f) ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "Near the VRAM budget: expect paging stalls.");
				report += std::string(overlay) + "\n";
			}
		}
		if (deferred)
		{
			const DeferredRenderer::Tunables& t = deferred->GetTunables();
			ImGui::TextDisabled("%s | NRD %s | DLSS mode %d | DLAA %s | RR %s | reflections %d spp | AO %d spp | GI %d rays/probe",
				dxrRenderer ? "DXR" : "Raster", t.nrdEnabled ? "on" : "off", t.dlssMode, t.dlaaEnabled ? "on" : "off",
				t.rayReconstructionEnabled ? "on" : "off", t.dxrReflectionSamples, t.dxrAoSamples, giRTRayCount);
		}

		DrawHistory("CPU work", cpu.GetFrameHistory(), cpu.GetFrameHistoryOffset(), cpu.GetFrameHistoryCount(), 8.33f);
		DrawHistory("GPU", gpu.GetFrameHistory(), gpu.GetFrameHistoryOffset(), gpu.GetFrameHistoryCount(), 8.33f);
		bool enabled = cpu.IsEnabled();
		if (ImGui::Checkbox("Collect CPU scopes", &enabled)) cpu.SetEnabled(enabled);
		ImGui::SameLine();
		if (ImGui::Button("Reset stats")) { cpu.ResetStats(); gpu.ResetStats(); }
		ImGui::SameLine();
		const bool copy = ImGui::Button("Copy report");

		if (ImGui::CollapsingHeader("GPU scopes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			report += "-- GPU scopes --\n";
			DrawScopeTable("gpuScopes", gpu.GetStats(), gpuMs, report);
			ImGui::TextDisabled("\"Ray trace + shade\" is one dispatch; use the feature cost sweep to split it.");
		}
		if (ImGui::CollapsingHeader("CPU scopes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			report += "-- CPU scopes --\n";
			DrawScopeTable("cpuScopes", cpu.GetStats(), cpuFrameMs, report);
			ImGui::TextDisabled("\"Device begin frame (GPU wait)\" is the CPU blocked on the GPU, not CPU work.");
		}
		if (ImGui::CollapsingHeader("Feature cost sweep", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("Toggles each enabled feature off and on %d times, lets the image settle, and compares steady-state GPU frame times. "
				"Keep the camera still while it runs (about %d s per feature at 60 fps).", kCostCycles, 2 * kCostCycles * (kCostWarmupFrames + kCostMeasureFrames) / 60);
			if (costProbe < 0)
			{
				if (ImGui::Button("Measure feature costs")) StartCostSweep();
			}
			else
			{
				const float window = float(costFrame) / float(kCostWarmupFrames + kCostMeasureFrames);
				const float phase = (float(costCycle) + (costPhaseOff ? 0.5f : 0.f) + 0.5f * window) / float(kCostCycles);
				const float progress = (float(costProbe) + phase) / float(std::max<size_t>(costProbes.size(), 1));
				char overlay[96];
				std::snprintf(overlay, sizeof(overlay), "%s (%s)", costProbes[costProbe].name, costPhaseOff ? "off" : "on");
				ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0), overlay);
				if (ImGui::Button("Stop")) StopCostSweep();
			}
			if (!costResults.empty() && ImGui::BeginTable("costs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
			{
				report += "-- Feature costs (baseline " + std::to_string(costBaselineMs) + " ms) --\n";
				ImGui::TableSetupColumn("Feature (turned off)");
				ImGui::TableSetupColumn("GPU ms without");
				ImGui::TableSetupColumn("Cost");
				ImGui::TableSetupColumn("+/- spread");
				ImGui::TableHeadersRow();
				for (const CostResult& r : costResults)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name);
					if (r.skipped)
					{
						ImGui::TableNextColumn(); ImGui::TextDisabled("already off");
						ImGui::TableNextColumn();
						ImGui::TableNextColumn();
						continue;
					}
					ImGui::TableNextColumn(); ImGui::Text("%.2f", r.frameMs);
					ImGui::TableNextColumn();
					const ImVec4 hot = r.deltaMs > 2.f ? ImVec4(1, .45f, .35f, 1) : r.deltaMs > 0.5f ? ImVec4(1, .85f, .4f, 1) : ImVec4(.7f, .9f, .7f, 1);
					const bool withinNoise = std::abs(r.deltaMs) <= r.spreadMs;
					ImGui::TextColored(withinNoise ? ImVec4(.6f, .6f, .6f, 1) : hot, "%+.2f ms", r.deltaMs);
					ImGui::TableNextColumn(); ImGui::TextDisabled("%.2f", r.spreadMs);
					char line[128];
					std::snprintf(line, sizeof(line), "%-34s %+7.2f ms  (+/- %.2f)\n", r.name, r.deltaMs, r.spreadMs);
					report += line;
				}
				ImGui::EndTable();
				ImGui::TextDisabled("Frame with everything on: %.2f ms. Grey costs are within the measurement spread. Costs overlap (reflections include their own GI lookups), so they need not sum to it.", costBaselineMs);
			}
		}
		if (ImGui::CollapsingHeader("Startup / load profile"))
		{
			report += "-- Load profile --\n";
			for (const CpuProfiler::LoadEntry& e : cpu.GetLoadReport())
			{
				if (e.ms < 1.0) continue;
				if (e.calls > 1) ImGui::Text("%*s%-40s %9.1f ms (%d calls)", e.depth * 2, "", e.name, e.ms, e.calls);
				else ImGui::Text("%*s%-40s %9.1f ms", e.depth * 2, "", e.name, e.ms);
				char line[160];
				std::snprintf(line, sizeof(line), "%*s%-40s %9.1f ms (%d)\n", e.depth * 2, "", e.name, e.ms, e.calls);
				report += line;
			}
		}
		if (copy) ImGui::SetClipboardText(report.c_str());
	}
#endif

	// Always-on GPU/CPU timing HUD (free-fly only). Reads the per-pass scopes the
	// RenderGraph pushes into the profiler.
	void DrawPerfOverlayImpl()
	{
#ifndef _RETAIL
		if (!showPerfOverlay) return;
		ImGui::SetNextWindowPos({ 8.f, 8.f }, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
			| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("##perf", nullptr, f))
		{
			const float dtMs = Application::GetInstance()->GetDeltaTime() * 1000.f;
			ImGui::Text("CPU  %6.2f ms   %4.0f fps", dtMs, dtMs > 0.f ? 1000.f / dtMs : 0.f);
			if (gpu.IsReady())
			{
				ImGui::Text("GPU  %6.2f ms", gpu.GetFrameGpuMs());
				ImGui::Separator();
				for (const GpuProfiler::ScopeResult& r : gpu.GetResults())
					ImGui::Text("%*s%-13s %6.3f", r.depth * 2, "", r.name.c_str(), r.ms);
			}
			else ImGui::TextDisabled("GPU  (warming up)");
		}
		ImGui::End();
#endif
	}

	// Render the scene into a cubemap at probePos, GGX-prefilter it, and point the
	// ambient IBL at the result. Uses fallbackCube for its own sky/IBL so it does
	// not feed back on itself.
	void CaptureProbeImpl(GraphicsEngine& ge)
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

	// Capture `giProbesPerFrame` GI probes (cycling `giCursor`), project each into
	// the deferred renderer's SH volume. Same tiny forward capture as the reflection
	// probe; multi-bounce comes for free because DrawPbr already samples the volume.
	void CaptureGiProbesImpl(GraphicsEngine& ge)
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

		const bool primingNow = giPriming;   // fixed for the whole batch (deterministic replace)
		const bool lightingRefresh = !primingNow && giLightingRefreshProbeBudget > 0;
		const bool dynamicEmissiveGi = showOrbitBalls && debugBallValid && orbitBallsEmitLight
			&& debugMat.emissiveStrength > 0.01f;
		// This is a coarse, world-space SH volume (200-unit cells by default),
		// not a surface cache. Writing a fast-moving local emitter into it creates
		// broad trilinear lobes that cannot move smoothly. Dynamic emissive proxy
		// lights are evaluated directly by the DXR camera pass; keep this cache
		// for stable indirect/environment transport until it has a proper
		// surface-cache representation.
		const bool dynamicCacheUpdate = false;
		// A probe trace shades every hit and casts its own shadow rays. Updating
		// four 64-ray probes every frame was a full secondary DXR workload, yet
		// its changing low-frequency result was still only an approximation. Keep
		// dynamic GI amortized: one probe closest to the emitters, at a reduced
		// ray budget, while the ordinary round-robin path remains available for
		// explicitly requested continuous GI updates.
		// Raster capture is six scene draws/probe, so retain its conservative
		// one-probe cadence.  Inline DXR can afford a small bounded batch every
		// frame, which makes emissive/direct changes start propagating immediately.
		// Maintain an approximately constant trace budget when a low ray count
		// is selected, so each probe gets fresh temporal samples quickly.
		const int autoBatch = std::clamp(32768 / std::max(giRTRayCount, 1), 1, 256);
		const int rtBatch = giProbeBudget > 0 ? std::min(giProbeBudget, 256) : autoBatch;
		const int batch = giPriming ? (useRT ? std::max(giPrimeBatch, rtBatch) : giPrimeBatch)
			: (useRT ? std::max(giTrickle, rtBatch) : 1);
		std::vector<int> prioritizedProbes;
		std::vector<DeferredRenderer::GiProbeBatchEntry> rtBatchEntries;
		if (useRT) rtBatchEntries.reserve((size_t)std::max(batch, 1));
		if (dynamicCacheUpdate && !primingNow)
		{
			std::vector<Vector3f> emitters;
			if (dynamicEmissiveGi)
			{
				const int emitterCount = std::clamp(orbitBallCount, 1, kMaxOrbitBalls);
				emitters.reserve(emitterCount);
				for (int i = 0; i < emitterCount && i < (int)orbitBalls.size(); ++i)
					emitters.push_back(orbitBalls[i].GetTransform().GetPosition());
			}

			// Probes sample a trilinear volume, so both the old and new emitter
			// neighbourhoods must be refreshed. Only prioritizing the new nearest
			// probe left cached radiance at the old location, which was the source
			// of the visible lighting trails/boiling.
			if ((dynamicEmissiveGi || !giPreviousEmissivePositions.empty()) && giDynamicDirtyMask.size() != (size_t)total)
				giDynamicDirtyMask.assign(total, 0);
			const float influence = std::max({ giSpacing.x, giSpacing.y, giSpacing.z }) * 1.75f + orbitBallRadius;
			const float influenceSq = influence * influence;
			for (int p = 0; p < total; ++p)
			{
				const int px = p % giCx;
				const int py = (p / giCx) % giCy;
				const int pz = p / (giCx * giCy);
				const Vector3f candidateProbePos = giOrigin + Vector3f{ px * giSpacing.x, py * giSpacing.y, pz * giSpacing.z };
				float nearestDistanceSq = std::numeric_limits<float>::max();
				for (const Vector3f& emitter : emitters)
					nearestDistanceSq = std::min(nearestDistanceSq, (candidateProbePos - emitter).LengthSqr());
				for (const Vector3f& previousEmitter : giPreviousEmissivePositions)
					nearestDistanceSq = std::min(nearestDistanceSq, (candidateProbePos - previousEmitter).LengthSqr());
				if (nearestDistanceSq <= influenceSq)
					giDynamicDirtyMask[p] = 1;
			}
			if (dynamicEmissiveGi)
				giPreviousEmissivePositions = std::move(emitters);
			else
				giPreviousEmissivePositions.clear(); // retain the mask until it is drained

			for (int attempt = 0; attempt < total; ++attempt)
			{
				const int candidate = (int)(giDynamicProbeCursor++ % (uint32_t)total);
				if (giDynamicDirtyMask[candidate])
				{
					giDynamicDirtyMask[candidate] = 0;
					prioritizedProbes.push_back(candidate);
					break;
				}
			}
		}
		for (int n = 0; n < batch; ++n)
		{
			int p = 0;
			if (n < (int)prioritizedProbes.size())
			{
				p = prioritizedProbes[n];
			}
			else
			{
				// Keep one round-robin update in the dynamic batch so distant probes
				// retain a valid background solution while nearby probes follow emitters.
				// Bound the search so a deliberately tiny debug volume cannot loop
				// forever when every probe is part of the prioritized set.
				int attempts = 0;
				do
				{
					p = giCursor;
					giCursor = (giCursor + 1) % total;
					++attempts;
				} while (attempts < total && std::find(prioritizedProbes.begin(), prioritizedProbes.end(), p) != prioritizedProbes.end());
				if (giCursor == 0) giPriming = false;
			}

			const int px = p % giCx;
			const int py = (p / giCx) % giCy;
			const int pz = p / (giCx * giCy);
			const Vector3f pos = giOrigin + Vector3f{ px * giSpacing.x, py * giSpacing.y, pz * giSpacing.z };

			if (useRT)
			{
				// No cubemap, no per-face scene redraw -- just ray-trace straight
				// from the probe position. Priming still replaces deterministically;
				// the live trickle still blends, same semantics as the raster path.
				// Dirty dynamic-cache cells discard most of their old solution when
				// actually refreshed. This removes light at the emitter's prior
				// position without invalidating the global volume.
				// Collect rather than dispatch: hysteresis and ray count are
				// loop-invariant, so the whole batch goes out as one dispatch
				// after this loop instead of one barrier-separated,
				// single-thread-group dispatch per probe.
				rtBatchEntries.push_back({ pos, p });
				if (lightingRefresh && giLightingRefreshProbeBudget > 0) --giLightingRefreshProbeBudget;
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
				const float hyst = primingNow ? 0.0f : ((dynamicCacheUpdate || lightingRefresh) ? std::min(giHysteresis, 0.2f) : giHysteresis);
				dr->GiProjectProbe(giCube.GetSrv(), p, hyst, kGiFaceRes);
				if (lightingRefresh && giLightingRefreshProbeBudget > 0) --giLightingRefreshProbeBudget;
			}
		}

		if (!rtBatchEntries.empty())
		{
			const float hyst = primingNow ? 0.0f : ((dynamicCacheUpdate || lightingRefresh) ? std::min(giHysteresis, 0.2f) : giHysteresis);
			const int rayCount = dynamicCacheUpdate ? std::min(giRTRayCount, 24) : giRTRayCount;
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
};

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

	s.benchFrames  = EnvInt("BENCH_FRAMES", 0);
	s.warmupFrames = EnvInt("BENCH_WARMUP", 60);
	s.sponzaCopies = std::max(1, EnvInt("BENCH_SPONZA_COPIES", 1));
	// Deferred path uses a structured light buffer (no 8-light cap); the forward /
	// transparent path still only sees the first NUMBER_OF_LIGHTS_ALLOWED.
	s.reportPath   = EnvStr("BENCH_REPORT", "bench_report.json");

	if (const char* r = std::getenv("BENCH_ROT_X")) s.modelRotX = (float)atof(r);

	// Materials rendered in the forward transparent pass (substring match, after
	// lowercasing + stripping a Blender ".003" suffix). BENCH_TRANSPARENT_MATS
	// (comma-separated) overrides the default; empty disables the pass.
	{
		const std::string mats = EnvStr("BENCH_TRANSPARENT_MATS", "glass,lamp_glass");
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
	std::string cubeName = EnvStr("BENCH_CUBEMAP", "horizonCubeMap");
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
	s.sealScene    = EnvInt("BENCH_SEAL", 0) != 0;
	if (const char* v = std::getenv("BENCH_AUTOSEAL")) s.autoSeal = std::clamp((float)atof(v), 0.f, 1.f);
	s.currentScene = EnvStr("BENCH_SCENE", "");
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
	s.showDebugBall = EnvInt("BENCH_MATBALL", 0) != 0;
	s.pillarMaterialOverride = EnvInt("BENCH_PILLAR_OVERRIDE", 1) != 0;
	if (const char* value = std::getenv("BENCH_PILLAR_ROUGHNESS")) s.pillarMat.roughness = std::clamp((float)atof(value), 0.f, 1.f);
	if (const char* value = std::getenv("BENCH_PILLAR_METALNESS")) s.pillarMat.metalness = std::clamp((float)atof(value), 0.f, 1.f);
	if (const char* e = std::getenv("BENCH_MATBALL_EM"))
	{
		float r = 0, g = 0, b = 0, st = 8.f;
		if (sscanf(e, "%f,%f,%f,%f", &r, &g, &b, &st) >= 3)
		{
			s.debugMat.emissiveColor[0] = r; s.debugMat.emissiveColor[1] = g; s.debugMat.emissiveColor[2] = b;
			s.debugMat.emissiveStrength = st;
		}
	}
	if (int oc = EnvInt("BENCH_ORBITBALLS", 0))
	{
		s.showOrbitBalls  = oc > 0;
		s.orbitBallCount  = std::clamp(oc, 1, Impl::kMaxOrbitBalls);
		if (const char* v = std::getenv("BENCH_ORBIT_RADIUS")) s.orbitPathRadius = (float)atof(v);
		if (const char* v = std::getenv("BENCH_ORBIT_SPEED"))  s.orbitSpeed = (float)atof(v);
		if (const char* v = std::getenv("BENCH_ORBIT_BALLRAD")) s.orbitBallRadius = (float)atof(v);
	}
	s.probeEnabled  = EnvInt("BENCH_PROBE", 1) != 0;
	s.probeInterval = std::max(1, EnvInt("BENCH_PROBE_INTERVAL", 45));
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
	if (const char* pp = std::getenv("BENCH_PROBE_POS"))
	{
		float x = 0, y = 0, z = 0;
		if (sscanf(pp, "%f,%f,%f", &x, &y, &z) == 3) s.probePos = { x, y, z };
	}
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

	// --- emissive-GI volume grid: auto from scene bounds, or bench_gi_<scene>.json ---
	s.giEnabled = EnvInt("BENCH_GI", 1) != 0;
	s.giPrimeBatch = std::max(1, EnvInt("BENCH_GI_PRIME", 8));
	s.giBatchProbes = EnvInt("BENCH_GI_BATCH", 1) != 0;
	if (auto* dr = (s.useDeferred && GraphicsEngine::GetInstance()) ? &GraphicsEngine::GetInstance()->GetDeferredRenderer() : nullptr)
		dr->GetTunables().giViz = EnvInt("BENCH_GI_VIZ", 0) != 0;
	{
		// Volume covers the scene AABB, but the probes themselves must sit half a
		// cell inside it. A probe on a floor/wall immediately captures that same
		// surface (especially in the 16x16 raster cubemap fallback), producing a
		// perfectly regular lattice of bright dots at the probe spacing.
		// Boundary receivers still sample the interior ring via the clamped lookup.
		// (Override with bench_gi_<scene>.json.)
		const Vector3f ext = s.sceneExtents;
		const Vector3f mn = s.sceneCenter - ext;
		// Keep the automatic volume dense enough that nearby colored surfaces can
		// contribute locally to the receiver.  The old 10x6x10 ceiling left the
		// Sponza scene with ~2 km probe spacing, which made red/green bounce read
		// as a faint scene-wide wash instead of believable shadow color bleed.
		// 16x8x16 is 2048 probes, safely below kMaxGiProbes (4096), and explicit
		// bench_gi_<scene>.json files still override this layout when needed.
		auto axis = [](float span) { return std::clamp((int)std::round(span / 360.f) + 1, 2, 16); };
		s.giCx = axis(2.f * ext.x);
		s.giCy = std::clamp(axis(2.f * ext.y), 2, 8);
		s.giCz = axis(2.f * ext.z);
		s.giSpacing = { (2.f * ext.x) / std::max(1, s.giCx),
		                (2.f * ext.y) / std::max(1, s.giCy),
		                (2.f * ext.z) / std::max(1, s.giCz) };
		s.giOrigin = mn + s.giSpacing * 0.5f;
		const std::string gf = s.currentScene.empty() ? std::string("bench_gi.json")
		                                              : ("bench_gi_" + s.currentScene + ".json");
		std::ifstream in(gf);
		if (in)
		{
			try
			{
				nlohmann::json j; in >> j;
				auto a3 = [](const nlohmann::json& v, Vector3f d) {
					return (v.is_array() && v.size() >= 3)
						? Vector3f{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() } : d;
				};
				if (j.contains("origin"))  s.giOrigin  = a3(j["origin"], s.giOrigin);
				if (j.contains("spacing")) s.giSpacing = a3(j["spacing"], s.giSpacing);
				if (j.contains("counts") && j["counts"].is_array() && j["counts"].size() >= 3)
				{
					s.giCx = std::clamp(j["counts"][0].get<int>(), 2, 32);
					s.giCy = std::clamp(j["counts"][1].get<int>(), 2, 32);
					s.giCz = std::clamp(j["counts"][2].get<int>(), 2, 32);
				}
				INFO_PRINT("emissive GI: from %s", gf.c_str());
			}
			catch (...) { ERROR_PRINT("emissive GI: bad %s", gf.c_str()); }
		}
		if ((int64_t)s.giCx * s.giCy * s.giCz > DeferredRenderer::kMaxGiProbes)
		{
			ERROR_PRINT("emissive GI: %d probes > cap %d; shrinking", s.giCx * s.giCy * s.giCz, DeferredRenderer::kMaxGiProbes);
			s.giCx = std::min(s.giCx, 12); s.giCy = std::min(s.giCy, 8); s.giCz = std::min(s.giCz, 12);
		}
		INFO_PRINT("emissive GI: %dx%dx%d = %d probes, spacing(%.0f,%.0f,%.0f)",
			s.giCx, s.giCy, s.giCz, s.giCx * s.giCy * s.giCz, s.giSpacing.x, s.giSpacing.y, s.giSpacing.z);
	}

	s.useDeferred = EnvInt("BENCH_DEFERRED", 1) != 0;
	s.dxrRenderer = EnvInt("BENCH_DXR_RENDERER", 1) != 0;
	s.gbufChannel = std::clamp(EnvInt("BENCH_GBUF", 0), 0, 9);
	s.wantClustered = EnvInt("BENCH_CLUSTERED", 1) != 0;
	s.wantSSAO      = EnvInt("BENCH_SSAO", 1) != 0;
	s.wantShadows   = EnvInt("BENCH_SHADOWS", 1) != 0;
	s.wantPostFx    = EnvInt("BENCH_POSTFX", 1) != 0;
	s.wantLocalShadows = EnvInt("BENCH_LOCAL_SHADOWS", 1) != 0;
	s.wantSSR       = EnvInt("BENCH_SSR", 1) != 0;
	if (const char* p = std::getenv("BENCH_SUN_PITCH")) s.sunPitch = (float)atof(p);
	if (const char* y = std::getenv("BENCH_SUN_YAW"))   s.sunYaw   = (float)atof(y);
	if (const char* v = std::getenv("BENCH_SUN_INTENSITY")) s.sunIlluminanceLux = std::max(0.f,(float)atof(v)) * 100000.f;
	if (const char* v = std::getenv("BENCH_SUN_LUX")) s.sunIlluminanceLux = std::max(0.f,(float)atof(v));
	if (const char* v = std::getenv("BENCH_SUN_KELVIN")) { s.sunTemperatureK = (float)atof(v); s.sunUseTemperature = true; }
	if (auto* dr = (s.useDeferred && GraphicsEngine::GetInstance()) ? &GraphicsEngine::GetInstance()->GetDeferredRenderer() : nullptr)
		dr->GetTunables().shadowShowCascades = EnvInt("BENCH_SHADOW_VIZ", 0) != 0;
	s.screenshotPath = EnvStr("BENCH_SCREENSHOT", "");
	s.shotFrame = EnvInt("BENCH_SHOT_FRAME", 0);
	s.camOrbitHeight = EnvStr("BENCH_ORBIT_HEIGHT", "").empty() ? -1.f : (float)atof(EnvStr("BENCH_ORBIT_HEIGHT", "").c_str());
	if (s.useDeferred)
	{
		// The engine owns the deferred renderer now; the bench just drives it.
		DeferredRenderer& dr = GraphicsEngine::GetInstance()->GetDeferredRenderer();
		if (dr.IsReady())
		{
			dr.OnResize(res);
			s.deferred = &dr;
			auto& tun = dr.GetTunables();
			tun.bloomEnabled = EnvInt("BENCH_BLOOM", 1) != 0;
			if (const char* v = std::getenv("BENCH_BLOOM_INTENSITY")) tun.bloomIntensity = (float)atof(v);
			tun.dxrLightingView = EnvInt("BENCH_LIGHTING_VIEW", 0);
			tun.dxrTextureFiltering = EnvInt("BENCH_RAY_TEXTURE_FILTER", 1) != 0;
			tun.fogEnabled = EnvInt("BENCH_FOG", 1) != 0;
			if (const char* v = std::getenv("BENCH_FOG_DENSITY")) tun.fogDensity = (float)atof(v);
			tun.volumetricEnabled = EnvInt("BENCH_VOLUMETRIC", 1) != 0;
			tun.volumetricSteps = std::clamp(EnvInt("BENCH_VOLUMETRIC_STEPS", tun.volumetricSteps), 8, 64);
			tun.atmosphereDebugView = EnvInt("BENCH_ATMOSPHERE_VIEW", 0);
			tun.taaEnabled = EnvInt("BENCH_TAA", 1) != 0;
			tun.taaJitter = EnvInt("BENCH_TAA_JITTER", 1) != 0;
			// BENCH_DXR_DENOISER=1 turns on DLSS Ray Reconstruction, which replaces
			// the native temporal resolve with a real ray denoiser. Mirrors what the
			// ImGui checkbox does: RR runs as a 1:1 DLAA-shaped pass, not upscaling.
			// 0 = native temporal, 1 = DLAA, 2..5 = DLSS Quality..Ultra Performance.
			tun.dlssMode = std::clamp(EnvInt("BENCH_DLSS_MODE", 0), 0, 5);
			tun.dlaaEnabled = tun.dlssMode == 1;
			tun.nrdEnabled = EnvInt("BENCH_NRD", tun.nrdEnabled ? 1 : 0) != 0;
			if (EnvInt("BENCH_DXR_DENOISER", 0) != 0)
			{
				tun.rayReconstructionEnabled = true;
				tun.dlaaEnabled = true;
				tun.dlssMode = 1;
			}
			// dlssMode selects the DXR render resolution, and that is baked into
			// the targets when they are created -- which is why the ImGui combo
			// recreates them on every change. Without this, the ray pass keeps
			// rendering at full resolution while DLSS is told it is upscaling:
			// strictly more work than native, which is exactly backwards and
			// makes the more aggressive modes measure SLOWER than the gentle ones.
			if (tun.dlssMode != 0) dr.RecreateDxrTargets();
			tun.specularAaEnabled = EnvInt("BENCH_SPECULAR_AA", 1) != 0;
			tun.taaDebugView = EnvInt("BENCH_TAA_VIEW", 0);
			tun.dxrAmbientOcclusion = EnvInt("BENCH_DXR_AO", 1) != 0;
			tun.dxrAoSamples = std::clamp(EnvInt("BENCH_DXR_AO_SAMPLES", tun.dxrAoSamples), 1, 8);
			tun.dxrIndirectGi = EnvInt("BENCH_DXR_GI", 1) != 0;
			tun.dxrDirectLighting = EnvInt("BENCH_DXR_DIRECT", 1) != 0;
			// Reflections are the largest single term in the per-pixel ray
			// budget (each sample traces a ray AND re-runs the full direct
			// shade on its hit), so they need their own A/B knob like the
			// other big passes above.
			tun.dxrReflections = EnvInt("BENCH_DXR_REFLECTIONS", 1) != 0;
			tun.dxrReflectionSamples = std::clamp(EnvInt("BENCH_DXR_REFLECTION_SAMPLES", 4), 1, 4);
			if (const char* rc = std::getenv("BENCH_DXR_REFLECTION_CUTOFF")) tun.dxrReflectionRoughnessCutoff = std::clamp((float)atof(rc), 0.f, 1.f);
			tun.exposureAuto = EnvInt("BENCH_AUTOEXPOSURE", tun.exposureAuto ? 1 : 0) != 0;
			tun.tonemapper = std::clamp(EnvInt("BENCH_TONEMAP", tun.tonemapper), 0, 3);
			tun.nrdCheckerboard = EnvInt("BENCH_NRD_CHECKERBOARD", tun.nrdCheckerboard ? 1 : 0) != 0;
			tun.nrdDenoiser = std::clamp(EnvInt("BENCH_NRD_DENOISER", tun.nrdDenoiser), 0, 1);
			tun.nrdValidation = EnvInt("BENCH_NRD_VALIDATION", 0) != 0;
			tun.nrdAntilag = EnvInt("BENCH_NRD_ANTILAG", tun.nrdAntilag ? 1 : 0) != 0;
			if (const char* v = std::getenv("BENCH_NRD_HISTORY")) tun.nrdHistorySeconds = std::max(0.01f, (float)atof(v));
			tun.dxrSunShadowSamples = std::clamp(EnvInt("BENCH_SUN_SHADOW_SAMPLES", tun.dxrSunShadowSamples), 1, 4);
			tun.volumetricResolution = std::clamp(EnvInt("BENCH_FOG_RESOLUTION", tun.volumetricResolution), 0, 3);
			s.giProbeBudget = std::clamp(EnvInt("BENCH_GI_PROBES_PER_FRAME", s.giProbeBudget), 0, 256);
			tun.preExposure = EnvInt("BENCH_PRE_EXPOSURE", tun.preExposure ? 1 : 0) != 0;
			if (const char* v = std::getenv("BENCH_SKY_NITS")) tun.skyLuminanceNits = std::max(0.f, (float)atof(v));
			if (const char* v = std::getenv("BENCH_EV_COMP")) tun.exposureComp = (float)atof(v);
			if (const char* v = std::getenv("BENCH_APERTURE")) tun.cameraAperture = std::max(0.5f, (float)atof(v));
			if (const char* v = std::getenv("BENCH_SHUTTER")) tun.cameraShutter = std::max(1e-6f, (float)atof(v));
			if (const char* v = std::getenv("BENCH_ISO")) tun.cameraIso = std::max(1.f, (float)atof(v));
			tun.contactShadows = EnvInt("BENCH_CONTACT", 1) != 0;
			tun.contactViz = EnvInt("BENCH_CONTACT_VIZ", 0) != 0;
			tun.localShadowViz = EnvInt("BENCH_LOCALSH_VIZ", 0) != 0;
			tun.contactShadows = EnvInt("BENCH_CONTACT", 1) != 0;
			if (const char* v = std::getenv("BENCH_SSR_STRENGTH")) tun.ssrStrength = (float)atof(v);
		}
		else
		{
			ERROR_PRINT("Sponza bench: engine deferred renderer not ready, falling back to forward");
			s.deferred = nullptr;
			s.useDeferred = false;
		}
	}
	if (const char* a = std::getenv("BENCH_AMBIENT")) s.ambientScale = (float)atof(a);

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
	if (s.benchFrames > 0 && s.frame == s.warmupFrames && EnvInt("BENCH_COST_SWEEP", 0) != 0)
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
}

void GameWorld::DrawDebugUI()
{
#ifndef _RETAIL
	Impl& s = *myImpl;
	if (!s.debugUiOpen) return;
	const bool uiCapture = s.benchFrames > 0 && EnvInt("BENCH_DEBUG_UI",0) != 0;
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
			const bool select = s.benchFrames > 0 && EnvStr("BENCH_DEBUG_TAB", "") == std::string(label);
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
						ImGui::Text(s.giPriming ? "priming... probe %d / %d" : "primed (%d probes)",
						s.giCursor, s.giCx * s.giCy * s.giCz);
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
			const bool selectProfiler = s.benchFrames > 0 && EnvStr("BENCH_DEBUG_TAB", "") == std::string("Profiler");
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
		static const int shotCount = std::max(1, EnvInt("BENCH_SHOT_COUNT", 1));
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
	if (s.benchFrames == 0 || EnvInt("BENCH_DEBUG_UI", 0) != 0)
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
		s.ambient.cubemap = sealedRoom ? nullptr : s.fallbackCube;
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
			const float h = std::round(
				s.sunPitch * 2.f + s.sunYaw * 1.3f + s.SunIntensity() * 40.f
				+ (s.SunColor().x + s.SunColor().y * 2.f + s.SunColor().z * 3.f) * 20.f
				+ (s.ambientColor[0] + s.ambientColor[1] + s.ambientColor[2]) * s.ambientScale * 50.f
				+ (float)s.cubemapIdx * 100.f
				+ (sealedRoom ? 777.f : 0.f));
			// The physical sky scale is only known once the environment map has
			// been measured (a frame or two in), and the probes must be traced
			// with it; hash it separately so a tiny night sky still registers.
			const float skyH = std::round(std::log2(std::max(s.deferred->GetTunables().skyLuminanceNits, 1e-6f)) * 8.f
				+ std::log2(std::max(s.deferred->GetEnvironmentAverageLuminance(), 1e-9f)) * 8.f
				+ std::log2(std::max(s.sunIlluminanceLux, 1e-6f)) * 8.f);
			const float lightHash = h + skyH * 7919.f;
			if (lightHash != s.giLightHash)
			{
				s.giLightHash = lightHash;
				// A change mid-prime (typically the sky measurement arriving)
				// would otherwise leave half the volume traced with stale light.
				if (s.giPriming) s.StartGiPrime();
				else
				{
					if (s.giUseRT)
					{
						// Do not clear the whole cache while the user drags a sun.
						// Refresh one sweep with low hysteresis instead, so existing
						// indirect light remains stable and the new result converges.
						s.giKeepUpdating = true;
						s.giSkipCount = 0;
						s.giLightingRefreshProbeBudget = s.giCx * s.giCy * s.giCz;
					}
					else s.StartGiPrime();
				}
			}
		}
		if (s.giPriming)
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
			const DeferredRenderer::DebugMaterial& dm = s.debugMat;
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
		if (s.frame == EnvInt("BENCH_TAA_RESET_FRAME", -1)) s.deferred->ResetTemporalHistory();
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
	if (std::getenv("BENCH_NOCULLFACE"))
		gss.SetRasterizerState(RasterizerState::NoFaceCulling);   // room planes are viewed from the inside

	const Frustum frustum = CalculateFrustum(s.camera);
	ModelDrawer& md = ge.GetModelDrawer();
	md.SetCullFrustum((s.frustumCull && s.models.size() > 1) ? &frustum : nullptr);

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
		auto fixedMaterialIndex = [](StringId name, const DeferredRenderer::DebugMaterial& material)
		{
			const uint32_t index = RayTracingMaterialTable::GetOrAssignMaterialIndex(name);
			RayTracingMaterialTable::FixedMaterial fixed;
			for (int i = 0; i < 3; ++i)
			{
				fixed.baseColor[i] = material.baseColor[i];
				fixed.emissiveColor[i] = material.emissiveColor[i];
			}
			fixed.roughness = material.roughness;
			fixed.metalness = material.metalness;
			fixed.ao = material.ao;
			fixed.emissiveStrength = material.emissiveStrength;
			RayTracingMaterialTable::SetFixedMaterial(index, fixed);
			return index;
		};
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
				if (materialOverride == 0u && textureMeshIndex < MAX_MESHES_PER_MODEL)
				{
					// TGO texture overrides belong to the instance, not the shared
					// mesh -- but only when the instance actually HAS one. This used
					// to run unconditionally for every mesh of every ordinary
					// instance (the far more common case: no per-instance texture
					// override at all, ModelInstance::myTextures all null), silently
					// replacing the mesh's real, texture-bearing materialIndex
					// (already correctly set just above from
					// AssignDefaultMaterials's work) with a brand-new, empty,
					// per-instance record every frame. DecodeHit's fallback for a
					// record with no albedo/normal/orm/emissive SRV and
					// useFixedMaterial == 0 is a flat, saturated colour hashed from
					// materialIndex alone (DxrCommon.hlsli) -- i.e. every mesh
					// instance in the whole scene rendering as an arbitrary flat
					// hue with no relation to its actual texture, which is exactly
					// the "checkerboard-looking mosaic of flat colours" bug.
					const auto textures = instance.GetTextures(textureMeshIndex);
					const bool hasOverride = textures[0] || textures[1] || textures[2] || textures[3];
					if (hasOverride)
					{
						const std::string name = "dxr/scene/" + s.currentScene + "/instance/" + std::to_string(d.instanceId);
						d.materialIndex = RayTracingMaterialTable::GetOrAssignMaterialIndex(StringRegistry::RegisterOrGetString(name));
						RayTracingMaterialTable::SetMaterialTextures(d.materialIndex, {
							textures[0] ? textures[0]->GetSrv() : rhi::SrvHandle{},
							textures[1] ? textures[1]->GetSrv() : rhi::SrvHandle{},
							textures[2] ? textures[2]->GetSrv() : rhi::SrvHandle{},
							textures[3] ? textures[3]->GetSrv() : rhi::SrvHandle{} });
					}
				}
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
				? fixedMaterialIndex("dxr/debug/pillar"_tgaid, s.pillarMat) : 0u;
			for (const ModelInstance& instance : s.models) addInstance(instance, pillarMaterial);

		const uint32_t debugMaterial = s.debugBallValid && (s.showDebugBall || s.showOrbitBalls)
			? fixedMaterialIndex("dxr/debug/sphere"_tgaid, s.debugMat) : 0u;

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
			const Frustum* fr = (s.frustumCull && s.models.size() > 1) ? &frustum : nullptr;

			auto drawOpaque = [dr, mdp, sp, fr]()
			{
				mdp->SetCullFrustum(fr);   // the shadow pass clears it
				const ModelShader& gsh = dr->GetGeometryShader();
				if (sp->IsPillarTest() && sp->pillarMaterialOverride && dr->HasDebugMatShader())
				{
					dr->BindDebugMaterial(sp->pillarMat);
					for (const ModelInstance& instance : sp->models) instance.Render(dr->GetDebugMatShader());
				}
				else
				for (size_t k = 0; k < sp->models.size(); ++k)
				{
					if (!sp->anyTransparent || sp->transparentMeshes[k].empty())
						mdp->Draw(sp->models[k], gsh);              // whole model (keeps frustum cull)
					else
						sp->models[k].Render(gsh, sp->opaqueMeshes[k]);
				}
				if (dr->HasDebugMatShader() && sp->debugBallValid &&
					(sp->showDebugBall || sp->showOrbitBalls))
				{
					dr->BindDebugMaterial(sp->debugMat);
					const ModelShader& dsh = dr->GetDebugMatShader();
					if (sp->showDebugBall)
						sp->debugBall.Render(dsh);
					if (sp->showOrbitBalls)
					{
						const int n = std::clamp(sp->orbitBallCount, 1, Impl::kMaxOrbitBalls);
						for (int i = 0; i < n && i < (int)sp->orbitBalls.size(); ++i)
							sp->orbitBalls[i].Render(dsh);
					}
				}
			};

			std::function<void()> drawTransparent;
			if (s.anyTransparent)
			{
				drawTransparent = [dr, mdp, sp]()
				{
					// Deferred transparency is classified as glass during import.  Use
					// the HDR/depth-aware forward shader when it compiled; retain the
					// original PBR path as a safe shader-load fallback.
					const ModelShader& psh = dr->HasGlassShader() ? dr->GetGlassShader() : mdp->GetPbrShader();
					for (size_t k = 0; k < sp->models.size(); ++k)
						sp->models[k].Render(psh, sp->transparentMeshes[k]);
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
						sp->models[k].Render(ssh, sp->opaqueMeshes[k]);
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
