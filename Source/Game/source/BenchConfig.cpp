#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"
#include "BenchConfig.h"

namespace
{
	std::optional<std::string> EnvOptStr(const char* name)
	{
		const char* v = std::getenv(name);
		return v ? std::optional<std::string>(v) : std::nullopt;
	}
	std::optional<float> EnvOptFloat(const char* name)
	{
		const char* v = std::getenv(name);
		return v ? std::optional<float>((float)std::atof(v)) : std::nullopt;
	}
}

namespace BenchConfig
{
Run ReadRun()
{
	Run r;
	r.shotCount = std::max(1, EnvInt("BENCH_SHOT_COUNT", 1));
	r.freezeFrame = EnvInt("BENCH_FREEZE_FRAME", 0);
	r.taaResetFrame = EnvInt("BENCH_TAA_RESET_FRAME", -1);
	r.costSweep = EnvInt("BENCH_COST_SWEEP", 0) != 0;
	r.noCullFace = std::getenv("BENCH_NOCULLFACE") != nullptr;
	r.debugUi = EnvInt("BENCH_DEBUG_UI", 0) != 0;
	r.debugTab = EnvStr("BENCH_DEBUG_TAB", "");
	r.cubemap = EnvStr("BENCH_CUBEMAP", "horizonCubeMap");
	r.transparentMats = EnvStr("BENCH_TRANSPARENT_MATS", "glass,lamp_glass");
	r.camMode = EnvOptStr("BENCH_CAM");
	r.camFile = EnvOptStr("BENCH_CAMFILE");
	r.spinDeg = EnvOptFloat("BENCH_SPIN");
	r.orbitRadius = EnvOptFloat("BENCH_ORBIT");
	r.exposure = EnvOptFloat("BENCH_EXPOSURE");
	r.modelRotX = EnvOptFloat("BENCH_ROT_X");
	return r;
}

void ApplyStartupOverrides(GameWorld::Impl& s)
{
	s.bench = ReadRun();
	s.benchFrames  = EnvInt("BENCH_FRAMES", 0);
	s.warmupFrames = EnvInt("BENCH_WARMUP", 60);
	s.sponzaCopies = std::max(1, EnvInt("BENCH_SPONZA_COPIES", 1));
	s.reportPath   = EnvStr("BENCH_REPORT", "bench_report.json");

	if (s.bench.modelRotX) s.modelRotX = *s.bench.modelRotX;
	s.sealScene    = EnvInt("BENCH_SEAL", 0) != 0;
	s.autoSeal = std::clamp(EnvFloat("BENCH_AUTOSEAL", s.autoSeal), 0.f, 1.f);
	s.currentScene = EnvStr("BENCH_SCENE", "");
}

void ApplyContentOverrides(GameWorld::Impl& s)
{
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
	// Preview sphere material: a .tgmat file and/or a surface type
	// (Opaque, Masked, Transparent) for testing glass and cutouts.
	if (const char* path = std::getenv("BENCH_MATBALL_TGMAT")) s.LoadDebugMaterial(path);
	if (const char* surface = std::getenv("BENCH_MATBALL_SURFACE")) s.debugMat.surfaceType = surface;
	s.debugMat.opacity = std::clamp(EnvFloat("BENCH_MATBALL_OPACITY", s.debugMat.opacity), 0.f, 1.f);
	s.debugMat.roughness = std::clamp(EnvFloat("BENCH_MATBALL_ROUGHNESS", s.debugMat.roughness), 0.f, 1.f);
	s.debugMat.metalness = std::clamp(EnvFloat("BENCH_MATBALL_METALNESS", s.debugMat.metalness), 0.f, 1.f);
	s.debugMat.ior = std::max(1.0f, EnvFloat("BENCH_MATBALL_IOR", s.debugMat.ior));
	s.debugMat.refractionScale = std::max(0.0f, EnvFloat("BENCH_MATBALL_REFRACTION", s.debugMat.refractionScale));
	if (int oc = EnvInt("BENCH_ORBITBALLS", 0))
	{
		s.showOrbitBalls  = oc > 0;
		s.orbitBallCount  = std::clamp(oc, 1, GameWorld::Impl::kMaxOrbitBalls);
		s.orbitPathRadius = EnvFloat("BENCH_ORBIT_RADIUS", s.orbitPathRadius);
		s.orbitSpeed = EnvFloat("BENCH_ORBIT_SPEED", s.orbitSpeed);
		s.orbitBallRadius = EnvFloat("BENCH_ORBIT_BALLRAD", s.orbitBallRadius);
	}
	s.probeEnabled  = EnvInt("BENCH_PROBE", 1) != 0;
	s.probeInterval = std::max(1, EnvInt("BENCH_PROBE_INTERVAL", 45));
	if (const char* pp = std::getenv("BENCH_PROBE_POS"))
	{
		float x = 0, y = 0, z = 0;
		if (sscanf(pp, "%f,%f,%f", &x, &y, &z) == 3) s.probePos = { x, y, z };
	}
	s.giEnabled = EnvInt("BENCH_GI", 1) != 0;
	s.giPrimeBatch = std::max(1, EnvInt("BENCH_GI_PRIME", 8));
	s.giBatchProbes = EnvInt("BENCH_GI_BATCH", 1) != 0;
	if (auto* dr = GraphicsEngine::GetInstance() ? &GraphicsEngine::GetInstance()->GetDeferredRenderer() : nullptr)
		dr->GetTunables().giViz = EnvInt("BENCH_GI_VIZ", 0) != 0;
}

void ApplyWorldOverrides(GameWorld::Impl& s)
{
	s.useDeferred = EnvInt("BENCH_DEFERRED", 1) != 0;
	s.dxrRenderer = EnvInt("BENCH_DXR_RENDERER", 1) != 0;
	s.gbufChannel = std::clamp(EnvInt("BENCH_GBUF", 0), 0, 9);
	s.wantClustered = EnvInt("BENCH_CLUSTERED", 1) != 0;
	s.wantSSAO      = EnvInt("BENCH_SSAO", 1) != 0;
	s.wantShadows   = EnvInt("BENCH_SHADOWS", 1) != 0;
	s.wantPostFx    = EnvInt("BENCH_POSTFX", 1) != 0;
	s.wantLocalShadows = EnvInt("BENCH_LOCAL_SHADOWS", 1) != 0;
	s.wantSSR       = EnvInt("BENCH_SSR", 1) != 0;
	s.sunPitch = EnvFloat("BENCH_SUN_PITCH", s.sunPitch);
	s.sunYaw = EnvFloat("BENCH_SUN_YAW", s.sunYaw);
	if (const char* v = std::getenv("BENCH_SUN_INTENSITY")) s.sunIlluminanceLux = std::max(0.f,(float)atof(v)) * 100000.f;
	if (const char* v = std::getenv("BENCH_SUN_LUX")) s.sunIlluminanceLux = std::max(0.f,(float)atof(v));
	if (const char* v = std::getenv("BENCH_SUN_KELVIN")) { s.sunTemperatureK = (float)atof(v); s.sunUseTemperature = true; }
	s.screenshotPath = EnvStr("BENCH_SCREENSHOT", "");
	s.shotFrame = EnvInt("BENCH_SHOT_FRAME", 0);
	s.camOrbitHeight = EnvFloat("BENCH_ORBIT_HEIGHT", -1.f);
	s.ambientScale = EnvFloat("BENCH_AMBIENT", s.ambientScale);
	s.giProbeBudget = std::clamp(EnvInt("BENCH_GI_PROBES_PER_FRAME", s.giProbeBudget), 0, 256);
}

void ApplyRendererOverrides(DeferredRenderer::Tunables& tun)
{
	tun.shadowShowCascades = EnvInt("BENCH_SHADOW_VIZ", 0) != 0;
	tun.bloomEnabled = EnvInt("BENCH_BLOOM", 1) != 0;
	tun.bloomIntensity = EnvFloat("BENCH_BLOOM_INTENSITY", tun.bloomIntensity);
	tun.dxrLightingView = EnvInt("BENCH_LIGHTING_VIEW", 0);
	tun.dxrTextureFiltering = EnvInt("BENCH_RAY_TEXTURE_FILTER", 1) != 0;
	tun.fogEnabled = EnvInt("BENCH_FOG", 1) != 0;
	tun.fogDensity = EnvFloat("BENCH_FOG_DENSITY", tun.fogDensity);
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
	// Ray Reconstruction replaces NRD; it runs at whatever BENCH_DLSS_MODE asks
	// for (1 = DLAA / native, 2..5 = DLSS upscaling), so the two are independent.
	if (EnvInt("BENCH_DXR_DENOISER", 0) != 0)
	{
		tun.rayReconstructionEnabled = true;
		if (tun.dlssMode == 0) { tun.dlssMode = 1; tun.dlaaEnabled = true; }
	}
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
	tun.preExposure = EnvInt("BENCH_PRE_EXPOSURE", tun.preExposure ? 1 : 0) != 0;
	if (const char* v = std::getenv("BENCH_SKY_NITS")) tun.skyLuminanceNits = std::max(0.f, (float)atof(v));
	tun.exposureComp = EnvFloat("BENCH_EV_COMP", tun.exposureComp);
	if (const char* v = std::getenv("BENCH_APERTURE")) tun.cameraAperture = std::max(0.5f, (float)atof(v));
	if (const char* v = std::getenv("BENCH_SHUTTER")) tun.cameraShutter = std::max(1e-6f, (float)atof(v));
	if (const char* v = std::getenv("BENCH_ISO")) tun.cameraIso = std::max(1.f, (float)atof(v));
	tun.contactShadows = EnvInt("BENCH_CONTACT", 1) != 0;
	tun.contactViz = EnvInt("BENCH_CONTACT_VIZ", 0) != 0;
	tun.localShadowViz = EnvInt("BENCH_LOCALSH_VIZ", 0) != 0;
	tun.ssrStrength = EnvFloat("BENCH_SSR_STRENGTH", tun.ssrStrength);
}
}
