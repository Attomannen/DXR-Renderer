#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <wrl/client.h>
#include <tge/math/vector2.h>
#include <tge/math/vector3.h>
#include <tge/math/Matrix4x4.h>
#include <tge/graphics/RenderTarget.h>
#include <tge/graphics/Camera.h>
#include <tge/rhi/ConstantBuffer.h>
#include <tge/rhi/StructuredBuffer.h>

namespace Tga
{
	class ModelShader;
	class RenderGraph;
	class Camera;
	struct VertexShader;
	struct PixelShader;
	struct ComputeShader;
	
	namespace rhi::dx12 { 	class NrdWrapper; }
	class GpuProfiler;

	// One point light for the deferred structured-buffer path. Layout must match
	// GpuLight in DeferredLightingPS.hlsl (two float4s). radius 0 = punctual.
	struct DeferredLight
	{
		float position[3];
		float range;
		float color[3];
		float radius;          // 0 = point, >0 = soft area
		float spotDir[3];      // cone axis (light -> scene), normalized
		float spotCosOuter;    // > 0 => spot light; <= 0 => point/area
		float spotCosInner;
		float shadowSlot;      // atlas tile base, -1 = no shadow (Stage 2)
		float _pad[2];
	};

	// Engine-owned deferred renderer. GraphicsEngine creates one sized to the
	// render resolution; games / the editor drive it through
	// GraphicsEngine::GetDeferredRenderer().
	//
	//   BeginGeometryPass()  -> bind 4-MRT G-buffer + the engine depth buffer, clear
	//   (caller draws opaque geometry with GetGeometryShader())
	//   ResolveLighting()    -> fullscreen PBR resolve, G-buffer -> HDR target
	//   Composite()          -> tonemap HDR -> the currently bound target (backbuffer)
	//   DebugBlit(channel)   -> show one G-buffer channel instead of lighting
	//
	// G-buffer: albedo (RGBA8), normal (RGBA16F), material ORM+emissiveMask (RGBA8),
	// emissive (R11G11B10F). Depth is the shared Tga::DX11::DepthBuffer.
	class DeferredRenderer
	{
	public:
		DeferredRenderer();
		~DeferredRenderer();

		bool Init(Vector2ui aResolution);
		bool IsReady() const { return myReady; }

		// Re-create the G-buffer / HDR targets at a new size (window resize).
		bool OnResize(Vector2ui aResolution);
		Vector2ui GetResolution() const { return myResolution; }
		// DXR temporal inputs: previous-minus-current pixels, and standard D3D depth [0,1].
		rhi::SrvHandle GetMotionVectorsSrv() const { return myTemporalSrv[0]; }
		rhi::SrvHandle GetTemporalDepthSrv() const { return myTemporalSrv[1]; }
		rhi::SrvHandle GetMotionValiditySrv() const { return myTemporalSrv[2]; }
		Vector2f GetProjectionJitterPixels() const { return myTaaJitter; }
		void SetRaySceneStationary(bool stationary) { myRaySceneStationary = stationary; }
		void ResetTemporalHistory() { myTemporalHistoryValid = false; myTaaHistoryValid = false; myTaaFrameIndex = 0; myNrdHistoryValid = false; }

		const ModelShader& GetGeometryShader() const { return *myGeometryShader; }
		// Forward glass uses the opaque HDR snapshot made immediately before the
		// transparent pass.  It deliberately has its own shader so ordinary alpha
		// blended materials do not accidentally sample a scene color target.
		bool HasGlassShader() const { return myGlassShader != nullptr; }
		const ModelShader& GetGlassShader() const { return *myGlassShader; }


		// Register this frame's deferred passes on the graph:
		//   geometry (aDrawOpaque fills the G-buffer) -> lighting -> transparent
		//   (aDrawTransparent, alpha-blended into HDR over the lit result, depth
		//   read-only) -> composite. aDebugChannel > 0 replaces lighting/transparent
		//   /composite with a single G-buffer debug blit.
		void BuildFrame(RenderGraph& aGraph,
		                const std::function<void()>& aDrawOpaque,
		                const std::function<void()>& aDrawTransparent = {},
		                const std::function<void(const Camera&)>& aDrawShadowCasters = {},
		                int aDebugChannel = 0);

		// Lower-level pass steps (BuildFrame drives these; also usable directly).
		void BeginGeometryPass();
		void ResolveLighting();
		void RenderSSR();      // reflection ray-march -> mySsrTex -> additive into HDR
		void RenderPostFx();   // bloom mip chain + auto-exposure adaptation
		void Composite();
		void DebugBlit(int aChannel);   // 1..7, see DeferredDebugPS.hlsl

		// Structured-buffer point lights for the deferred resolve (bypasses the
		// engine's 8-light cbuffer cap). Upload once per frame; the count is
		// clamped to GetMaxLights().
		static constexpr int kMaxLights = 1024;
		int  GetMaxLights() const { return kMaxLights; }
		void UploadLights(const DeferredLight* aLights, int aCount);
		int  GetLightCount() const { return myLightCount; }

		// Clustered light culling. Feed the current camera each frame; BuildFrame
		// then runs a compute pass that bins lights into froxel clusters and the
		// lighting resolve only loops each pixel's cluster. Toggle off = brute force.
		void SetCamera(const Camera& aCamera);
		void SetClustered(bool aOn) { myClusteredWanted = aOn; }
		bool IsClustered() const { return myClusteredWanted && myClusterCS != nullptr; }

		// Screen-space AO: a compute-free fullscreen occlusion + depth-aware blur
		// pass pair, folded into the lighting resolve's ambient term.
		void SetSSAO(bool aOn) { mySsaoWanted = aOn; }
		bool IsSSAO() const { return mySsaoWanted && mySsaoPs != nullptr && mySsaoBlurPs != nullptr; }

		// Screen-space reflections: a view-space ray-march of the reflection ray
		// against the depth buffer, added into the HDR target after lighting.
		void SetSSR(bool aOn) { mySsrWanted = aOn; }
		bool IsSSR() const { return mySsrWanted && mySsrPs != nullptr && mySsrApplyPs != nullptr; }

