#pragma once

// GameWorld internals, shared by GameWorld*.cpp. Not part of the public API.

#include "GameWorld.h"
#include <age/render/CubemapPrefilter.h>
#include <cstdio>
#include <age/render/DeferredRenderer.h>
#include <age/render/RayTracingMaterialTable.h>
#include <age/graphics/RenderTarget.h>
#include <age/graphics/DepthBuffer.h>
#include <age/render/RenderGraph.h>
#include <age/render/RenderResourcePool.h>
#include <age/graphics/GraphicsEngine.h>
#include <age/graphics/GraphicsStateStack.h>
#include <age/graphics/DX11.h>
#include <age/graphics/Camera.h>
#include <age/graphics/AmbientLight.h>
#include <age/graphics/DirectionalLight.h>
#include <age/graphics/PointLight.h>
#include <age/math/Photometry.h>
#include <age/debugging/CpuProfiler.h>
#include <age/drawers/ModelDrawer.h>
#include <age/render/GpuProfiler.h>
#include <age/model/Model.h>
#include <age/model/ModelFactory.h>
#include <age/model/ModelInstance.h>
#include <age/texture/TextureManager.h>
#include <age/texture/texture.h>
#include <age/input/InputManager.h>
#include <age/application.h>
#include <age/settings/settings.h>
#include <age/log/Log.h>
#include <age/EngineDefines.h>
#include <age/physics/PhysicsWorld.h>
#include <age/script/ScriptRuntimeInstance.h>
#include <age/script/Contexts/GameScriptContext.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <DirectXTex/ScreenGrab/ScreenGrab11.h>
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
#include "SceneFiles.h"
#include "GiProbeScheduler.h"
#include <age/shaders/ModelShader.h>
#include <age/material/MaterialAsset.h>
#include "BenchConfig.h"

using namespace Ag;
using namespace GameScene;

struct GameWorld::Impl
{
	// ---- config
	BenchConfig::Run bench;   // one-shot BENCH_* settings, read once in Init()
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
	// Neutral by default: the procedural sky (DeferredRenderer::Tunables::
	// proceduralSkyEnabled) already produces physically correct sky color and
	// brightness, and this multiplies straight on top of it (EvaluateAmbiance's
	// AmbientLightColor.rgb *) -- a tinted default here would silently
	// re-color/dim an otherwise-correct sky. Still a free artistic override.
	float ambientColor[3] = { 1.0f, 1.0f, 1.0f };
	float ambientScale = 1.0f;   // BENCH_AMBIENT env; scales the ambient/IBL term
	int   cubemapIdx = 0;        // panel Cubemap combo

	// --- reflection probe (Phase 5 stage A): one probe, re-captured every N frames ---
	std::unique_ptr<CubemapPrefilter> probePrefilter;
	CubemapData probeBase, probePrefiltered;
	// The authored sky stays at mip 0 in this cube, while its remaining mips
	// contain GGX specular and cosine-convolved diffuse lighting. Unlike the
	// dynamic local probe, it is safe to use for the DXR sky as well as IBL.
	CubemapData worldEnvironmentPrefiltered;
	// Prefiltered environment cubemaps replaced by a rebuild, kept alive until
	// every frame in flight that was recorded against them has retired. The
	// old rebuild Reset() the live cubemap first, so the GPU read a destroyed
	// texture for a frame or two: that was the black / raw-blue sky flash on
	// every sky refresh. Second = the frame at which it may be destroyed.
	std::vector<std::pair<CubemapData, int>> retiredEnvironmentCubemaps;
	void ReleaseRetiredEnvironmentCubemaps();
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
	int   giPrimeBatch = 8;     // probes/frame while first populating the volume
	int   giTrickle = 4;        // bounded RT probes/frame after priming
	int   giFrameSkip = 1;      // DXR updates every frame; raster retains a cheaper cadence below
	int   giSkipCount = 0;
	bool  giKeepUpdating = true;
	bool  giAutoReprime = true;   // re-prime when the sun / ambient changes
	float giLightHash = 0.f;
	GiProbeScheduler giScheduler;   // which probes to refresh each frame
	static constexpr int kGiFaceRes = 16;
	int   probeInterval = 30;      // recapture cadence (frames); BENCH_PROBE_INTERVAL
	int   probeCountdown = 0;
	static constexpr int kProbeRes = 256;

