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

struct ID3D11SamplerState;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11Texture2D;
struct ID3D11DepthStencilView;

namespace Tga
{
	class ModelShader;
	class RenderGraph;
	class Camera;
	struct VertexShader;
	struct PixelShader;
	struct ComputeShader;

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

		const ModelShader& GetGeometryShader() const { return *myGeometryShader; }

		// Material-preview shader: renders a mesh into the G-buffer with constant PBR
		// values (no textures) from BindDebugMaterial(). For a debug sphere.
		struct DebugMaterial
		{
			float baseColor[3] = { 0.8f, 0.8f, 0.8f };
			float roughness = 0.5f;
			float metalness = 0.0f;
			float ao = 1.0f;
			float emissiveColor[3] = { 1.f, 1.f, 1.f };
			float emissiveStrength = 0.0f;
		};
		bool HasDebugMatShader() const { return myDebugMatShader != nullptr; }
		const ModelShader& GetDebugMatShader() const { return *myDebugMatShader; }
		void BindDebugMaterial(const DebugMaterial& aMat);

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
		void ClearGi();   // zero the whole SH buffer -- call before a fresh (re-)prime
		void GiProjectProbe(ID3D11ShaderResourceView* aCubeSrv, int aProbeIndex,
		                    float aHysteresis, int aFaceRes);

		// Cascaded shadow maps for the directional light. Feed the light direction
		// + scene bounds each frame (after SetCamera); BuildFrame's shadow pass then
		// renders kNumCascades depth cascades via the caller's draw fn, and the
		// lighting resolve PCF-samples them onto the directional term.
		static constexpr int kNumCascades = 4;

		// Point / spot light shadows: nearest few casters render into a shared depth
		// atlas each frame; the lighting resolve samples it per local light.
		static constexpr int kLocalAtlasRes  = 4096;
		static constexpr int kLocalTilePx    = 512;
		static constexpr int kLocalTilesRow  = kLocalAtlasRes / kLocalTilePx;   // 8
		static constexpr int kLocalTileCount = kLocalTilesRow * kLocalTilesRow; // 64
		static constexpr int kMaxShadowLights = 8;
		void SetLocalShadows(bool aOn) { myLocalShadowsWanted = aOn; }
		bool IsLocalShadows() const { return myLocalShadowsWanted && myShadowShader && myLocalAtlasSrv.IsValid(); }

		void SetShadows(bool aOn) { myShadowsWanted = aOn; }
		bool IsShadows() const { return myShadowsWanted && myShadowShader != nullptr && myShadowDsvs[0].IsValid(); }

		// Live-tweakable knobs (debug UI). Read each frame by RenderSSAO / RenderShadows.
		struct Tunables
		{
			float ssaoRadius = 42.f, ssaoBias = 0.6f, ssaoIntensity = 1.5f, ssaoPower = 1.6f;
			float shadowDepthBias = 3.0f;   // world units (/ cascade depth range in shader)
			float shadowNormalOffset = 2.5f;   // in shadow-texels
			float shadowStrength = 1.0f;
			bool  shadowShowCascades = false;  // tint output by cascade index

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

			// --- post FX (bloom + exposure) ---
			bool  bloomEnabled    = true;
			float bloomThreshold  = 2.0f;    // HDR luma where bloom starts
			float bloomKnee       = 0.5f;    // soft-knee width (fraction of threshold)
			float bloomIntensity  = 0.04f;   // additive blend weight in the composite
			bool  exposureAuto    = false;   // opt-in; manual 1.0 keeps the tuned look
			float exposureKey     = 0.14f;   // auto: target average scene luma
			float manualExposure  = 1.0f;    // used when exposureAuto == false
			float exposureMin     = 0.10f;
			float exposureMax     = 4.0f;
			float exposureSpeed   = 2.5f;    // adaptation rate (per second)
			float exposureComp    = 0.0f;    // EV bias (stops)