		// Box-projected reflection probe for the IBL specular (b12). enabled=false
		// -> infinite-cubemap behaviour. Fed each frame by the game.
		void SetReflectionProbeBox(const Vector3f& aCenter, const Vector3f& aHalfExtents, bool aEnabled);

		// Stage-1 emissive-GI irradiance volume: a grid of L2-SH probes. The game
		// captures probes (small cubes) and calls GiProjectProbe() to fold each one
		// into the SH buffer; the lighting resolve then samples it (b13 / t22).
		static constexpr int kMaxGiProbes = 4096;
		bool HasGi() const { return myGiProjectCS != nullptr && myGiShBuffer.IsValid(); }
		int  GetMaxGiProbes() const { return kMaxGiProbes; }
		void SetGiVolume(const Vector3f& aOrigin, const Vector3f& aSpacing,
		                 int aCx, int aCy, int aCz, float aIntensity, bool aEnabled,
		                 float aAutoSealStrength = 0.f);
		// Environment used only for sky -> surface -> probe diffuse transport in
		// the inline DXR GI pass.  It is intentionally not a replacement for the
		// final resolve's primary IBL.
		void SetGiEnvironment(rhi::SrvHandle aSrv, const Vector3f& aTint, bool aEnabled);

		// Procedural sky (see DeferredRendererSky.cpp): recomputes the LUTs and
		// re-renders the base sky cubemap only when the sun direction or an
		// atmosphere tunable actually changed since the last call -- cheap to
		// call every frame. aSunDirToLight and aSunIlluminance use the same
		// convention as SetShadowLight / the DXR sun tunables (normalised
		// ground->sun direction; tint*intensity radiance). aNightSkyCubeSrv is
		// the authored cubemap blended in as stars once the sun sets (empty =
		// none). Returns true the frame it actually regenerated (callers use
		// that to know when to re-run GeneratePrefilteredCubemap on the result).
		bool UpdateProceduralSky(const Vector3f& aSunDirToLight, const Vector3f& aSunIlluminance,
		                         float aCameraHeightCm, rhi::SrvHandle aNightSkyCubeSrv);
		rhi::SrvHandle GetProceduralSkyCubemapSrv() const { return mySkyCubemapSrv; }
		uint32_t GetProceduralSkyCubemapResolution() const { return kSkyCubemapRes; }

		// GPU profiler that sub-pass scopes report into (null: CPU timing only).
		void SetProfiler(GpuProfiler* aProfiler) { myProfiler = aProfiler; }
		// Average luminance (scene units) of the environment's sky hemisphere,
		// measured on the GPU whenever the environment changes; < 0 until known.
		float GetEnvironmentAverageLuminance() const { return myEnvAverageLuminance; }
		void ClearGi();   // zero the whole SH buffer -- call before a fresh (re-)prime
		void GiProjectProbe(rhi::SrvHandle aCubeSrv, int aProbeIndex,
		                    float aHysteresis, int aFaceRes);

		// Ray-traced alternative to GiProjectProbe: shoots aRayCount rays from
		// aProbePos over the full sphere and shades each hit with the SAME
		// analytic model DxrLightingCS.hlsl uses (real textures, real shadow
		// rays, real point/spot lights) instead of relying on a rasterized
		// cubemap capture -- a drop-in replacement that writes the identical SH
		// buffer GiProjectProbe does, without re-rendering the scene 6 times per
		// probe. DXR-only; call sites should check HasGiRT() first (mirrors
		// HasGi()'s pattern) since this is silently a no-op otherwise.
		bool HasGiRT() const { return myGiTraceCS != nullptr && myGiShBuffer.IsValid(); }

		// One probe to refresh. GiProjectProbeBatchRT traces a whole array of
		// these in a single Dispatch, one thread group per entry.
		struct GiProbeBatchEntry
		{
			Vector3f position;
			int index = 0;
		};
		// The batched form is the real entry point. Tracing probes one at a
		// time meant Dispatch(1,1,1) -- a single 64-thread group, i.e. one SM
		// of the whole GPU, with a full UAV barrier drain between each probe,
		// so a batch of N probes ran N times serially at ~1/60th occupancy.
		// One dispatch of N groups is the same per-probe math with the GPU
		// actually filled.
		//
		// Precondition for batching: probes within one dispatch have no
		// ordering guarantee relative to each other, so GiTraceInlineCS must
		// not read the GiSH volume it is concurrently writing. That holds
		// today -- its infinite-bounce feedback calls EvaluateDxrGi, whose t2
		// (gGiSH) and b13 (volume dimensions) are deliberately not bound on
		// this path, so it returns 0 before touching the buffer. If that
		// feedback is ever wired up for real, it needs double-buffered probe
		// state (read last frame's, write this frame's), not a return to
		// per-probe dispatches.
		void GiProjectProbeBatchRT(const GiProbeBatchEntry* aEntries, int aCount,
		                           float aHysteresis, int aRayCount = 64,
		                           float aFireflyClamp = 12.0f);
		// Single-probe convenience wrapper; forwards to the batched path so the
		// two cannot drift apart.
		void GiProjectProbeRT(const Vector3f& aProbePos, int aProbeIndex,
		                      float aHysteresis, int aRayCount = 64,
		                      float aFireflyClamp = 12.0f);

		// Cascaded shadow maps for the directional light. Feed the light direction
		// + scene bounds each frame (after SetCamera); BuildFrame's shadow pass then
		// renders kNumCascades depth cascades via the caller's draw fn, and the
		// lighting resolve PCF-samples them onto the directional term.
		static constexpr int kNumCascades = 4;

		// Point / spot light shadows: the highest-influence casters render into a
		// shared depth atlas each frame. Selection is deliberately camera-independent
		// so authored shadows do not pop or follow the player.
		static constexpr int kLocalAtlasRes  = 4096;
		static constexpr int kLocalTilePx    = 512;
		static constexpr int kLocalTilesRow  = kLocalAtlasRes / kLocalTilePx;   // 8
		static constexpr int kLocalTileCount = kLocalTilesRow * kLocalTilesRow; // 64
		static constexpr int kMaxShadowLights = 8;
		void SetLocalShadows(bool aOn) { myLocalShadowsWanted = aOn; }
		bool IsLocalShadows() const { return myLocalShadowsWanted && myShadowShader && myLocalAtlasSrv.IsValid(); }