	bool RebuildWorldEnvironmentPrefilter();

	// --- material preview debug sphere ---
	ModelInstance debugBall;
	bool debugBallValid = false;
	bool showDebugBall = false;
	MaterialAsset debugMat;              // constants only unless maps are set
	bool pillarMaterialOverride = true;
	MaterialAsset pillarMat;
	uint32_t debugMaterialIndex = 0;     // RayTracingMaterialTable slots, see UpdateDebugMaterials
	uint32_t pillarMaterialIndex = 0;
	std::string debugMatLoadedMaps;      // maps currently bound to the preview spheres
	bool DebugBallIsGlass() const { return debugMat.IsTransparent(); }
	Vector3f debugBallPos{ 0.f, 0.f, 0.f };
	float debugBallRadius = 45.f;      // desired world-space radius
	float debugBallModelRadius = 1.f;  // FBX bounds radius (from the model)
	float debugBallModelExtent = 1.f;  // FBX half-size of the sphere itself; the bounds radius is larger (box diagonal)
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

	void ComputeBounds(const std::shared_ptr<Model>& model);

	// With a saved viewpoint (bench_camera.json): "fixed" holds it exactly (still
	// image), "spin" holds the position and sweeps yaw in place, "orbit" circles
	// the saved point. Default when a camera is saved is "spin". No saved camera
	// -> auto orbit around a point low in the scene.
	// "bob" holds the saved position, pitches it by camBobPitch and moves it
	// up and down by +/- camBobCm over the run (vertical translation only, for
	// sky / cloud reprojection tests).
	enum class CamMode { Fixed, Spin, Orbit, Bob } camMode = CamMode::Orbit;
	float camSpinDeg = 35.f;   // BENCH_SPIN: half-sweep for "spin" mode
	float camBobCm = 300.f;    // BENCH_BOB: half-amplitude of the vertical bob, cm
	int   camBobHoldFrames = 200; // BENCH_BOB_HOLD: frames held still (history converges, first screenshot) before the bob starts
	float camBobPitch = -35.f; // BENCH_BOB_PITCH: degrees added to the saved pitch (negative = up, matching the orbit camera)
	// Set when the matching BENCH_SUN_* env var was present, so the scene's
	// own .tgs lighting does not overwrite an explicit request.
	float benchSunPitch = 0.f, benchSunYaw = 0.f, benchSunLux = 0.f, benchSunKelvin = 0.f;
	bool sunPitchOverridden = false, sunYawOverridden = false;
	bool sunLuxOverridden = false, sunKelvinOverridden = false;
	void ApplySunOverrides();
	float camOrbitHeight = -1.f;  // BENCH_ORBIT_HEIGHT: fraction of sceneExtents.y; <0 = oscillate
	bool  orbitRoom = false;   // BENCH_CAM=room: orbit the scene centre even with a saved camera

	void SetScriptedCamera();

	void UpdateFreeFly(float dt);

	std::string camFile = "bench_camera.json";
	bool camLoaded = false;   // a saved viewpoint is available

	void SaveCamera();

	bool LoadCamera();

	// Local lights are scene data. This reads the selected .tgs's
	// `lighting.lights` array only; a scene without that array has no local lights.
	bool LoadLights(const std::string& file, float sceneRadius, float exposure);

	std::string currentScene = "TEST";