			// --- screen-space reflections ---
			bool  ssrEnabled        = true;
			float ssrMaxDistance    = 900.f;   // view-space march length
			float ssrThickness      = 24.f;    // depth-match tolerance (view units)
			float ssrRoughnessCutoff = 0.55f;  // no SSR past this roughness
			float ssrStrength       = 1.0f;
			int   ssrSteps          = 48;
			int   ssrRefineSteps    = 5;
		};
		Tunables& GetTunables() { return myTunables; }

		// Post FX: bloom + auto-exposure, applied during Composite (HDR -> backbuffer).
		// Off => Composite falls back to the plain engine tonemap.
		void SetPostFx(bool aOn) { myPostFxWanted = aOn; }
		bool IsPostFx() const { return myPostFxWanted && myCompositePs != nullptr; }

		void SetShadowLight(const Vector3f& aLightDir, const Vector3f& aSceneCenter, float aSceneRadius);
		const ModelShader& GetShadowShader() const { return *myShadowShader; }
		const Camera& GetCascadeCamera(int i) const { return myCascadeCam[i]; }

		// Froxel grid config.
		static constexpr int kTilePx        = 32;
		static constexpr int kZSlices       = 24;
		static constexpr int kMaxPerCluster = 256;   // lights binned per froxel; excess is dropped

	private:
		bool CreateTargets(Vector2ui aResolution);
		bool CreateClusterBuffers(Vector2ui aResolution);
		bool CreateShadowMaps();
		bool CreateLocalShadowAtlas();
		void RenderLocalShadows(const std::function<void(const Camera&)>& aDrawShadowCasters);
		bool CreatePostFxTargets(Vector2ui aResolution);
		void PostFxFullscreen(const PixelShader* aPs, RenderTarget& aDst, Vector2ui aDstSize,
		                      const rhi::SrvHandle* aSrvs, int aSrvCount,
		                      Vector2f aSrcTexel, bool aAdditive = false);
		void CullClusters();
		void RenderSSAO();
		void RenderShadows(const std::function<void(const Camera&)>& aDrawShadowCasters);
		void BindFullscreen(const PixelShader* aPixelShader);
		void BindGBufferSrvs();
		void UnbindGBufferSrvs();

		Vector2ui myResolution{ 0, 0 };
		bool myReady = false;

		RenderTarget myAlbedo;      // SV_TARGET0
		RenderTarget myNormal;      // SV_TARGET1
		RenderTarget myMaterial;    // SV_TARGET2
		RenderTarget myEmissive;    // SV_TARGET3
		RenderTarget myHdr;         // lighting accumulation (R16G16B16A16F)

		std::unique_ptr<ModelShader> myGeometryShader;   // PbrModelShaderVS + GBufferPS
		std::unique_ptr<ModelShader> myDebugMatShader;   // PbrModelShaderVS + GBufferDebugMatPS
		rhi::ConstantBuffer myDebugMatCb;   // b11
		const VertexShader* myFullscreenVs = nullptr;
		const PixelShader*  myLightingPs   = nullptr;
		const PixelShader*  myDebugPs      = nullptr;

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
		float myNear = 1.f, myFar = 100000.f;

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
		rhi::ConstantBuffer myGiVolumeCb;                  // b13
		rhi::ConstantBuffer myGiProjectCb;                 // CS b0
		rhi::SamplerHandle myGiLinearSampler;       // CS s0

		// --- cascaded shadow maps (directional) ---
		static constexpr int kShadowRes = 3072;
		std::unique_ptr<ModelShader> myShadowShader;         // PbrModelShaderVS + ShadowPS
		rhi::TextureHandle myShadowTex; // D32_Float (typeless resource), kNumCascades array slices
		rhi::SrvHandle myShadowSrv;                 // t19, array
		std::array<rhi::DsvHandle, kNumCascades> myShadowDsvs;
		rhi::SamplerHandle myShadowCmpSampler;                // s2
		rhi::ConstantBuffer myShadowCb;     // b9
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
		RenderTarget myExposure[2];                        // persistent 1x1 ping-pong
		int  myExposureSrc = 0;
		bool myExposureCleared = false;
		rhi::SamplerHandle myLinearSampler;   // s3
		rhi::ConstantBuffer myPostFxCb;              // b10
		bool myPostFxWanted = true;

		Tunables myTunables;
	};
}