		void SetShadows(bool aOn) { myShadowsWanted = aOn; }
		bool IsShadows() const { return myShadowsWanted && myShadowShader != nullptr && myShadowDsvs[0].IsValid(); }
		void SetDxrSunShadows(bool aOn) { myDxrSunShadowsWanted = aOn; }
		bool IsDxrSunShadows() const { return myDxrSunShadowsWanted && myDxrShadowCS != nullptr && myDxrShadowSrv.IsValid(); }

		// Live-tweakable knobs (debug UI). Read each frame by RenderSSAO / RenderShadows.
		struct Tunables
		{
			float ssaoRadius = 42.f, ssaoBias = 0.6f, ssaoIntensity = 1.5f, ssaoPower = 1.6f;
			float shadowDepthBias = 3.0f;   // world units (/ cascade depth range in shader)
			float shadowNormalOffset = 2.5f;   // in shadow-texels
			float shadowStrength = 1.0f;
			bool  shadowShowCascades = false;  // tint output by cascade index
			bool  dxrSunShadowDebug = false;

			// Screen-space contact shadows (directional light, in the lighting pass).
			bool  contactShadows   = true;
			float contactLength    = 45.0f;    // world units marched toward the sun
			float contactThickness = 30.0f;    // max depth gap treated as an occluder
			bool  contactViz       = false;    // debug: show the isolated contact term
			bool  localShadowViz   = false;    // debug: flag local-shadowed local lights
			bool  giViz            = false;    // debug: show the raw GI irradiance term

			// Point/spot shadow-atlas budget (each caster re-renders the scene per tile;
			// a point light = 6 tiles). Lower for scenes made of a few big meshes.
			int   localShadowMaxCasters = 4;   // total shadow-casting local lights
			int   localShadowMaxPoints  = 2;   // of those, at most this many points (6x cost)

			// Shared atmosphere; scene units are centimeters, UI distances are meters.
			bool fogEnabled = true;
			float fogDensity = 0.0015f; // extinction per meter at base height
			float fogHeightFalloff = 0.025f; // per meter
			float fogBaseHeight = 0.f;
			float fogStartDistance = 5.f;
			float fogMaxDistance = 500.f;
			float fogColor[3] = {0.30f, 0.40f, 0.55f}; // linear HDR
			bool fogAffectSky = false;
			bool volumetricEnabled = true;
			float volumetricStrength = 0.5f;
			float volumetricAnisotropy = 0.45f;
			float volumetricDistance = 120.f;
			int volumetricSteps = 32;
			int volumetricResolution = 1;   // volume march at 1/(2^n) of the fog target: 0 full, 1 half, 2 quarter
			bool sunDiskEnabled = true;
			// Radians. The real sun's angular RADIUS is ~0.267 degrees (its full
			// disk, i.e. diameter, is the commonly-quoted ~0.53 degrees / 32
			// arcminutes -- this field is the half-angle from the disk's center,
			// so it wants the smaller number).
			float sunDiskAngularRadius = 0.00465f; // ~0.267 degrees, the real sun
			float sunDiskIntensity = 24.f;        // HDR, shaped by the existing exposure/bloom path
			int atmosphereDebugView = 0; // beauty, transmittance, sunlight

			// --- procedural sky: physically based Rayleigh/Mie/ozone atmosphere
			// (Hillaire 2020, the technique Unreal Engine ships), driven by the
			// current sun direction. When enabled its output replaces the static
			// fallback cubemap as the source for raster ambient/IBL, DXR's
			// environment, and GI's environment tint -- see DeferredRendererSky.cpp.
			bool proceduralSkyEnabled = true;
			// Extra haze density (multiplies the Mie scattering coefficient), the
			// same role turbidity plays in Preetham/Hosek-Wilkie: ~2 is a clear
			// day, higher values wash out the sky toward white/grey.
			float atmosphereTurbidity = 2.f;
			// Diffuse reflectance of the ground plane the multi-scatter LUT bounces
			// light off; only visibly matters near the horizon and at twilight.
			float groundAlbedo = 0.3f;
			// Blend strength of the authored "night sky" cubemap (stars) as the sun
			// drops toward and below the horizon; 0 disables the night overlay.
			// Low by design: the selected cubemap (by default "horizonCubeMap", the
			// same asset used as the daytime fallback elsewhere) is authored at
			// roughly daylight brightness, not calibrated as a dim starfield -- at
			// 1.0 it was washing the correctly-dark physical sky back out to a
			// bright white wall at night. 1.0 still means "this cubemap at its own
			// authored brightness", for anyone who swaps in an actual night asset.
			float nightSkyIntensity = 0.05f;

			// --- post FX (bloom + exposure) ---
			bool  bloomEnabled    = true;
			float bloomThreshold  = 2.0f;    // HDR luma where bloom starts
			float bloomKnee       = 0.5f;    // soft-knee width (fraction of threshold)
			float bloomIntensity  = 0.04f;   // additive blend weight in the composite
			// Physical camera (see Photometry.h). Manual exposure comes from
			// aperture/shutter/ISO; auto meters the scene like a reflected-light
			// meter. Defaults are the "sunny 16" rule, EV100 ~15.
			bool  exposureAuto    = false;          // averaging meters over-brighten mostly shaded daylight views
			float cameraAperture  = 16.0f;          // f-number
			float cameraShutter   = 1.0f / 125.0f;  // seconds
			float cameraIso       = 100.0f;
			float autoEvMin       = -2.0f;          // metered EV100 clamp
			float autoEvMax       = 17.0f;
			float exposureSpeed   = 2.5f;    // adaptation rate (per second)
			float exposureComp    = 0.0f;    // EV compensation (stops, +1 = brighter)
			int   tonemapper      = 0;       // 0 AgX, 1 AgX Punchy, 2 ACES (fitted), 3 none
			// DXR renderer: write lighting already multiplied by the previous
			// frame's exposure, so moonlit and sunlit scenes both stay inside
			// FP16's precise range. Invisible otherwise.
			bool  preExposure     = true;
			// > 0: scale the environment map so its sky hemisphere averages this
			// luminance in cd/m² (clear day ~8 000, overcast ~2 000, night ~0.01).
			// 0 keeps the legacy relative "IBL scale". Defaults to 0 (off) because
			// proceduralSkyEnabled's sky is already physically correct in absolute
			// terms -- rescaling it to a fixed target here would flatten its own
			// day/night brightness swing back to a constant. This is for the
			// static/authored-cubemap case (proceduralSkyEnabled = false), where
			// the source texture has no inherent absolute brightness of its own.
			float skyLuminanceNits = 0.0f;