	// Materials (GameWorldMaterials.cpp). One registration path for scene
	// .tgmat instances and the preview spheres, shared by raster and DXR.
	uint32_t RegisterMaterial(const std::string& aName, const MaterialAsset& aMaterial,
		const TextureResource* const* someTextures);
	void ApplySceneMaterial(ModelInstance& anInstance, int aMesh, const std::string& aMaterialPath,
		const MaterialAsset& aMaterial);
	void UpdateDebugMaterials();
	bool LoadDebugMaterial(const char* aPath);
	void DrawDebugBalls(const ModelShader& aShader) const
	{
		if (showDebugBall) debugBall.Render(aShader);
		if (showOrbitBalls)
		{
			const int n = std::clamp(orbitBallCount, 1, kMaxOrbitBalls);
			for (int i = 0; i < n && i < (int)orbitBalls.size(); ++i) orbitBalls[i].Render(aShader);
		}
	}

	bool IsPillarTest() const { return fs::path(currentScene).stem().string() == "PillarTest"; }

	// Begin a fresh GI prime: wipe the SH buffer so the sweep is deterministic
	// (no history blended in -- fixes "re-prime gives a different result each time").
	void StartGiPrime();

	// (Re)load everything that depends on the scene: instances, sub-mesh split,
	// bounds, the light rig, and the start camera. Safe to call at runtime to
	// switch scenes (free-fly). aEnv = honour BENCH_* overrides (Init only).
	bool LoadSceneContent(const std::string& sceneName, bool aEnv);
	void RecomputeGiVolume();

	void WriteReport();

	void BuildCostProbes();

	void StartCostSweep();

	void StopCostSweep();

	static float Percentile(std::vector<float> v, float q);
	static float Median(std::vector<float> v) { return Percentile(std::move(v), 0.5f); }

	// Moves to the next probe with something to turn off; skipped ones are
	// recorded as such.
	void AdvanceCostProbe();

	// Each probe measures "on" and then "off" back to back, so slow drift
	// (thermals, background load) cancels out of the difference.
	void StepCostSweep();

#ifndef _RETAIL
	static const char* ScopeName(const char* n) { return n ? n : "?"; }
	static const char* ScopeName(const std::string& n) { return n.c_str(); }

	template <class Stats>
	static void DrawScopeTable(const char* id, const std::vector<const Stats*>& stats, float frameMs, std::string& report);

	static void DrawHistory(const char* label, const float* history, int offset, int count, float target);

	void DrawProfilerTab();

	// --- physics test on the debug sphere (GameWorldPhysics.cpp) ---
	Ag::PhysicsWorld physics;
	Ag::PhysicsBodyId physicsBall;
	Ag::PhysicsBodyId physicsFloor;      // only when the scene has no static collision
	bool physicsActive = false;
	bool physicsSavedFollowCam = true;
	bool physicsSavedShowBall = false;
	Vector3f physicsStartPos{ 0.f, 0.f, 0.f };
	float physicsGravity = 9.81f;         // m/s^2
	float physicsMass = 0.f;              // kg, 0 = from volume
	float physicsRestitution = 0.5f;
	float physicsFriction = 0.5f;
	float physicsFloorOffset = 0.f;       // cm above the bottom of the scene bounds
	// BENCH_PHYSICS=1: start the simulation as soon as the scene has physics and log the
	// first prop's height once a second, for checking without the UI.
	bool physicsAutoStart = false;
	float physicsLogTimer = 0.f;
	int physicsLogCount = 0;
	// --- per-object scripts (GameWorldScripts.cpp) ---
	// An object's script is the event graph inside its .tgo. It starts when the scene loads
	// and runs every frame.
	struct SceneScriptObject
	{
		size_t instance = 0;                                   // index into models
		std::string name;                                      // for logs
		std::unique_ptr<Ag::ScriptRuntimeInstance> graph;
		std::unordered_map<StringId, Ag::Property> dynamicProperties;
		std::unordered_map<StringId, Ag::Property> staticProperties;

