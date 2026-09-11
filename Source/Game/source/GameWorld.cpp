#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorld.h"
#include "CubemapPrefilter.h"
#include <cstdio>
#include <tge/render/DeferredRenderer.h>
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
	static const char* kBuiltinRoomScene = "<BuiltinRoom>";

	// A fixed-parameter PBR material, matching DeferredRenderer::DebugMaterial +
	// optional [C,N,M,FX] texture map paths. Serialised as .tgmat JSON by the
	// GameEditor's Material Editor; loaded here for the built-in room + debug sphere.
	struct MaterialDef
	{
		float baseColor[3]     = { 0.8f, 0.8f, 0.8f };
		float roughness        = 0.5f;
		float metalness        = 0.0f;
		float ao               = 1.0f;
		float emissiveColor[3] = { 0.0f, 0.0f, 0.0f };
		float emissiveStrength = 0.0f;
		std::array<std::string, 4> maps{ "", "", "", "" };
	};

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
		out.roughness        = j.value("roughness", out.roughness);
		out.metalness        = j.value("metalness", out.metalness);
		out.ao               = j.value("ao", out.ao);
		out.emissiveStrength = j.value("emissiveStrength", out.emissiveStrength);
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

	// One renderable from a .tgo / scene object: model + explicit per-mesh textures
	// ([C,N,M,FX] per material) + a world transform.
	struct SceneEntry
	{
		std::string fbx;
		std::vector<std::array<std::string, 4>> textures;   // per mesh/material
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
			out.textures.clear();
			if (v.contains("textures"))
			{
				for (const json& row : v["textures"])
				{
					std::array<std::string, 4> t{ "", "", "", "" };
					for (size_t i = 0; i < row.size() && i < 4; ++i)
					{
						std::string s = row[i].get<std::string>();
						std::replace(s.begin(), s.end(), '\\', '/');
						t[i] = s;
					}
					out.textures.push_back(t);
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

	// Load a .tgs scene: <gameRoot>/<name>.tgs + its <name>.leveldata/ folder of
	// object files. Each object references a .tgo (via "path") or carries the
	// Model property inline, plus translation/rotation/scale.
	std::vector<SceneEntry> LoadTgs(const std::string& name)
	{
		std::vector<SceneEntry> out;
		const fs::path root = fs::path(Tga::Settings::GameAssetRoot());
		fs::path levelData = root / (name + ".leveldata");
		std::error_code ec;
		if (!fs::exists(levelData, ec))
		{
			// also try the name as given (may already include .tgs)
			levelData = root / (fs::path(name).stem().string() + ".leveldata");
			if (!fs::exists(levelData, ec)) { ERROR_PRINT("bench: no leveldata for scene '%s'", name.c_str()); return out; }
		}

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
					fs::path found;
					for (const fs::directory_entry& de : fs::recursive_directory_iterator(root, ec))
					{
						if (de.is_regular_file() && de.path().extension() == ".tgo"
							&& de.path().stem().string() == defName) { found = de.path(); break; }
					}
					if (!found.empty()) { if (auto le = LoadTgo(found)) { e = *le; haveModel = true; } }
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
	int   lightCount = 8;
	std::string reportPath = "bench_report.json";
	std::string modelPath = "sponza/Sponza.fbx";

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
	std::unique_ptr<InputManager> input;

	Vector3f sceneCenter{ 0,0,0 };
	Vector3f sceneExtents{ 1000,1000,1000 };
	float    orbitRadius = 800.f;

	// ---- free-fly state
	Vector3f camPos{ 0, 200, -800 };
	Vector3f camRot{ 10, 0, 0 };          // pitch, yaw, roll (deg)
	bool     mouseTrapped = false;
	float    flySpeed = 1200.f;

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
	bool useDeferred = true;
	int  gbufChannel = 0;   // 0 = normal output, 1..8 = G-buffer debug view

	// ---- live-tweak state (debug UI, free-fly only)
	float sunPitch = 55.f, sunYaw = -35.f;
	float sunColor[3] = { 1.0f, 0.96f, 0.88f };
	float sunSoftness = 0.f;
	float sunIntensity = 1.0f;
	float ambientColor[3] = { 0.35f, 0.42f, 0.55f };
	float ambientScale = 1.0f;   // BENCH_AMBIENT env; scales the ambient/IBL term
	int   cubemapIdx = 0;        // panel Cubemap combo

	// --- reflection probe (Phase 5 stage A): one probe, re-captured every N frames ---
	std::unique_ptr<CubemapPrefilter> probePrefilter;
	CubemapData probeBase, probePrefiltered;
	std::unique_ptr<RenderTarget> probeFaceRt;
	std::unique_ptr<DepthBuffer>  probeFaceDepth;
	TextureResource* fallbackCube = nullptr;   // the env_* cube, used when the probe is off
	Vector3f probePos{ 0.f, 0.f, 0.f };
	Vector3f probeBox{ 1000.f, 1000.f, 1000.f };   // influence-box half-extents (box parallax)
	bool  probeEnabled = true;

	// --- emissive-GI irradiance volume (Phase 6 stage 1) ---
	CubemapData giCube;
	std::unique_ptr<RenderTarget> giFaceRt;
	std::unique_ptr<DepthBuffer>  giFaceDepth;
	Vector3f giOrigin{ 0.f, 0.f, 0.f };
	Vector3f giSpacing{ 200.f, 200.f, 200.f };
	int   giCx = 8, giCy = 4, giCz = 8;
	float giIntensity = 1.6f;
	float giHysteresis = 0.85f;
	bool  giEnabled = true;
	int   giCursor = 0;
	int   giPrimeBatch = 8;     // probes/frame while first populating the volume
	int   giTrickle = 1;        // probes per capture after priming (0 = stop)
	int   giFrameSkip = 6;      // ...one capture every N frames after priming
	int   giSkipCount = 0;
	bool  giPriming = true;
	bool  giKeepUpdating = false;
	bool  giAutoReprime = true;   // re-prime when the sun / ambient changes
	float giLightHash = 0.f;
	static constexpr int kGiFaceRes = 16;
	int   probeInterval = 30;      // recapture cadence (frames); BENCH_PROBE_INTERVAL
	int   probeCountdown = 0;
	static constexpr int kProbeRes = 256;

	// --- material preview debug sphere ---
	ModelInstance debugBall;
	bool debugBallValid = false;
	bool showDebugBall = false;
	DeferredRenderer::DebugMaterial debugMat;
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

	// --- built-in procedural room (<BuiltinRoom> scene) ---
	// 6 primitive-plane surfaces: 0 floor, 1 ceiling, 2..5 walls (-X,+X,-Z,+Z).
	std::vector<ModelInstance> roomSurfaces;
	Vector3f roomSize{ 1600.f, 900.f, 1600.f };
	bool roomSealed = true;   // <BuiltinRoom>: closed box, no sun / no sky IBL
	bool roomLamps = true;    // <BuiltinRoom>: 4 neutral ceiling fill lamps
	// Any scene: skip the fallback rainbow point-light rig / kill sun + sky IBL.
	bool autoLightRig = true;
	bool sealScene = false;
	// Procedural interior detection: fade sun + sky-IBL by GI-probe sky visibility.
	// 0 = off, 1 = full. Needs GI enabled + the volume covering the play space.
	float autoSeal = 0.f;
	DeferredRenderer::DebugMaterial roomMat[6];
	char roomTgmatPath[260] = "";
	char dbgTgmatPath[260] = "";
	int  roomEditSurface = 0;
	bool roomInitDone = false;
	bool  wantShadows = true, wantSSAO = true, wantClustered = true, wantPostFx = true;
	bool  wantLocalShadows = true;   // point/spot shadow atlas
	bool  wantSSR = true;
	bool  debugUiOpen = true;
	bool  showLightMarkers = true;
	bool  showPerfOverlay = true;
	std::string screenshotPath;
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
	bool  orbitRoom = false;   // BENCH_CAM=room: orbit the scene centre even with a saved camera

	void SetScriptedCamera()
	{
		const float u = benchFrames > 1 ? (float)frame / (float)(benchFrames - 1) : 0.f;

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
		Vector3f pos = look + Vector3f{ std::cos(ang) * orbitRadius,
		                                sceneExtents.y * (0.10f + 0.08f * std::sin(ang * 0.5f)),
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
		const float speed = flySpeed * (input->IsKeyHeld(VK_SHIFT) ? 4.f : 1.f);
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

	// Optional authored point lights: bench_lights_<scene>.json =
	//   { "lights": [ { "pos":[x,y,z], "color":[r,g,b], "intensity":6,
	//                   "range":<world>, "radius":20 }, ... ] }
	// color is 0..1; final RGB = color * intensity * BENCH_EXPOSURE. Falls back to
	// the procedural rig when the file is absent/empty. Capped at
	// NUMBER_OF_LIGHTS_ALLOWED until Phase 3 lifts it.
	bool LoadLights(const std::string& file, float sceneRadius, float exposure)
	{
		std::ifstream in(file);
		if (!in) return false;
		nlohmann::json j;
		try { in >> j; } catch (...) { ERROR_PRINT("bench: bad light file %s", file.c_str()); return false; }
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
			const Vector3f pos = L.contains("pos")   ? a3(L["pos"],   Vector3f{ 0,0,0 }) : Vector3f{ 0,0,0 };
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
			lightExtra.push_back(ex);
			++n;
		}
		INFO_PRINT("bench: %d authored light(s) from %s%s", n, file.c_str(),
			skipped ? "  (some skipped: over kMaxLights)" : "");
		return n > 0;
	}

	std::string currentScene = "TEST";

	bool IsBuiltinRoom() const { return currentScene == kBuiltinRoomScene; }

	// Begin a fresh GI prime: wipe the SH buffer so the sweep is deterministic
	// (no history blended in -- fixes "re-prime gives a different result each time").
	void StartGiPrime()
	{
		giCursor = 0;
		giPriming = true;
		if (deferred && deferred->HasGi()) deferred->ClearGi();
	}

	// Assemble the 6 primitive-plane surfaces of the procedural room into `models`.
	// Order: 0 floor, 1 ceiling, 2 wall -X, 3 wall +X, 4 wall -Z, 5 wall +Z.
	// Normals point into the room; the draw path forces no-face-culling so winding
	// never matters. Surface materials live in roomMat[] (edited in ImGui / .tgmat).
	void BuildBuiltinRoom(ModelFactory& mf)
	{
		std::shared_ptr<Model> plane = mf.GetModel("Plane");   // engine built-in, 100x100 XZ, +Y normal
		if (!plane) { ERROR_PRINT("BuiltinRoom: 'Plane' primitive missing"); return; }

		const float hx = roomSize.x * 0.5f, hy = roomSize.y * 0.5f, hz = roomSize.z * 0.5f;
		const float sx = roomSize.x / 100.f, sy = roomSize.y / 100.f, sz = roomSize.z / 100.f;

		struct Surf { Vector3f scale; Matrix4x4f rot; Vector3f pos; };
		const Surf surfs[6] = {
			{ { sx, 1.f, sz }, Matrix4x4f::CreateIdentityMatrix(),          { 0.f,  0.f,  0.f } }, // floor  (+Y)
			{ { sx, 1.f, sz }, Matrix4x4f::CreateRotationAroundX(180.f),    { 0.f, roomSize.y, 0.f } }, // ceiling (-Y)
			{ { sy, 1.f, sz }, Matrix4x4f::CreateRotationAroundZ(-90.f),    { -hx, hy, 0.f } },    // wall -X (+X)
			{ { sy, 1.f, sz }, Matrix4x4f::CreateRotationAroundZ(90.f),     {  hx, hy, 0.f } },    // wall +X (-X)
			{ { sx, 1.f, sy }, Matrix4x4f::CreateRotationAroundX(90.f),     { 0.f, hy, -hz } },    // wall -Z (+Z)
			{ { sx, 1.f, sy }, Matrix4x4f::CreateRotationAroundX(-90.f),    { 0.f, hy,  hz } },    // wall +Z (-Z)
		};

		roomSurfaces.clear();
		roomSurfaces.resize(6);
		if (!roomInitDone)
		{
			// Defaults: bright glossy floor, mid-grey rough ceiling + walls.
			for (int i = 0; i < 6; ++i)
			{
				roomMat[i] = DeferredRenderer::DebugMaterial{};
				roomMat[i].metalness = 0.f;
				roomMat[i].ao = 1.f;
				if (i == 0) { roomMat[i].baseColor[0] = roomMat[i].baseColor[1] = roomMat[i].baseColor[2] = 0.9f; roomMat[i].roughness = 0.05f; }
				else        { roomMat[i].baseColor[0] = roomMat[i].baseColor[1] = roomMat[i].baseColor[2] = 0.5f; roomMat[i].roughness = 1.0f; }
			}
			roomInitDone = true;
		}

		for (int i = 0; i < 6; ++i)
		{
			roomSurfaces[i].Init(plane);
			Matrix4x4f m = Matrix4x4f::CreateFromScale(surfs[i].scale) * surfs[i].rot;
			m.SetPosition(surfs[i].pos);
			roomSurfaces[i].SetTransform(m);
			models.push_back(roomSurfaces[i]);
			opaqueMeshes.push_back(std::vector<int>{ 0 });
			transparentMeshes.push_back(std::vector<int>{});
		}
	}

	// (Re)load everything that depends on the scene: instances, sub-mesh split,
	// bounds, the light rig, and the start camera. Safe to call at runtime to
	// switch scenes (free-fly). aEnv = honour BENCH_* overrides (Init only).
	bool LoadSceneContent(const std::string& sceneName, bool aEnv)
	{
		currentScene = sceneName;

		models.clear();
		opaqueMeshes.clear();
		transparentMeshes.clear();
		instanceOffsets.clear();
		pointLights.clear();
		lightExtra.clear();
		anyTransparent = false;

		ModelFactory& mf = ModelFactory::GetInstance();
		auto& texMgr = GraphicsEngine::GetInstance()->GetTextureManager();

		if (sceneName == kBuiltinRoomScene)
		{
			const auto tRoom0 = std::chrono::high_resolution_clock::now();
			BuildBuiltinRoom(mf);
			modelLoadMs = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now() - tRoom0).count();
			if (models.empty()) { ERROR_PRINT("BuiltinRoom: build failed"); return false; }
		}
		else
		{
		std::vector<SceneEntry> entries;
		if (!sceneName.empty())
		{
			entries = LoadTgs(sceneName);
			INFO_PRINT("bench: scene '%s' -> %zu object(s)", sceneName.c_str(), entries.size());
		}
		else if (modelPath.size() > 4 && modelPath.substr(modelPath.size() - 4) == ".tgo")
		{
			if (auto e = LoadTgo(fs::path(Settings::GameAssetRoot()) / modelPath)) entries.push_back(*e);
		}
		else
		{
			SceneEntry e; e.fbx = modelPath; entries.push_back(e);
		}
		if (entries.empty())
		{
			ERROR_PRINT("bench: nothing to load (model='%s' scene='%s')", modelPath.c_str(), sceneName.c_str());
			return false;
		}

		const auto tLoad0 = std::chrono::high_resolution_clock::now();
		const int side = (int)std::ceil(std::sqrt((double)sponzaCopies));
		const bool tileCopies = (entries.size() == 1);

		for (const SceneEntry& e : entries)
		{
			std::shared_ptr<Model> model = mf.GetModel(e.fbx.c_str());
			if (!model) { ERROR_PRINT("bench: failed to load '%s'", e.fbx.c_str()); continue; }

			const int copies = tileCopies ? sponzaCopies : 1;
			const float sizeXZ0 = std::max(sceneExtents.x, sceneExtents.z) * 2.f;
			const float step = sizeXZ0 * 1.15f;

			for (int i = 0; i < copies; ++i)
			{
				ModelInstance mi;
				mi.Init(model);

				const int meshCount = std::min((int)model->GetMeshCount(), MAX_MESHES_PER_MODEL);
				for (int m = 0; m < meshCount && m < (int)e.textures.size(); ++m)
					for (int j = 0; j < 4; ++j)
					{
						if (e.textures[m][j].empty()) continue;
						const TextureSrgbMode sm = (j == 0) ? TextureSrgbMode::ForceSrgbFormat : TextureSrgbMode::ForceNoSrgbFormat;
						if (Tga::Texture* t = texMgr.GetTexture(e.textures[m][j].c_str(), sm))
							mi.SetTexture(m, j, t);
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
					(MatchesAny(mat ? mat : "", transparentMatKeys) ? tr : op).push_back(m);
				}
				if (!tr.empty()) anyTransparent = true;
				opaqueMeshes.push_back(std::move(op));
				transparentMeshes.push_back(std::move(tr));
			}
		}
		modelLoadMs = std::chrono::duration<double, std::milli>(
			std::chrono::high_resolution_clock::now() - tLoad0).count();
		} // end else (non-BuiltinRoom scene load)
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

		// Material-preview debug sphere (Source/Game/data/Sphere/Sphere.fbx).
		if (!debugBallValid)
		{
			if (std::shared_ptr<Model> sph = mf.GetModel("Sphere/Sphere.fbx"))
			{
				debugBall.Init(sph);
				debugBallValid = true;
				debugBallModelRadius = std::max(sph->GetBounds().radius, 0.001f);
				orbitBalls.clear();
				orbitBalls.resize(kMaxOrbitBalls);
				for (ModelInstance& mi : orbitBalls) mi.Init(sph);
			}
			else ERROR_PRINT("material preview: Sphere/Sphere.fbx not found");
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

		const std::string defLightFile = sceneName.empty() ? std::string("bench_lights.json")
		                                                   : ("bench_lights_" + sceneName + ".json");
		const std::string lightFile = aEnv ? EnvStr("BENCH_LIGHTFILE", defLightFile.c_str()) : defLightFile;

		if (sceneName == kBuiltinRoomScene)
		{
			// Optional neutral fill: 4 soft white lamps just below the ceiling.
			if (roomLamps)
			{
				const float intensity = 1.6f * exposure * (sceneRadius / 1200.f) * (sceneRadius / 1200.f);
				const float ox = sceneExtents.x * 0.45f, oz = sceneExtents.z * 0.45f;
				const Vector3f lamp[4] = {
					{ sceneCenter.x - ox, roomSize.y * 0.86f, sceneCenter.z - oz },
					{ sceneCenter.x + ox, roomSize.y * 0.86f, sceneCenter.z - oz },
					{ sceneCenter.x - ox, roomSize.y * 0.86f, sceneCenter.z + oz },
					{ sceneCenter.x + ox, roomSize.y * 0.86f, sceneCenter.z + oz },
				};
				for (const Vector3f& lp : lamp)
				{
					PointLight p;
					p.position = lp;
					p.color = Color{ intensity, intensity * 0.97f, intensity * 0.92f, 1.f };
					p.range = sceneRadius * 1.6f;
					p.radius = sceneRadius * 0.02f;
					pointLights.push_back(p);
					lightExtra.push_back({});
				}
			}
		}
		else if (!LoadLights(lightFile, sceneRadius, exposure) && autoLightRig)
		{
			const float span = sceneExtents.x * 0.7f;
			const float rel = sceneRadius / 1600.f;
			const float intensity = 2.0f * exposure * rel * rel;
			static const float kPalette[6][3] = {
				{ 1.00f, 0.30f, 0.25f }, { 1.00f, 0.65f, 0.20f }, { 0.35f, 1.00f, 0.40f },
				{ 0.25f, 0.80f, 1.00f }, { 0.45f, 0.45f, 1.00f }, { 0.90f, 0.35f, 1.00f },
			};
			const int latSide = std::max(1, (int)std::ceil(std::cbrt((float)lightCount)));
			const float rangeScale = lightCount <= 8 ? 1.1f
				: std::max(0.5f, 1.1f * std::cbrt(8.f / (float)lightCount));
			for (int i = 0; i < lightCount; ++i)
			{
				const int gx = i % latSide, gy = (i / latSide) % latSide, gz = i / (latSide * latSide);
				auto frac = [latSide](int g) { return latSide > 1 ? (float)g / (float)(latSide - 1) - 0.5f : 0.f; };
				PointLight p;
				p.position = sceneCenter + Vector3f{ frac(gx) * 1.6f * span,
				                                     frac(gy) * sceneExtents.y * 1.2f,
				                                     frac(gz) * sceneExtents.z * 1.4f };
				const float* c = kPalette[i % 6];
				p.color = Color{ c[0] * intensity, c[1] * intensity, c[2] * intensity, 1.f };
				p.range = sceneRadius * rangeScale;
				p.radius = sceneRadius * 0.01f;
				pointLights.push_back(p);
				lightExtra.push_back({});
			}
		}

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
		o << "  \"model\": \"" << modelPath << "\",\n";
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
		o << " }\n";
		o << "}\n";
		o.close();

		INFO_PRINT("=== Sponza bench: %zu frames | frame %.3f ms (%.1f fps) | cpu %.3f ms | %.0f draw calls | report -> %s",
			frameMs.size(), fMean, fMean > 0 ? 1000.0 / fMean : 0.0, cMean, dcMean, reportPath.c_str());
	}

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
			if (skyVS && skyVS->shader && skyPS && skyPS->shader)
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

		if (probePrefilter->CaptureSceneToCubemap(*probeFaceRt, faceCb, probeBase))
		{
			probePrefilter->GeneratePrefilteredCubemap(
				probeBase.srv.Get(), probeBase.size, 128, 128, probePrefiltered);
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
		if (!dr || !dr->HasGi() || !probePrefilter || !giFaceRt || !giFaceDepth || !fallbackCube) return;

		auto& gss = ge.GetGraphicsStateStack();
		auto& mdl = ge.GetModelDrawer();
		const Camera savedCam = gss.GetCamera();

		// A sealed built-in room has no sky: don't bake the skybox into GI, and let
		// the probes see the emissive spheres so the bounce takes their colour.
		const bool sealed = (IsBuiltinRoom() && roomSealed) || sealScene;

		AmbientLight capAmb = ambient;
		capAmb.cubemap = sealed ? nullptr : fallbackCube;
		gss.SetAmbientLight(capAmb);

		const VertexShader* skyVS = DX11::LoadVertexShader("Shaders/SkyboxVS");
		const PixelShader*  skyPS = DX11::LoadPixelShader("Shaders/SkyboxPS");

		const int total = giCx * giCy * giCz;
		if (total <= 0) return;

		const bool primingNow = giPriming;   // fixed for the whole batch (deterministic replace)
		const int batch = giPriming ? giPrimeBatch : giTrickle;
		for (int n = 0; n < batch; ++n)
		{
			const int p = giCursor;
			giCursor = giCursor + 1;
			if (giCursor >= total) { giCursor = 0; giPriming = false; }

			const int px = p % giCx;
			const int py = (p / giCx) % giCy;
			const int pz = p / (giCx * giCy);
			const Vector3f pos = giOrigin + Vector3f{ px * giSpacing.x, py * giSpacing.y, pz * giSpacing.z };

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

				if (!sealed && skyVS && skyVS->shader && skyPS && skyPS->shader)
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

			if (probePrefilter->CaptureSceneToCubemap(*giFaceRt, faceCb, giCube))
			{
				// Priming replaces (deterministic); only the live trickle blends.
				const float hyst = primingNow ? 0.0f : giHysteresis;
				dr->GiProjectProbe(giCube.GetSrv(), p, hyst, kGiFaceRes);
			}
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
	s.lightCount   = std::clamp(EnvInt("BENCH_LIGHTS", 8), 0, DeferredRenderer::kMaxLights);
	s.reportPath   = EnvStr("BENCH_REPORT", "bench_report.json");
	s.modelPath    = EnvStr("BENCH_MODEL", "sponza/Sponza.fbx");

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

	// Load the scene (instances, bounds, light rig, start camera). Runtime scene
	// switching (ImGui) calls this again with aEnv = false.
	if (const char* v = std::getenv("BENCH_ROOM_SIZE"))
	{
		float a = (float)atof(v);
		if (a > 1.f) s.roomSize = { a, a * 0.56f, a };
	}
	s.autoLightRig = EnvInt("BENCH_AUTOLIGHTS", 1) != 0;
	s.sealScene    = EnvInt("BENCH_SEAL", 0) != 0;
	if (const char* v = std::getenv("BENCH_AUTOSEAL")) s.autoSeal = std::clamp((float)atof(v), 0.f, 1.f);
	s.currentScene = EnvStr("BENCH_SCENE", "TEST");
	if (!s.LoadSceneContent(s.currentScene, true))
		return;

	if (HWND* hwnd = Application::GetInstance()->GetHWND())
		s.input = std::make_unique<InputManager>(*hwnd);

	s.gpu.Init();

	// --- reflection probe (Phase 5 stage A) ---
	s.showDebugBall = EnvInt("BENCH_MATBALL", 0) != 0;
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
	if (auto* dr = (s.useDeferred && GraphicsEngine::GetInstance()) ? &GraphicsEngine::GetInstance()->GetDeferredRenderer() : nullptr)
		dr->GetTunables().giViz = EnvInt("BENCH_GI_VIZ", 0) != 0;
	{
		// Volume = scene AABB exactly. The outer probe ring then sits *on* the
		// bounding geometry (outer walls / floor / ceiling) rather than floating
		// outside it: full GI coverage for open scenes, and boundary probes are
		// embedded in the boundary geometry so auto-seal sky-visibility stays
		// correct for interior surfaces. (Override with bench_gi_<scene>.json.)
		const Vector3f ext = s.sceneExtents;
		const Vector3f mn = s.sceneCenter - ext;
		auto axis = [](float span) { return std::clamp((int)std::round(span / 360.f) + 1, 2, 10); };
		s.giCx = axis(2.f * ext.x);
		s.giCy = std::clamp(axis(2.f * ext.y), 2, 6);
		s.giCz = axis(2.f * ext.z);
		s.giOrigin = mn;
		s.giSpacing = { (2.f * ext.x) / std::max(1, s.giCx - 1),
		                (2.f * ext.y) / std::max(1, s.giCy - 1),
		                (2.f * ext.z) / std::max(1, s.giCz - 1) };
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
	s.gbufChannel = std::clamp(EnvInt("BENCH_GBUF", 0), 0, 9);
	s.wantClustered = EnvInt("BENCH_CLUSTERED", 1) != 0;
	s.wantSSAO      = EnvInt("BENCH_SSAO", 1) != 0;
	s.wantShadows   = EnvInt("BENCH_SHADOWS", 1) != 0;
	s.wantPostFx    = EnvInt("BENCH_POSTFX", 1) != 0;
	s.wantLocalShadows = EnvInt("BENCH_LOCAL_SHADOWS", 1) != 0;
	s.wantSSR       = EnvInt("BENCH_SSR", 1) != 0;
	if (const char* p = std::getenv("BENCH_SUN_PITCH")) s.sunPitch = (float)atof(p);
	if (const char* y = std::getenv("BENCH_SUN_YAW"))   s.sunYaw   = (float)atof(y);
	if (auto* dr = (s.useDeferred && GraphicsEngine::GetInstance()) ? &GraphicsEngine::GetInstance()->GetDeferredRenderer() : nullptr)
		dr->GetTunables().shadowShowCascades = EnvInt("BENCH_SHADOW_VIZ", 0) != 0;
	s.screenshotPath = EnvStr("BENCH_SCREENSHOT", "");
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
			tun.exposureAuto = EnvInt("BENCH_AUTOEXPOSURE", 0) != 0;
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

	INFO_PRINT("Sponza bench: model '%s'  center(%.0f,%.0f,%.0f) extents(%.0f,%.0f,%.0f)  copies=%d  lights=%d  benchFrames=%d",
		s.modelPath.c_str(), s.sceneCenter.x, s.sceneCenter.y, s.sceneCenter.z,
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

	ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Render tuning", &s.debugUiOpen))
	{
		ImGui::Text("%.1f FPS  (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::TextDisabled("RMB look - WASD move - Shift fast - F5 save cam");
		ImGui::Separator();

		// --- scene picker: every *.tgs under the game data root ---
		{
			static std::vector<std::string> sceneList;
			static bool scanned = false;
			if (!scanned)
			{
				scanned = true;
				std::error_code ec;
				const std::filesystem::path root = Tga::Settings::GameAssetRoot();
				for (const auto& de : std::filesystem::directory_iterator(root, ec))
					if (de.is_regular_file() && de.path().extension() == ".tgs")
						sceneList.push_back(de.path().stem().string());
				std::sort(sceneList.begin(), sceneList.end());
				sceneList.insert(sceneList.begin(), kBuiltinRoomScene);   // procedural room, always available
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

		// Scene lighting: the fallback "rainbow" point-light rig only makes sense
		// for the headline Sponza benchmark. Turn it off for authored rooms, and
		// "Seal" to also drop the sun + sky IBL so the room is dark until you light it.
		if (ImGui::Checkbox("Auto light rig", &s.autoLightRig))
			s.LoadSceneContent(s.currentScene, false);
		ImGui::SameLine();
		ImGui::Checkbox("Seal (no sun / sky)", &s.sealScene);
		ImGui::SliderFloat("Auto-seal interiors (GI)", &s.autoSeal, 0.f, 1.f, "%.2f");
		ImGui::Separator();

		ImGui::Checkbox("Deferred", &s.useDeferred);
		const char* views[] = { "Lit", "Albedo", "Normal", "Roughness", "Metalness",
		                        "Baked AO", "Emissive", "Depth", "SSAO" };
		ImGui::Combo("View", &s.gbufChannel, views, IM_ARRAYSIZE(views));
		ImGui::Checkbox("Perf overlay", &s.showPerfOverlay);

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
				ImGui::SliderFloat("Emissive strength", &dm.emissiveStrength, 0.f, 16.f, "%.2f");
				ImGui::InputTextWithHint("##dbgtgmat", "path/to/foo.tgmat", s.dbgTgmatPath, sizeof(s.dbgTgmatPath));
				ImGui::SameLine();
				if (ImGui::Button("Load .tgmat##dbg"))
					LoadTgmatInto(s.dbgTgmatPath, s.debugMat);
				ImGui::Checkbox("Emits light (area light proxy)", &s.debugBallEmitsLight);
				if (s.debugBallEmitsLight)
				{
					ImGui::SliderFloat("Emissive light gain", &s.debugEmissiveLightGain, 0.f, 0.15f, "%.3f");
					ImGui::Checkbox("Proxy casts shadow (costly)", &s.debugEmissiveCastShadow);
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
			else ImGui::TextDisabled("Sphere/Sphere.fbx not loaded");
		}
		if (s.IsBuiltinRoom() && ImGui::CollapsingHeader("Built-in room", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::DragFloat3("Room size", &s.roomSize.x, 10.f, 100.f, 20000.f, "%.0f"))
				s.LoadSceneContent(kBuiltinRoomScene, false);   // rebuild surfaces at the new size
			ImGui::Checkbox("Sealed (no sun / sky IBL)", &s.roomSealed);
			if (ImGui::Checkbox("Ceiling fill lamps", &s.roomLamps))
				s.LoadSceneContent(kBuiltinRoomScene, false);
			ImGui::SameLine();
			if (ImGui::SmallButton("Re-prime GI")) s.StartGiPrime();

			const char* faces[6] = { "Floor", "Ceiling", "Wall -X", "Wall +X", "Wall -Z", "Wall +Z" };
			ImGui::Combo("Surface", &s.roomEditSurface, faces, 6);
			int i = std::clamp(s.roomEditSurface, 0, 5);
			DeferredRenderer::DebugMaterial& rm = s.roomMat[i];
			ImGui::ColorEdit3("Base colour##room", rm.baseColor);
			ImGui::SliderFloat("Roughness##room", &rm.roughness, 0.f, 1.f, "%.3f");
			ImGui::SliderFloat("Metalness##room", &rm.metalness, 0.f, 1.f, "%.3f");
			ImGui::SliderFloat("AO##room", &rm.ao, 0.f, 1.f, "%.3f");
			ImGui::ColorEdit3("Emissive colour##room", rm.emissiveColor);
			ImGui::SliderFloat("Emissive strength##room", &rm.emissiveStrength, 0.f, 16.f, "%.2f");
			ImGui::InputTextWithHint("##roomtgmat", "path/to/foo.tgmat", s.roomTgmatPath, sizeof(s.roomTgmatPath));
			ImGui::SameLine();
			if (ImGui::Button("Load .tgmat##room"))
				LoadTgmatInto(s.roomTgmatPath, rm);
			if (ImGui::Button("Apply to all surfaces"))
				for (int k = 0; k < 6; ++k) s.roomMat[k] = rm;
		}
		if (ImGui::CollapsingHeader("Sun / directional", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Pitch", &s.sunPitch, -89.f, 89.f, "%.1f deg");
			ImGui::SliderFloat("Yaw",   &s.sunYaw, -180.f, 180.f, "%.1f deg");
			ImGui::ColorEdit3("Colour", s.sunColor);
			ImGui::SliderFloat("Intensity", &s.sunIntensity, 0.f, 4.f);
			ImGui::SliderFloat("Softness",  &s.sunSoftness, 0.f, 1.f);
		}
		if (ImGui::CollapsingHeader("Ambient / IBL"))
		{
			ImGui::ColorEdit3("Ambient", s.ambientColor);
			ImGui::SliderFloat("IBL scale", &s.ambientScale, 0.f, 4.f, "%.2f");
			static const char* kCubes[] = { "horizonCubeMap", "env_studio", "env_powerplant", "env_slipway" };
			if (ImGui::Combo("Cubemap", &s.cubemapIdx, kCubes, IM_ARRAYSIZE(kCubes)))
				s.fallbackCube = GraphicsEngine::GetInstance()->GetTextureManager()
					.GetTexture((std::string("Textures/") + kCubes[s.cubemapIdx] + ".dds").c_str(),
						TextureSrgbMode::None);
			ImGui::Checkbox("Reflection probe", &s.probeEnabled);
			ImGui::SliderInt("Probe interval", &s.probeInterval, 1, 240);
			if (ImGui::Button("Recapture now")) s.probeCountdown = 0;
			ImGui::DragFloat3("Probe pos", &s.probePos.x, 5.f);
			ImGui::DragFloat3("Probe box (half)", &s.probeBox.x, 5.f, 1.f, 100000.f);
		}
		if (ImGui::CollapsingHeader("Emissive GI", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled##gi", &s.giEnabled);
			ImGui::Text("%d x %d x %d = %d probes", s.giCx, s.giCy, s.giCz, s.giCx * s.giCy * s.giCz);
			ImGui::SliderFloat("Intensity##gi", &s.giIntensity, 0.f, 4.f, "%.2f");
			ImGui::SliderFloat("Hysteresis", &s.giHysteresis, 0.f, 0.99f, "%.2f");
			ImGui::Text(s.giPriming ? "priming... probe %d / %d" : "primed (%d probes)",
				s.giCursor, s.giCx * s.giCy * s.giCz);
			ImGui::SliderInt("Prime batch", &s.giPrimeBatch, 1, 32);
			ImGui::Checkbox("Keep updating (dynamic)", &s.giKeepUpdating);
			if (s.giKeepUpdating) ImGui::SliderInt("Trickle skip", &s.giFrameSkip, 1, 30);
			ImGui::DragFloat3("Volume origin", &s.giOrigin.x, 10.f);
			ImGui::DragFloat3("Probe spacing", &s.giSpacing.x, 5.f, 1.f, 100000.f);
			ImGui::Checkbox("Auto re-prime on light change", &s.giAutoReprime);
			if (ImGui::Button("Re-prime volume")) s.StartGiPrime();
			if (s.deferred) ImGui::Checkbox("Show GI term", &s.deferred->GetTunables().giViz);
		}

		DeferredRenderer::Tunables* tun = s.deferred ? &s.deferred->GetTunables() : nullptr;

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
		if (ImGui::CollapsingHeader("SSR", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled##ssr", &s.wantSSR);
			if (tun)
			{
				ImGui::SliderFloat("Max distance", &tun->ssrMaxDistance, 50.f, 4000.f, "%.0f");
				ImGui::SliderFloat("Thickness", &tun->ssrThickness, 2.f, 120.f, "%.0f");
				ImGui::SliderFloat("Roughness cutoff", &tun->ssrRoughnessCutoff, 0.05f, 1.f, "%.2f");
				ImGui::SliderFloat("Strength##ssr", &tun->ssrStrength, 0.f, 2.f, "%.2f");
				ImGui::SliderInt("March steps", &tun->ssrSteps, 8, 128);
				ImGui::SliderInt("Refine steps", &tun->ssrRefineSteps, 0, 8);
			}
		}
		if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled##ssao", &s.wantSSAO);
			if (tun)
			{
				ImGui::SliderFloat("AO radius", &tun->ssaoRadius, 4.f, 200.f);
				ImGui::SliderFloat("AO bias", &tun->ssaoBias, 0.f, 4.f);
				ImGui::SliderFloat("AO intensity", &tun->ssaoIntensity, 0.f, 4.f);
				ImGui::SliderFloat("AO power", &tun->ssaoPower, 0.5f, 4.f);
			}
		}
		if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Clustered culling", &s.wantClustered);
			ImGui::Checkbox("Show light markers", &s.showLightMarkers);
			ImGui::Text("%d local light(s)", (int)s.pointLights.size());
			ImGui::Separator();

			const float kR2D = 57.29578f, kD2R = 0.01745329f;
			for (int i = 0; i < (int)s.pointLights.size(); ++i)
			{
				Impl::LightExtra* ex = (i < (int)s.lightExtra.size()) ? &s.lightExtra[i] : nullptr;
				const bool isSpot = ex && ex->spotCosOuter > 0.f;
				ImGui::PushID(i);
				if (ImGui::TreeNodeEx("l", ImGuiTreeNodeFlags_DefaultOpen, "%s %d",
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
					ImGui::DragFloat("Range", &s.pointLights[i].range, 10.f, 20.f, 20000.f, "%.0f");
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
		if (ImGui::CollapsingHeader("Post FX", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled##postfx", &s.wantPostFx);
			if (tun)
			{
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
				ImGui::SeparatorText("Exposure");
				ImGui::Checkbox("Auto exposure", &tun->exposureAuto);
				if (tun->exposureAuto)
				{
					ImGui::SliderFloat("Key", &tun->exposureKey, 0.02f, 0.6f, "%.3f");
					ImGui::SliderFloat("Min", &tun->exposureMin, 0.01f, 2.f, "%.2f");
					ImGui::SliderFloat("Max", &tun->exposureMax, 1.f, 32.f, "%.1f");
					ImGui::SliderFloat("Adapt speed", &tun->exposureSpeed, 0.25f, 10.f, "%.2f");
				}
				else
				{
					ImGui::SliderFloat("Exposure", &tun->manualExposure, 0.05f, 8.f, "%.2f");
				}
				ImGui::SliderFloat("EV comp", &tun->exposureComp, -4.f, 4.f, "%.2f");
			}
		}
	}
	ImGui::End();

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

	// Interactive tuning panel (free-fly runs only) — updates s.* live.
	if (s.benchFrames == 0)
	{
		DrawDebugUI();
		s.DrawPerfOverlayImpl();
	}

	// Rebuild the sun / ambient from live state so slider tweaks take effect.
	s.dirLight.transform = Matrix4x4f::CreateFromRollPitchYaw(Vector3f{ s.sunPitch, s.sunYaw, 0.f });
	s.dirLight.color = Color{ s.sunColor[0] * s.sunIntensity, s.sunColor[1] * s.sunIntensity, s.sunColor[2] * s.sunIntensity };
	s.dirLight.softness = s.sunSoftness;
	s.ambient.color = Color{ s.ambientColor[0] * s.ambientScale, s.ambientColor[1] * s.ambientScale, s.ambientColor[2] * s.ambientScale };

	// A sealed built-in room gets no exterior sun and no sky IBL -- only the
	// ceiling lamps, the emissive orbit/debug spheres and GI light it.
	const bool sealedRoom = (s.IsBuiltinRoom() && s.roomSealed) || s.sealScene;
	if (sealedRoom)
	{
		s.dirLight.color = Color{ 0.f, 0.f, 0.f, 1.f };
		s.ambient.color  = Color{ 0.015f, 0.016f, 0.018f, 1.f };   // faint floor so corners aren't crushed
	}

	gss.SetCamera(s.camera);
	gss.SetDirectionalLight(s.dirLight);
	gss.ClearPointLights();

	// Reflection probe: re-capture every N frames, then point the IBL at it.
	if (s.probeEnabled && s.probePrefilter && !sealedRoom)
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
				s.sunPitch * 2.f + s.sunYaw * 1.3f + s.sunIntensity * 40.f
				+ (s.sunColor[0] + s.sunColor[1] * 2.f + s.sunColor[2] * 3.f) * 20.f
				+ (s.ambientColor[0] + s.ambientColor[1] + s.ambientColor[2]) * s.ambientScale * 50.f
				+ (float)s.cubemapIdx * 100.f
				+ (sealedRoom ? 777.f : 0.f) + (s.roomLamps ? 55.f : 0.f));
			if (h != s.giLightHash)
			{
				s.giLightHash = h;
				if (!s.giPriming) s.StartGiPrime();
			}
		}
		if (s.giPriming)
		{
			s.CaptureGiProbesImpl(ge);
		}
		else if (s.giKeepUpdating && --s.giSkipCount <= 0)
		{
			s.giSkipCount = std::max(1, s.giFrameSkip);
			s.CaptureGiProbesImpl(ge);
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
		s.deferred->SetShadowLight(s.dirLight.transform.GetForward(), s.sceneCenter, s.sceneExtents.Length());
		s.deferred->SetCamera(s.camera);
		// Box-parallax the IBL against the probe influence box when a probe is live.
		const bool boxOn = s.probeEnabled && s.probePrefilter && s.probePrefiltered.resource != nullptr;
		s.deferred->SetReflectionProbeBox(s.probePos, s.probeBox, boxOn);
		s.deferred->SetGiVolume(s.giOrigin, s.giSpacing, s.giCx, s.giCy, s.giCz,
			s.giIntensity, s.giEnabled && s.deferred->HasGi(),
			(s.giEnabled && !sealedRoom) ? s.autoSeal : 0.f);
	}

	gss.Push();
	gss.SetBlendState(BlendState::Disabled);
	gss.SetAlphaTestThreshold(0.33f);   // so masked decals cut out (opaque albedo is alpha=1)
	if (std::getenv("BENCH_NOCULLFACE") || s.IsBuiltinRoom())
		gss.SetRasterizerState(RasterizerState::NoFaceCulling);   // room planes are viewed from the inside

	const Frustum frustum = CalculateFrustum(s.camera);
	ModelDrawer& md = ge.GetModelDrawer();
	md.SetCullFrustum((s.frustumCull && s.models.size() > 1) ? &frustum : nullptr);

	s.gpu.BeginFrame();

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
				if (sp->IsBuiltinRoom() && dr->HasDebugMatShader())
				{
					// Procedural room: each surface gets its own fixed-param material.
					const ModelShader& dsh = dr->GetDebugMatShader();
					for (size_t k = 0; k < sp->roomSurfaces.size() && k < 6; ++k)
					{
						dr->BindDebugMaterial(sp->roomMat[k]);
						sp->roomSurfaces[k].Render(dsh);
					}
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
				drawTransparent = [mdp, sp]()
				{
					const ModelShader& psh = mdp->GetPbrShader();
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

	if (!s.screenshotPath.empty() && !s.screenshotTaken)
	{
		const int shotFrame = s.benchFrames > 3 ? s.benchFrames - 2 : 120;
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