			// --- screen-space reflections ---
			bool  ssrEnabled        = true;
			float ssrMaxDistance    = 900.f;   // view-space march length
			float ssrThickness      = 24.f;    // depth-match tolerance (view units)
			float ssrRoughnessCutoff = 0.55f;  // no SSR past this roughness
			float ssrStrength       = 1.0f;
			int   ssrSteps          = 48;
			int   ssrRefineSteps    = 5;

			// --- DXR renderer controls ---
			float dxrSunIntensity     = 3.14159265f;   // radiance scale; /pi in the diffuse term cancels this at 1x-equivalent brightness
			float dxrSunTint[3]       = { 1.05f, 1.0f, 0.9f };
			// Safety floor only. Proper diffuse/specular environment lighting is the
			// primary fill source in full DXR mode; a large flat term destroys form.
			float dxrAmbientIntensity = 0.02f;
			float dxrReflectionRoughnessCutoff = 0.35f;
			bool  dxrDirectLighting = true;
			bool  dxrEnvironmentLighting = true;
			bool  dxrIndirectGi = true;
			// Feeds each probe's own volume state back into new probe updates so
			// light can bounce more than once. 0 disables it (original single-
			// bounce behaviour); ~1 approximates a physically plausible second+
			// bounce. See GiTraceInlineCS.hlsl's EvaluateDxrGi() feedback call.
			float dxrGiInfiniteBounce = 0.8f;
			bool  dxrReflections = true;
			bool  dxrAmbientOcclusion = true;
			float dxrAoDistance = 80.f;
			float dxrAoStrength = 1.f;
			// Occlusion rays per pixel. Measured at 1600x900 on Sponza, AO was
			// the single most expensive term in the DXR frame (~3.1 ms of
			// ~15.5 ms) purely because this was a hardcoded 4. It is the
			// stochastic term the temporal resolve converges best, so this is
			// the first dial to turn when the frame is too slow.
			int dxrAoSamples = 2;
			bool dxrTextureFiltering = true;
			// Emissive geometry as area lights. RIS candidates per pixel; 0 off.
			// Art without punctual lights (Bistro) gets all its local light from
			// this, but it only shows where the sun is not drowning it out.
			int emissiveLightSamples = 8;
			int dxrReflectionSamples = 4; // stochastic GGX rays/pixel; the shader scales this
			                             // down with roughness, so raising it is close to free
			bool specularAaEnabled = true;
			float specularAaStrength = 0.25f;
			bool taaEnabled = true;
			// Sub-pixel Halton offset on the primary rays. Off = a fixed sample
			// grid, which resolves temporally but recovers no geometric detail
			// and starves DLSS of the phases it expects.
			//
			// Default OFF, which costs real image detail, because with NRD on it
			// currently costs more than it buys. Measured on the Bistro street
			// camera, FROZEN, reading NRD's own DIFF FRAMES validation tile
			// (brighter = less history accumulated):
			//     jitter off                R  2.8 G  2.5 B  8.2
			//     jitter constant (.25,.25) R  3.1 G  3.3 B  6.1
			//     jitter Halton             R 38.8 G 20.1 B 29.3
			// A constant sub-pixel offset is free; it is the offset CHANGING
			// each frame that makes NRD reject history across the whole image,
			// which reads as everything shimmering in every upscaler mode, since
			// they all consume NRD's output.
			//
			// NRD's inputs already satisfy its documented contract (non-jittered
			// MVs and matrices, cameraJitter/Prev supplied), and its own
			// compensation -- widening the disocclusion threshold by
			// (1 + jitterDelta)/height -- is far too small to cover a full pixel
			// of movement. Feeding it the geometric normal instead of the
			// normal-mapped one as its guide recovers part of it (38.8 -> 27.4,
			// its README warns about exactly this), so the rest is viewZ moving
			// sub-pixel under the guide. Turn this back on once that is solved,
			// or when running with NRD off, where it is a pure win.
			bool taaJitter = false;
			// NVIDIA RTX path.  DLAA runs at native resolution and replaces the
			// custom temporal resolve only when Streamline reports it available.
			int dlssMode = 0;       // 0=off, 1=Native(DLAA), 2=Quality, 3=Balanced, 4=Performance, 5=UltraPerf
			// Off by default: DLSS keeps per-frame ray noise as detail, and even with
			// NRD the engine's own TAA measured steadier on a still camera.
			bool dlaaEnabled = false; // Use DLSS to anti-alias the native resolution
			int dlssPreset = 0;       // 0 default, 1 J, 2 K, 3 L (steadiest measured), 4 M
			bool rayReconstructionEnabled = false; // Override dlaaEnabled and dlssMode to 1
			bool nrdEnabled = true;    // 1-2 ray AO/reflections are unusably noisy without it
			// NRD only: trace AO and reflection rays on alternating half-pixel
			// checkerboards (NRD reconstructs full resolution). ~Halves both.
			bool nrdCheckerboard = false;
			// 0 REBLUR (NVIDIA's choice for low-sample path-traced signals),
			// 1 RELAX (tuned for clean, high-sample signals such as RTXDI).
			int nrdDenoiser = 0;
			float nrdHistorySeconds = 0.5f;   // longer = smoother but more lag / smearing
			int nrdFastHistoryFrames = 6;      // responsive history used to clamp the long one
			bool nrdAntilag = true;            // reset history where lighting changes quickly
			bool nrdValidation = false;        // NRD's debug overlay (motion, depth, normals, history)
			float nrdDisocclusionThreshold = 0.01f; // NRD relative plane-distance threshold (test knob)
			bool nrdMotion25D = false;         // feed NRD viewZprev - viewZ in motion.z (2.5D)
			// Sun shadow rays per pixel (1-4); fewer rays rotate per frame and
			// rely on the temporal resolve for the soft penumbra.
			int dxrSunShadowSamples = 4;
			// The depth/motion rejection protects disocclusions. Keep more history
			// on valid samples so single-sample DXR AO and reflections converge
			// rather than visibly pulse while the camera is still.
			float taaHistoryWeight = 0.94f;
			float taaStationaryWeight = 0.975f;
			int taaDebugView = 0; // resolved, reprojected history, rejection mask
			int dxrLightingView = 0; // beauty, AO, environment, diffuse GI
		};
		Tunables& GetTunables() { return myTunables; }
		bool RecreateDxrTargets() { return myDxrLightingCS ? CreateDxrLightingTargets(myResolution) : false; }