		SceneScriptObject() = default;
		SceneScriptObject(const SceneScriptObject&) = delete;
		SceneScriptObject& operator=(const SceneScriptObject&) = delete;
		SceneScriptObject(SceneScriptObject&&) noexcept = default;
		SceneScriptObject& operator=(SceneScriptObject&&) noexcept = default;
	};
	std::vector<SceneScriptObject> sceneScripts;
	bool scriptsEnabled = true;
	int scriptFrame = 0;
	void ClearSceneScripts();
	void RegisterSceneScripts(const GameScene::SceneEntry& entry, size_t instanceIndex);
	void UpdateSceneScripts(float deltaSeconds);
	void DispatchContactEvents();   // raises On Collision Enter / On Trigger Enter from the physics contacts

	// --- Characters (GameWorldScripts.cpp) ---
	// A character is created when the simulation starts and destroyed on Reset, like the props.
	struct SceneCharacterObject
	{
		size_t instance = 0;
		Ag::PhysicsCharacterDesc desc;
		Ag::PhysicsCharacterId id;
		Matrix4x4f startTransform;
	};
	std::vector<SceneCharacterObject> sceneCharacters;
	void RegisterSceneCharacter(const GameScene::SceneEntry& entry, const Matrix4x4f& worldTransform, size_t instanceIndex);

	// --- Camera components (GameWorldScripts.cpp) ---
	struct SceneCameraObject
	{
		size_t instance = 0;
		Vector3f offset{ 0.f, 170.f, 0.f };
		float fov = 90.f;
		float pitch = 0.f;
	};
	std::vector<SceneCameraObject> sceneCameras;
	int activeSceneCamera = -1;          // index into sceneCameras, -1 = free-fly
	float cameraFov = 90.f;
	void RegisterSceneCamera(const GameScene::SceneEntry& entry, size_t instanceIndex);
	void SetSceneCameraActive(int index);
	void UpdateSceneCamera();
	void ApplyCameraFov(float fov);

	bool showPhysicsWireframe = false;    // draw collision edges (green static, orange awake, blue asleep)
	float physicsWireRadius = 3000.f;     // only near the camera; a level mesh has far too many edges
	Ag::PhysicsDebugLines physicsWireLines;
	void DrawPhysicsOverlay();
	bool physicsIncludeBall = true;       // drop the debug sphere with the scene's props
	void StartPhysicsTest();
	void ResetPhysicsTest();
	void UpdatePhysicsTest(float deltaSeconds);
	void DrawPhysicsTab();

	// Scene objects with collision (from the .tgo Model "Collision" setting or a
	// Collider component). Static ones get a body when the scene loads; dynamic ones
	// (Rigidbody) get theirs on Start so Reset can put them back.
	struct ScenePhysicsObject
	{
		size_t instance = 0;                 // index into models
		Ag::PhysicsBodyDesc desc;
		Ag::PhysicsBodyId body;
		Matrix4x4f startTransform;
		Vector3f scale{ 1.f, 1.f, 1.f };
		bool dynamic = false;
	};
	std::vector<ScenePhysicsObject> scenePhysicsObjects;
	std::unordered_map<std::string, Ag::PhysicsShapeId> scenePhysicsShapes;
	int scenePhysicsStaticCount = 0;
	void ClearScenePhysics();
	void RegisterScenePhysics(const GameScene::SceneEntry& entry, const std::shared_ptr<Model>& model,
		const Matrix4x4f& worldTransform, size_t instanceIndex);
#endif

	// Always-on GPU/CPU timing HUD (free-fly only). Reads the per-pass scopes the
	// RenderGraph pushes into the profiler.
	void DrawPerfOverlayImpl();

	// Render the scene into a cubemap at probePos, GGX-prefilter it, and point the
	// ambient IBL at the result. Uses fallbackCube for its own sky/IBL so it does
	// not feed back on itself.
	void CaptureProbeImpl(GraphicsEngine& ge);

	// Capture the GI probes giScheduler picks for this frame, project each into
	// the deferred renderer's SH volume. Same tiny forward capture as the reflection
	// probe; multi-bounce comes for free because DrawPbr already samples the volume.
	void CaptureGiProbesImpl(GraphicsEngine& ge);
};