		// Post FX: bloom + auto-exposure, applied during Composite (HDR -> backbuffer).
		// Off => Composite falls back to the plain engine tonemap.
		void SetPostFx(bool aOn) { myPostFxWanted = aOn; }
		bool IsPostFx() const { return myPostFxWanted && myCompositePs != nullptr; }

		void SetShadowLight(const Vector3f& aLightDir, const Vector3f& aSceneCenter, float aSceneRadius);
		const ModelShader& GetShadowShader() const { return *myShadowShader; }
		const Camera& GetCascadeCamera(int i) const { return myCascadeCam[i]; }

		// Stage-3 validation pass: one inline RayQuery per pixel against the
		// CURRENT frame's TLAS (rhi::IDevice::BuildRaytracingTlas), root-bound
		// automatically each frame via Dx12Device's space2 root SRVs (see
		// Dx12CommandContext::OnBeginFrame) -- nothing here binds the TLAS
		// itself. Opt-in, DX12/DXR-1.1-only, and deliberately decoupled from
		// the debug-channel system below: it doesn't read or replace any
		// G-buffer channel, only writes its own diagnostic hit/miss texture.
		// GameWorld must call BuildRaytracingTlas (already does, before
		// BeginFrame) earlier in the SAME frame this pass runs in -- BuildFrame
		// is called after BeginFrame, so by construction this pass always sees
		// this frame's own TLAS, never a stale or null one from frame zero.
		void SetDxrLighting(bool aOn) { myDxrLightingWanted = aOn; }
		bool IsDxrLighting() const { return myDxrLightingWanted && myDxrLightingCS != nullptr; }
		rhi::SrvHandle GetDxrLightingSrv() const { return myDxrLightingSrv; }

		// When on, BuildFrame skips the normal lit scene entirely and blits
		// this pass's output straight to the backbuffer at full resolution,
		// same short-circuit shape as the aDebugChannel>0 path below.
		void SetDxrFullscreen(bool aOn) { myDxrFullscreenWanted = aOn; }
		bool IsDxrFullscreen() const { return IsDxrLighting() && myDxrFullscreenWanted; }

		// Full DXR mode owns HDR lighting. It bypasses the G-buffer lighting
		// resolve and all raster shadow/AO/reflection passes.
		void SetDxrRenderer(bool aOn) { SetDxrLighting(aOn); SetDxrFullscreen(aOn); }
		bool IsDxrRenderer() const { return IsDxrFullscreen(); }

		// Froxel grid config.
		static constexpr int kTilePx        = 32;
		static constexpr int kZSlices       = 24;
		static constexpr int kMaxPerCluster = 256;   // lights binned per froxel; excess is dropped

	private:
		bool CreateTargets(Vector2ui aResolution);
		bool CreateClusterBuffers(Vector2ui aResolution);
		bool CreateShadowMaps();
		bool CreateLocalShadowAtlas();
		bool CreateDxrLightingTargets(Vector2ui aResolution);
		bool CreateDxrBrdfLut();
		void GatherEmissiveLights(rhi::ICommandContext& ctx);
		void EnsureReservoirs();
		void RenderDxrLighting();
		void ResolveDxrLightingToHdr();
		// One fogged-HDR target plus its half-resolution sun-shaft volume.
		struct AtmosphereTargetSet
		{
			RenderTarget hdr;
			rhi::TextureHandle volumeTex;
			rhi::SrvHandle volumeSrv;
			rhi::UavHandle volumeUav;
			Vector2ui size{ 0, 0 };
			bool IsValid() const { return hdr.GetSrv().IsValid() && volumeSrv.IsValid() && volumeUav.IsValid(); }
		};
		bool CreateAtmosphereTargetSet(Vector2ui aResolution, AtmosphereTargetSet& aSet);
		void ReleaseAtmosphereTargetSet(AtmosphereTargetSet& aSet);
		bool CreateAtmosphereTargets(Vector2ui resolution);
		// aRenderResolution: apply fog in the DXR render-resolution domain, into
		// myAtmosphereRender, instead of the display-resolution myHdr. Used when
		// DLSS upscales, so fog is part of the image DLSS reconstructs --
		// previously fog was simply never drawn in that mode.
		bool RenderAtmosphere(bool beforeTemporal = false, bool aRenderResolution = false);
		rhi::ConstantBuffer myAtmosphereCb;
		rhi::ConstantBuffer myAtmosphereShadowCameraCb;
		const PixelShader* myAtmospherePs = nullptr;
		const ComputeShader* myVolumeCS = nullptr;
		const ComputeShader* myVolumeRTCS = nullptr;
		RenderTarget myAtmosphereHdr;
		rhi::TextureHandle myVolumeTex;
		uint32_t myVolumeDivisor = 2;       // divisor the volume textures were allocated with
		bool CreateVolumeTexture(Vector2ui aTargetSize, const char* aName, rhi::TextureHandle& aTex, rhi::SrvHandle& aSrv, rhi::UavHandle& aUav);
		bool myNrdCheckerboardActive = false;
		rhi::ConstantBuffer myNrdCompositeCb;
		rhi::SrvHandle myVolumeSrv;
		rhi::UavHandle myVolumeUav;
		// Render-resolution fog outputs; only allocated while DLSS renders the
		// ray pass below display resolution.
		AtmosphereTargetSet myAtmosphereRender;

		// --- procedural sky (DeferredRendererSky.cpp) ---
		static constexpr uint32_t kTransmittanceLutW = 256, kTransmittanceLutH = 64;
		static constexpr uint32_t kMultiScatterLutRes = 32;
		static constexpr uint32_t kSkyViewLutW = 192, kSkyViewLutH = 108;
		static constexpr uint32_t kSkyCubemapRes = 128;
		bool CreateSkyTargets();
		void ReleaseSkyTargets();
		void DispatchSkyViewLut();   // always: cheap, tracks the sun every frame it's dirty
		void DispatchSkyFixedLuts(); // transmittance + multi-scatter: only on tunable changes
		void RenderSkyCubemap(rhi::SrvHandle aNightSkyCubeSrv);
		const ComputeShader* mySkyTransmittanceLutCS = nullptr;
		const ComputeShader* mySkyMultiScatterLutCS = nullptr;
		const ComputeShader* mySkyViewLutCS = nullptr;
		const PixelShader* mySkyCubemapPs = nullptr;
		rhi::ConstantBuffer mySkyConstantsCb;      // b11, shared by all sky-LUT passes
		rhi::ConstantBuffer mySkyCubemapFaceCb;    // b12, SkyCubemapPS only
		rhi::TextureHandle myTransmittanceLutTex; rhi::SrvHandle myTransmittanceLutSrv; rhi::UavHandle myTransmittanceLutUav;
		rhi::TextureHandle myMultiScatterLutTex; rhi::SrvHandle myMultiScatterLutSrv; rhi::UavHandle myMultiScatterLutUav;
		rhi::TextureHandle mySkyViewLutTex; rhi::SrvHandle mySkyViewLutSrv; rhi::UavHandle mySkyViewLutUav;
		rhi::TextureHandle mySkyCubemapTex; rhi::SrvHandle mySkyCubemapSrv;
		rhi::RtvHandle mySkyCubemapFaceRtv[6];
		rhi::SamplerHandle mySkyLutSampler, mySkyCubeSampler;
		// Dirty tracking: the fixed LUTs only depend on atmosphere tunables
		// (turbidity/ground albedo), not the sun direction, so they're cached
		// separately from the sky-view LUT + cubemap, which track the sun.
		bool mySkyFixedLutsValid = false;
		float myLastSkyTurbidity = -1.f, myLastSkyGroundAlbedo = -1.f;
		Vector3f myLastSkySunDir{ 0.f, 0.f, 0.f };
		Vector3f myLastSkySunIlluminance{ -1.f, -1.f, -1.f };
		float myLastSkyCameraHeight = -1e9f;

		void RenderLocalShadows(const std::function<void(const Camera&)>& aDrawShadowCasters);
		bool CreatePostFxTargets(Vector2ui aResolution);
		void PostFxFullscreen(const PixelShader* aPs, RenderTarget& aDst, Vector2ui aDstSize,
		                      const rhi::SrvHandle* aSrvs, int aSrvCount,
		                      Vector2f aSrcTexel, bool aAdditive = false);
		void CullClusters();
		void RenderSSAO();
		void RenderShadows(const std::function<void(const Camera&)>& aDrawShadowCasters);
		void RenderDxrSunShadows();
		void BindFullscreen(const PixelShader* aPixelShader);
		void WriteDxrDepthToDepthBuffer();
		void BindGBufferSrvs();
		void UnbindGBufferSrvs();

		Vector2ui myResolution{ 0, 0 };
		bool myReady = false;

		RenderTarget myAlbedo;      // SV_TARGET0
		RenderTarget myNormal;      // SV_TARGET1
		RenderTarget myMaterial;    // SV_TARGET2
		RenderTarget myEmissive;    // SV_TARGET3
		RenderTarget myHdr;         // lighting accumulation (R16G16B16A16F)
		RenderTarget myOpaqueHdr;   // immutable HDR snapshot sampled by forward glass

		std::unique_ptr<ModelShader> myGeometryShader;   // PbrModelShaderVS + GBufferPS
		std::unique_ptr<ModelShader> myGlassShader;      // PbrModelShaderVS + GlassModelShaderPS
		const VertexShader* myFullscreenVs = nullptr;
		const PixelShader*  myLightingPs   = nullptr;
		const PixelShader*  myDebugPs      = nullptr;
		const PixelShader*  mySceneCopyPs  = nullptr;
		const PixelShader*  myDxrDepthPs   = nullptr;   // DXR device depth -> depth buffer

		rhi::SamplerHandle myPointSampler;   // s1

		rhi::StructuredBuffer myLightBuffer;              // t15 structured (CPU-updated)
		rhi::ConstantBuffer myLightParamsCb;           // b6 { uint count; }
		int myLightCount = 0;

		// --- clustered light culling ---
		const ComputeShader* myClusterCS = nullptr;
		rhi::StructuredBuffer myClusterIndexBuffer;      // t16 / u0 (GPU-only, CS-written)
		rhi::StructuredBuffer myClusterCountBuffer;      // t17 / u1 (GPU-only, CS-written)
		rhi::ConstantBuffer myClusterCb;               // CS b0 / PS b7
		Vector2ui myTileCount{ 0, 0 };
		int myNumClusters = 0;
		bool myClusteredWanted = true;

		Matrix4x4f myProjToView;
		Matrix4x4f myViewToProj;
		Matrix4x4f myWorldToView;
		Matrix4x4f myCameraTransform;   // world transform (SetCamera); DXR lighting pass needs position + basis, not just the view matrix
		float myNear = 1.f, myFar = 100000.f;

		// --- DXR Stage-3 validation: RayQuery DXR lighting pass ---
		const ComputeShader* myDxrLightingCS = nullptr;
		rhi::TextureHandle myDxrLightingTex;                 // RGBA16F HDR ray-traced radiance
		Vector2ui myDxrRenderResolution{ 0, 0 };           // DLSS input resolution; display target stays myResolution
		rhi::SrvHandle myDxrLightingSrv;
		rhi::UavHandle myDxrLightingUav;
		rhi::TextureHandle myDlaaTex;                     // native-resolution DLAA output
		rhi::SrvHandle myDlaaSrv;
		rhi::UavHandle myDlaaUav;
		const ComputeShader* myDxrBrdfLutCS = nullptr;
		// Emissive geometry as area lights. Art like Bistro has no punctual
		// lights at all -- every lamp and sign is an emissive mesh -- so without
		// this list nothing samples them and a lamp glows without lighting
		// anything. Rebuilt only when the ray scene changes.
		const ComputeShader* myEmissiveGatherCS = nullptr;
		rhi::StructuredBuffer myEmissiveLightBuffer;
		rhi::StructuredBuffer myEmissiveCountBuffer;
		rhi::ConstantBuffer myEmissiveGatherCb;
		uint32_t myEmissiveGatheredFor = 0xffffffffu;   // instance count the list was built from
		// ReSTIR reservoirs, ping-ponged: one frame writes while the previous
		// frame's is read for temporal reuse. A reservoir is 48 bytes.
		rhi::StructuredBuffer myReservoirBuffer[2];
		uint32_t myReservoirIndex = 0;
		Vector2ui myReservoirResolution{ 0, 0 };
		static constexpr uint32_t kMaxEmissiveLights = 65536;
		rhi::TextureHandle myDxrBrdfLutTex;              // 256² RG16F split-sum BRDF integration
		rhi::SrvHandle myDxrBrdfLutSrv;
		rhi::UavHandle myDxrBrdfLutUav;
		rhi::ConstantBuffer myDxrBrdfLutCb;              // CS b0, generated once at startup
		// Motion, device depth, validity, then signed normal + roughness.
		// The last guide is both a stronger native temporal reject test and the
		// material-aware input required by NRD/DLAA/Streamline integrations.
		// [6] is the resolve-time motion vector: surface motion blended toward
		// the reflected virtual image's motion on specular-dominated pixels.
		std::array<rhi::TextureHandle, 7> myTemporalTex;
		std::array<rhi::SrvHandle, 7> myTemporalSrv;
		std::array<rhi::UavHandle, 7> myTemporalUav;
		Matrix4x4f myPreviousWorldToClip;
		Matrix4x4f myResolvePreviousWorldToClip;   // previous frame's matrix, as seen by the resolve pass
		bool myTemporalHistoryValid = false;
		const ComputeShader* myTaaCS = nullptr;
		rhi::ConstantBuffer myTaaCb;
		// Color/depth pairs, diagnostic, and one surface-guide history per pair.
		std::array<rhi::TextureHandle, 7> myTaaTex;
		std::array<rhi::SrvHandle, 7> myTaaSrv;
		std::array<rhi::UavHandle, 7> myTaaUav;
		bool myTaaHistoryValid = false, myTaaWasEnabled = false;
		bool myRaySceneStationary = false, myTaaCameraStationary = false;
		// Geometry motion cannot describe a moving shadow.  Set by light uploads
		// and SetShadowLight; consumed after the temporal resolve for this frame.
		bool myTaaLightingChanged = false;
		uint32_t myTaaFrameIndex = 0, myTaaHistoryIndex = 0;
		uint32_t myDxrFrameIndex = 0; // advances even when TAA is disabled; seeds stochastic DXR work
		Vector2f myTaaJitter{0,0}, myPreviousTaaJitter{0,0};
		rhi::ConstantBuffer myDxrLightingCb;   // CS b0
		rhi::SamplerHandle myDxrMaterialSampler;   // CS s0, for the material albedo fetch
		const PixelShader* myDxrCopyPs = nullptr; // linear HDR copy into myHdr
		bool myDxrLightingWanted = false;
		bool myDxrFullscreenWanted = false;
		
		// NVIDIA NRD (RELAX diffuse + specular) over the DXR pass's indirect lighting.
		// Created lazily on first use and dropped on every target resize.
		std::unique_ptr<rhi::dx12::NrdWrapper> myNrd;
		GpuProfiler* myProfiler = nullptr;
		bool myNrdFailed = false;          // don't retry creation every frame
		bool myNrdHistoryValid = false;
		// viewZ (R32F), normal+roughness (R10G10B10A2), then RGBA16F noisy
		// diffuse, noisy specular, denoised diffuse, denoised specular.
		// The first four are DxrLightingCS's u7..u10.
		enum { kNrdViewZ, kNrdNormal, kNrdDiffuse, kNrdSpecular, kNrdDiffuseOut, kNrdSpecularOut, kNrdValidation, kNrdTexCount };
		std::array<rhi::TextureHandle, kNrdTexCount> myNrdTex;
		std::array<rhi::SrvHandle, kNrdTexCount> myNrdSrv;
		std::array<rhi::UavHandle, kNrdTexCount> myNrdUav;
		const ComputeShader* myNrdCompositeCS = nullptr;
		Matrix4x4f myNrdPrevWorldToView, myNrdPrevViewToClip;
		bool NrdActive() const;
		void DenoiseDxrDiffuse(rhi::ICommandContext& ctx);

		// --- SSAO ---
		const PixelShader* mySsaoPs = nullptr;
		const PixelShader* mySsaoBlurPs = nullptr;
		RenderTarget myAoRaw;    // R8, raw occlusion
		RenderTarget myAo;       // R8, blurred (fed to lighting at t18)
		rhi::ConstantBuffer mySsaoCb;       // b8
		rhi::ConstantBuffer mySsaoBlurCb;   // b8
		bool mySsaoWanted = true;

		// --- screen-space reflections ---
		const PixelShader* mySsrPs = nullptr;
		const PixelShader* mySsrApplyPs = nullptr;
		RenderTarget mySsrTex;                               // half-res RGBA16F reflection radiance + confidence
		Vector2ui    mySsrRes{ 0, 0 };
		RenderTarget myIblSpecTex;                           // RGBA16F probe IBL specular (for SSR-over-probe)
		rhi::ConstantBuffer mySsrCb;        // b8
		bool mySsrWanted = true;

		rhi::ConstantBuffer myProbeCb;     // b12 ReflectionProbeBuffer
		float myProbeBox[8] = { 0,0,0,0, 0,0,0,0 };         // center.xyz+enabled, half.xyz+pad

		// --- emissive-GI irradiance volume ---
		const ComputeShader* myGiProjectCS = nullptr;
		rhi::StructuredBuffer myGiShBuffer;                   // structured float4, 9 per probe; t22 / CS u0
		rhi::StructuredBuffer myGiShPreviousBuffer;          // previous sweep for emissive bounce
		// DXR DDGI visibility: 8x8 octahedral directional distance moments/probe.
		rhi::StructuredBuffer myGiVisibilityBuffer;           // float4 {mean, meanSq, valid, pad}; CS u1 / DXR t5
		rhi::StructuredBuffer myGiVisibilityPreviousBuffer;
		rhi::ConstantBuffer myGiVolumeCb;                  // b13
		rhi::ConstantBuffer myGiProjectCb;                 // CS b0
		rhi::SamplerHandle myGiLinearSampler;       // CS s0
		const ComputeShader* myGiTraceCS = nullptr;   // GiTraceInlineCS: ray-traced probe capture
		rhi::ConstantBuffer myGiTraceCb;           // CS b0
		// Per-group probe list for GiProjectProbeBatchRT: float4 {xyz = world
		// position, w = probe index bit-cast to uint}, CS t6. Sized so a whole
		// frame's batch normally fits in ONE StructuredBuffer::Update --
		// that class only has 4 update slots per frame before it starts
		// overwriting storage already recorded into the command list, so a
		// larger batch must split into at most 4 dispatches (see the
		// implementation), not one per probe.
		static constexpr int kMaxGiProbeBatch = 256;
		rhi::StructuredBuffer myGiProbeBatchBuffer;
		// Varies the low-discrepancy rotation on each inline probe refresh so
		// temporal hysteresis can accumulate new samples.
		uint32_t myGiTraceSequence = 0;
		rhi::SrvHandle myGiEnvironmentSrv;          // CS t3, prefiltered environment
		Vector3f myGiEnvironmentTint{ 0.f, 0.f, 0.f };
		bool myGiEnvironmentEnabled = false;
		// Physical sky: EnvironmentAverageCS measures the sky hemisphere once per
		// environment; the readback lands the frame after the dispatch.
		const ComputeShader* myEnvAverageCS = nullptr;
		rhi::TextureHandle myEnvAverageTex;
		rhi::UavHandle myEnvAverageUav;
		rhi::SrvHandle myEnvAverageMeasured;     // environment the value belongs to
		rhi::SrvHandle myEnvAveragePending;      // dispatched, awaiting readback
		float myEnvAverageLuminance = -1.f;
		void MeasureEnvironment(rhi::ICommandContext& ctx);
		Vector3f EnvironmentTint() const;        // tint for every environment lookup
		float SkyDisplayScale() const;           // primary-ray sky scale
		float SkyBrightnessScale() const;        // physical sky relative to a clear day
		bool PreExposureActive() const;
		void EnsureExposureHistory();

		// --- cascaded shadow maps (directional) ---
		static constexpr int kShadowRes = 3072;
		std::unique_ptr<ModelShader> myShadowShader;         // PbrModelShaderVS + ShadowPS
		rhi::TextureHandle myShadowTex; // D32_Float (typeless resource), kNumCascades array slices
		rhi::SrvHandle myShadowSrv;                 // t19, array
		std::array<rhi::DsvHandle, kNumCascades> myShadowDsvs;
		rhi::SamplerHandle myShadowCmpSampler;                // s2
		rhi::ConstantBuffer myShadowCb;     // b9
		const ComputeShader* myDxrShadowCS = nullptr;
		rhi::TextureHandle myDxrShadowTex;
		rhi::SrvHandle myDxrShadowSrv;
		rhi::UavHandle myDxrShadowUav;
		rhi::ConstantBuffer myDxrShadowCb;
		bool myDxrSunShadowsWanted = false;
		std::array<Camera, kNumCascades> myCascadeCam;
		std::array<Matrix4x4f, kNumCascades> myCascadeViewProj;
		std::array<float, kNumCascades> myCascadeSplit{};
		Vector3f myShadowLightDir{ 0.f, -1.f, 0.f };
		Vector3f myShadowSceneCenter{ 0.f, 0.f, 0.f };
		float myShadowSceneRadius = 1000.f;
		bool myShadowsWanted = true;

		// --- point / spot light shadow atlas ---
		rhi::TextureHandle myLocalAtlasTex;              // D32_Float (typeless resource)
		rhi::DsvHandle myLocalAtlasDsv;      // whole atlas
		rhi::SrvHandle myLocalAtlasSrv;    // t21
		rhi::StructuredBuffer myLocalShadowBuffer;            // structured, t20 (CPU-updated)
		std::vector<DeferredLight> myLights;   // CPU copy (UploadLights); shadowSlot patched per frame
		Vector3f myCameraPos{ 0.f, 0.f, 0.f };
		bool myLocalShadowsWanted = true;

		// --- post fx: bloom mip chain + auto exposure ---
		static constexpr int kBloomMips = 6;
		const PixelShader* myBloomPrefilterPs = nullptr;
		const PixelShader* myBloomDownPs      = nullptr;
		const PixelShader* myBloomUpPs        = nullptr;
		const PixelShader* myExposureLumaPs   = nullptr;
		const PixelShader* myExposureDownPs   = nullptr;
		const PixelShader* myExposureAdaptPs  = nullptr;
		const PixelShader* myCompositePs      = nullptr;
		std::array<RenderTarget, kBloomMips> myBloomMip;   // [0] = half res, each next halved
		std::array<Vector2ui, kBloomMips>    myBloomSize{};
		std::array<RenderTarget, 7> myExpMip;              // 64,32,16,8,4,2,1 log-luma
		RenderTarget myExposure[2];                        // persistent 1x1 EV100 ping-pong
		int  myExposureSrc = 0;
		int  myPreExposureIndex = 0;                       // history the current HDR was exposed with
		bool myExposureCleared = false;
		rhi::SamplerHandle myLinearSampler;   // s3
		rhi::ConstantBuffer myPostFxCb;              // b10
		bool myPostFxWanted = true;

		Tunables myTunables;
	};
}
