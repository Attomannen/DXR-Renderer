# TGE rendering backend roadmap

Living plan for the graphics-backend remake. Status legend:
`[x]` done · `[~]` in progress · `[ ]` not started · `[>]` deferred / parked

Goal (user): faster & better PBR, deferred shading, accurate + fast lighting,
emissive global illumination, reflections, fewer draw calls — plus editor tooling,
debug/profiling, content pipeline, and standalone utilities.

Bench + baselines: `Source/Game/BENCH.md`. Per-phase numbers are captured there.

---

## Phase 0 — Baseline & build  `[x]`

- [x] CLI build of all configs (VS 18 / MSVC v145) — `p5g3-build-env`
- [x] Sponza bench harness (`GameMain`), scripted fly-through, JSON reports
- [x] Perf baseline captured (forward PBR, 1×/4×/9× Sponza)
- [x] FBX static-mesh import cache — first load ~16 s → ~0.7 s (23×)

## Phase 1 — Draw-call reduction  `[x]`

- [x] `ModelShader` split `RenderSetup()` (per-instance binds) / `RenderMesh()` (per-sub-mesh)
- [x] Skip the per-sub-mesh 8 KB bone-buffer map for static meshes
- [x] Merge same-material sub-meshes at import (Sponza 120 → 28), baked into mesh cache v3
- [x] Whole-model frustum cull (`Model::GetBounds()`, `ModelDrawer::SetCullFrustum()`)
- Result: 4× −24 %, 9× −49 %, CPU submit −90 %

## Phase 2 — Deferred shading  `[~]`

- [x] 4-MRT G-buffer (albedo / world-normal / ORM+emissiveMask / emissive) + shared depth
- [x] Geometry pass (`GBufferPS.hlsl`, reuses `PbrModelShaderVS` + ModelShader path + cull)
- [x] Fullscreen PBR lighting resolve (`DeferredLightingPS.hlsl`, depth → world-pos)
- [x] HDR light target + engine tonemap composite
- [x] G-buffer debug visualiser (`BENCH_GBUF=1..7`, `DeferredDebugPS.hlsl`)
- [x] Screenshot capture in the bench (`BENCH_SCREENSHOT`)
- [x] Verified visually equal to forward
- Result: 1× −17 %, 9× −20 %, frame pacing much tighter
- Still in the bench only — see Phase 2.5

## Phase 2.5 — Productionize deferred + instrument  `[~]`

- [x] GPU timestamp profiler — `Source/Graphics/tge/render/GpuProfiler.{h,cpp}` (disjoint + per-scope begin/end, 5-frame ring-buffered non-blocking readback, `TGA_GPU_SCOPE` macro)
- [x] Per-pass timing in the bench report (`gpu_ms: { frame, geometry, lighting, composite }`)
  - Finding: **lighting is flat ~0.2 ms regardless of scene / overdraw**; all cost is the G-buffer geometry pass. Confirms Phase 3 (many lights) will be cheap.
- [x] On-screen pass-timing overlay — `GameWorld::Impl::DrawPerfOverlayImpl()`: always-on HUD in free-fly,
      CPU/GPU frame ms + per-`RenderGraph`-pass GPU scopes from `GpuProfiler::GetResults()`. "Perf overlay" toggle.
- [x] Shader hot-reload **verified present** — `DX11::CompileShaderIfChanged` registers a `FileWatcher`
      callback per top-level `.hlsl`, `Application::Update` calls `FlushChanges()` each frame. Works for
      direct `.hlsl` edits; editing a shared `.hlsli` include needs a relaunch (watcher only tracks the entry files).
- [x] **GPU debug markers** — `Tga::GpuMarkerScope` / `TGA_GPU_MARKER` (`Source/Graphics/tge/render/GpuMarker.{h,cpp}`),
  lazily QIs `DX11::Context` for `ID3DUserDefinedAnnotation`. The render graph wraps every pass automatically.
- [ ] Verify shader hot-reload covers the deferred set — `FileWatcher` + runtime `D3DCompileFromFile` already exist
  behind `DebugFeature::Filewatcher`; confirm it picks up `GBufferPS` / `DeferredLightingPS` / `DeferredDebugPS` (free Phase 3 iteration speed if so)
- [x] Bench free-flight camera + saved viewpoint: fly with `BENCH_FRAMES=0` (WASD / RMB-look / E-Q / Shift),
  **F5** saves `bench_camera.json`, **F9** reloads, **F6** prints. Timed runs: `BENCH_CAM=` `spin` (default —
  hold position, yaw-sweep in place, still shot matches the save), `fixed` (dead still), `orbit` (circle the
  saved point), `room` (orbit scene centre). Also: `BENCH_SPIN`, `BENCH_EXPOSURE`, `BENCH_ORBIT`, `BENCH_ROT_X`,
  `BENCH_CAMFILE`. Scripts: `Bin/frame.bat <model>` (curated per-model framing, one command → hero shot + bench),
  `Bin/view.bat <model>` (free-fly + F5 save), `Bin/shot.bat <model> [out] [spin|fixed|room|orbit|forward]`.
- [x] Promote `DeferredRenderer` into `GraphicsEngine` (engine owns the G-buffer + passes) — moved to
  `Source/Graphics/tge/render/DeferredRenderer.{h,cpp}` in `namespace Tga`; `GraphicsEngine` constructs one in
  `Init()` sized to `Application::GetRenderSize()`, exposes `GetDeferredRenderer()` + `IsReady()` / `OnResize()`.
  The bench now drives the engine-owned instance instead of owning its own. Forward path unaffected.
- [x] Forward pass for transparency — `DeferredRenderer::BuildFrame` now takes a `drawTransparent` fn and
  inserts a `transparent` pass between `lighting` and `composite`: alpha-blend into the lit HDR target,
  depth `ReadOnlyLessOrEqual` (test, no write). Bench classifies sub-meshes by material name
  (`glass`, `lamp_glass` default; `BENCH_TRANSPARENT_MATS` override, empty disables) — those go forward-PBR,
  the rest stay in the G-buffer. New `ModelInstance::Render(shader, meshIndices)` (hoisted setup). glass /
  lamp_glass cook.json baseColor alpha dropped to ~0.2–0.3. No back-to-front sort yet (fine for a few panes).
- [ ] Editor viewport renders through the deferred path  ← **Phase 3 gate**
- [ ] Editor debug views: G-buffer inspector, pass-timing panel, light-count / overdraw viz

> **Gate before Phase 3:** ~~render-graph skeleton~~ ✓ · ~~transparency forward pass~~ ✓ · editor viewport
> through the deferred path is the only gate item left (parallel — not blocking Phase 3 bench work).
- [x] Sky: `DeferredLightingPS` samples the environment cubemap along the view ray where depth == far

### New Sponza (Intel/EA Main) — DONE

- [x] Cooker `aliases` (cook.json) — texture-pack prefixes had drifted from the FBX material names (11 mappings)
- [x] Cooker `inputs` (cook.json) — explicit file→role per material, for oddly-named assets (DamagedHelmet's `gltf_embedded_*@channels=X.jpeg`)
- [x] `CacheCreateBuffers` hardened: null buffers + skip on failure, 4 GB guard, per-mesh diagnostics
- [x] Force `Vertex.position.w = 1` on FBX import (some exporters leave it 0 → mesh collapses)
- [x] User re-exported from Blender → mesh imports **clean** (the raw glTF→FBX machine conversion was the problem)
- [x] All 28 materials resolve: 24 fully textured (C/N/M), 4 constant-shaded (glass/lamp_glass_01/light_bulb/dirt_decal)
- [x] **DamagedHelmet** (Khronos) added — single mesh, C/N/M/**FX emissive**, loads + renders
- [x] `.tgo` + `.tgm` written for both (cooker `--tgo` probes cooked DDS on disk per FBX material →
  handles aliases + constants). `NewSponza.tgo` = 28/28 materials with textures assigned; `DamagedHelmet.tgo` = 1/1.
- [x] `Bin/view.bat <model>` (free-fly, F5 saves per-model viewpoint) and `Bin/shot.bat <model>` (screenshot+bench from saved view)

### Test assets available

- `sponza/Sponza.fbx` — the reliable draw-call / perf bench (120→28 merged, works perfectly)
- `main_sponza/NewSponza_Main_Yup_003.fbx` — bigger open scene, fully textured
- `damaged-helmet/source/DamagedHelmet.fbx` — small PBR prop with emissive + AO (for HDR/reflection/GI iteration)
- `main_sponza/textures/kloppenheim_05_4k.hdr` — a real sky HDR (needs equirect→cubemap; future IBL upgrade)
- Still no scene with strong in-context emissive light sources — Intel "Sponza Emissive/Curtains" pack would fill that

## Phase 3 — Clustered / tiled lighting  `[x]`

- [x] Move light data from the fixed `NUMBER_OF_LIGHTS_ALLOWED` array to a structured buffer — deferred path
  only: `DeferredRenderer` owns `StructuredBuffer<GpuLight>` (t15, 1024 cap) + count cbuffer (b6),
  `UploadLights()` per frame. Engine's forward/transparent path still uses the b2 8-light cbuffer (unchanged).
- [x] Lift the 8-light cap (deferred) — `BENCH_LIGHTS` up to 1024; authored `bench_lights_*.json` uncapped.
- [x] Compute cluster/tile assignment — `EngineAssets/Shaders/ClusterCullCS.hlsl`: froxel grid
  **32px tiles × 24 exponential depth slices** (50×29×24 = 34,800 clusters @ 1600×900), one thread/cluster,
  builds the view-space AABB, sphere-tests every light, writes a compact per-cluster index list
  (`kMaxPerCluster` = 256; excess silently dropped in pathologically dense clusters → dimming + faint
  tile-banding). `DeferredRenderer::CullClusters()` runs it as a `clusters` render-graph pass.
  Engine is **left-handed (+Z forward)** — cluster Z math must use positive view Z (cost a debug cycle).
- [x] Deferred lighting pass consumes the cluster grid — `DeferredLightingPS` maps pixel → cluster, loops
  only that cluster's lights. `BENCH_CLUSTERED=0` forces brute-force for A/B.
- [x] Re-sweep (TEST scene, 3D-lattice stress rig): `lighting` GPU pass **brute → clustered** —
  64 lights: 1.20 → **0.74 ms**; 1024 (all overlapping one courtyard): 18.5 → **4.9 ms** (3.8×, dense
  clusters genuinely hold 200+ lights — real work). Realistic loads stay sub-ms (authored 6-light rig
  = 0.17 ms). Frame 64: 2.4 → 1.9 ms. `clusters` CS pass 0.06 ms @64, 0.63 ms @1024.
- [~] Follow-up: a two-phase light-list prepass (atomic global list, no per-cluster cap) would remove the
  drop-at-cap dimming and flatten the CS cost — parked; current cap is fine for realistic scenes.
- [ ] Physical light units — replace the bench's magic-constant inverse-square rig with lumens/candela +
  explicit radius; pairs naturally with the structured light buffer

## Phase 3.2 — SSAO  `[x]`

- [x] Screen-space AO deferred pass — `EngineAssets/Shaders/SSAOPS.hlsl`: fullscreen, reads G-buffer
  world normal (t11) + depth (t14), 16-sample view-space hemisphere kernel rotated per-pixel by a hash,
  range-checked occlusion. `EngineAssets/Shaders/SSAOBlurPS.hlsl`: depth-aware 7×7 box blur (t18 raw → `myAo`).
  `DeferredRenderer::RenderSSAO()` runs both as an `ssao` render-graph pass after geometry.
- [x] Feed into the ambient term — `DeferredLightingPS` multiplies the blurred AO into `orm.r` before
  `EvaluateAmbiance` (ambient-only, not direct light). `gSsaoEnabled` flag in the b6 cbuffer.
- [x] Debug view (`BENCH_GBUF=8`) + `BENCH_SSAO` toggle (default 1).
- Cost ~0.33 ms full-res @1600×900. **Deferred (`SetCamera`) also now stashes `viewToProj`.**
- [ ] Follow-up: half-res + bilateral upsample; temporal accumulation (or lean on Phase 4.5 TAA) —
  parked, current full-res + blur is clean enough. Offline AO bake tool stays the high-quality path.

## Phase 3.4 — Emissive intensity (material-pipeline slice of Phase 6)  `[x]`

- [x] New `_FX` encoding — **r = emissive mask** (`max` of source RGB, so saturated hues survive; colour
  comes from albedo in the shader), **g = `emissiveStrength / MAX_EMISSIVE_STRENGTH`** (16, in `common.hlsli`),
  constant per material. Height dropped (unused).
- [x] `GBufferPS` + `PbrModelShaderPS`: `emissive = albedo.rgb * fx.r * (fx.g * MAX_EMISSIVE_STRENGTH)` →
  HDR into the `R11G11B10F` emissive G-buffer / forward radiance. `DeferredLightingPS` unchanged (adds the
  G-buffer emissive as-is).
- [x] Spaceship `cook.json` (`emissiveStrength: 9`) — the BLUE marker dot now self-illuminates as an HDR
  source (white-hot core + blue halo; **Phase 4 bloom** will make it read properly). `light_bulb` (New
  Sponza) `emissiveStrength: 4` re-cooked.
- Backwards compatible: assets with an emissive map but no explicit strength → `fx.g` = 1/16 → ×1 (current look).
- **Cooker bugs fixed along the way:** (1) `MatOverride::mergeFrom` clobbered `emissiveStrength` from the
  `*` defaults whenever the `emissive` colour was unset → now a `-1` sentinel merges independently; (2) the
  cooker re-ingested its own `*_C/_N/_M/_FX.dds` outputs as sources when `--in` == `--out` (Spaceship/) — a
  stale `_FX.dds` took the `PackedFx` passthrough → now skips its own output names.

## Phase 3.5 — Shadows  `[~]`

- [x] **Cascaded shadow maps for the directional light** — 4 cascades, 2048² `R32_TYPELESS` Texture2DArray
  (per-slice DSV, array SRV t19). Practical split (λ=0.75) between near and `min(farPlane, sceneRadius·4)`.
  Per cascade: view-frustum slice → world corners → bounding sphere → ortho light `Camera` (radius-ceil
  snapped). `DeferredRenderer::RenderShadows()` renders each cascade depth-only (`ShadowPS.hlsl` = empty,
  `PbrModelShaderVS`) by swapping `gss.SetCamera` + `UpdateGpuStates(true)`, restores the view camera after.
  `shadows` render-graph pass runs first. `BENCH_SHADOWS` toggle.
- [x] **PCF 3×3** — `SamplerComparisonState` (LESS_EQUAL, border=1), `SampleCmpLevelZero`, blended by
  `gShadowStrength`. Applied to the directional term only in `DeferredLightingPS`.
- [x] **Bias & filtering pass** (fixed the "shadows only look right up close" report — a fixed **NDC** depth
  bias becomes ~16 world units in a far cascade's huge depth range → peter-pan gap that closes as you
  approach a nearer, tighter cascade):
  - depth bias is now **world units ÷ per-cascade `orthoDepth`** (`gCascadeDepthRange[]`), slope-scaled by N·L
  - normal-offset bias in **shadow-texel units** × `gCascadeTexelWorld[]` (consistent across cascades)
  - **cascade blend** — lerp into cascade c+1 over the last 20% of c's split range (kills the seam banding)
  - **PCF 5×5**, shadow map **3072²**, `shadowFar = sceneRadius·1.3`, **λ=0.5** (near cascades cover a useful range, not crammed at the camera)
  - texel snapping (cascade centre → texel grid, no edge crawl on camera move)
  - `BENCH_SUN_PITCH` / `BENCH_SUN_YAW`; `BENCH_SHADOW_VIZ=1` / panel "Show cascades" tints by cascade.
- [x] **Sign bug fixed** — `dirLight.transform.GetForward()` is **L** (surface→sun, points up); the shadow
  camera was placed along +L (below the scene, looking up) → the floor self-shadowed everything. Now placed
  at the sun looking down (−L).
- Cost ~2.6 ms (4× full-scene re-render of New Sponza @2048²). Frame 1.8→4.2 ms with shadows.
- [ ] Follow-ups: **per-cascade frustum cull + res falloff for far cascades** (the 2.6 ms); alpha-tested
  casters; cascade-blend at splits.
- [x] Point / spot light shadows — shared 4096² depth atlas, 8×8 tiles of 512px, nearest
      <=8 casters by `lum/dist` per frame, re-rendered every frame. Spot = 1 tile (perspective,
      `near=far*0.05`, slope-scaled bias, 3x3 PCF). Point = 6 cube-face tiles (`CubeFace()`
      picks the face; uv inset stops PCF crossing face boundaries). `SetLocalShadows` /
      "Point/spot shadows" panel / `BENCH_LOCAL_SHADOWS`; `BENCH_GBUF=9` shows the atlas.
      Spot light type also landed: `DeferredLight` + cluster CS + `EvaluateSpotLight` +
      `"spot"` JSON block. Cost ~0.35 ms/spot, ~0.7 ms/point (full re-render per tile).
  - [x] Per-tile caster cull — `drawShadowCasters(const Camera&)` + `ModelInstance::Render(shader,
        Frustum)` sub-mesh cull to each shadow view (also gives directional per-cascade cull free).
        Budget tunables `localShadowMaxCasters` (4) / `localShadowMaxPoints` (2) + a brightness
        cutoff keep dim fill lights out of the atlas. Note: sub-mesh cull only bites when the
        scene is many discrete objects; a scene made of a few huge meshes still pays per tile.
- [x] In-game light editor — "Lights" panel: per-light translation / direction / cone / range
      drags, plus projected screen markers. (No 3D manipulator — `DebugDrawer` is 2D only.)
- [x] Screen-space / contact shadows — `ContactShadow()` in `DeferredLightingPS`: 16-step view-space
      ray-march toward the sun, re-projects each step to sample `GBufferDepth`, occluded when the ray
      passes behind a surface within `gContactThickness`. Directional light only, multiplied onto
      `sunShadow` alongside the CSM; skipped where CSM already shadows (`sunShadow <= 0.05`) or the
      surface faces away. Params in `ShadowParams` b9 (`gContactLength` / `gContactThickness`, took the
      `_shadowPad` slot); `Tunables.contactShadows/contactLength(45)/contactThickness(30)`; panel
      checkbox + 2 sliders under Shadows; `BENCH_CONTACT` env. Cost ~0.12 ms @1600x900. No artifacts
      on TEST / PillarTest; it's a fill for CSM gaps (floating props) — subtle when the CSM is tight.

## Phase 4 — HDR / exposure / bloom  `[~]`

- [x] Bloom: soft-knee prefilter (half-res) -> 13-tap COD downsample chain (6 mips,
      R11G11B10F) -> additive 9-tap tent upsample -> added in the composite. Shaders
      `BloomPrefilterPS` / `BloomDownPS` / `BloomUpPS`, shared `PostFxCommon.hlsli`.
- [x] Auto-exposure: HDR -> 64x64 log-luma -> box downsample to 1x1 -> temporal
      adapt in a persistent 1x1 ping-pong (`ExposureLumaPS` / `ExposureDownPS` /
      `ExposureAdaptPS`, framerate-independent). Manual-exposure + EV-comp override.
      Default is manual 1.0 (keeps the tuned tonemap look); auto is an opt-in toggle.
- [x] `DeferredCompositePS`: `hdr*exposure + bloom*intensity` then ACES tonemap ->
      backbuffer, replacing the plain engine tonemap in `DeferredRenderer::Composite`
      (falls back to the engine tonemap when post-fx is off). New `postfx` render-graph
      pass; cbuffer `PostFxParams` b10, `LinearClamp` s3, post-fx SRVs t0..t2.
- [x] ImGui "Post FX" panel section (bloom threshold/knee/intensity, auto/manual
      exposure, key/min/max/adapt-speed, EV comp). `BENCH_POSTFX` env toggle.
- [ ] Full HDR pipeline for the forward path too (not just deferred)
- [ ] Editor-side exposure/curve controls + per-camera exposure
- [ ] Lens dirt / chromatic-aberration options on the composite

## Phase 4.5 — TAA + motion vectors + DLSS  `[ ]`

Grouped as one initiative (2026-09-12) rather than kept separate: TAA and DLSS both consume the
exact same jittered-rendering + motion-vector + depth pipeline, so building that once and layering
two resolve strategies on top of it is the right shape, not two unrelated features.

- [ ] Per-object motion-vector G-buffer channel (needs prev-frame transforms — a `prevWorldMatrix`
      alongside each `ModelInstance`'s current one, fed into the geometry pass). Build this first,
      regardless of what follows — it's also a prereq for reflection/GI denoising (including
      Stage 3's DXR GI temporal accumulation) and unlocks motion blur later.
- [ ] Jittered projection (Halton sequence sub-pixel offset per frame) + DIY temporal resolve with
      neighbourhood clamping (reject ghosting on disocclusion/fast motion) + history buffer
      management (disocclusion detection, first-frame/resize handling — the part that actually
      costs engineering time). A real, portable, engine-owned AA baseline — there is currently
      none beyond whatever the hardware defaults to, which for this deferred renderer is
      effectively nothing.
- [ ] **DLSS** (new item, not part of the original plan) — layered on top of the same jitter/
      motion-vector/depth infrastructure as a *second* resolve path behind a toggle, once DIY TAA
      is proven stable (don't bring in an unfamiliar external SDK before the surrounding
      infrastructure is validated). Integration path: NVIDIA Streamline (the modern umbrella API
      covering Super Resolution/Frame Generation/Ray Reconstruction) rather than raw NGX — this is
      the first non-Microsoft, license-bound binary SDK dependency this engine would take on
      (redistributable DLL + NVIDIA license terms). Keep DIY TAA as the non-NVIDIA-hardware
      fallback.

## Phase 5 — Reflections  `[x]` (multi-probe blend is the only follow-up)

- [x] Screen-space reflections — `SSRPS` view-space linear march (48 steps) + binary refine
      at **half res**, `SSRApplyPS` resolves + bilinearly upsamples into HDR. Roughness-cutoff /
      distance / edge / fresnel fades. `SsrCb` b8, `mySsrTex` RGBA16F, `SetSSR` / "SSR" panel /
      `BENCH_SSR`. ~0.4 ms full-res. Verified (floor mirrors geometry with fades off); needs a
      glossy surface to read. TODO: half-res + roughness blur, temporal accumulate, hi-Z trace.
- [~] Reflection probes — Stage A done: `CubemapPrefilter` (from Tutorial-22) pulled into the game,
      one probe re-captured every N frames (skybox + `DrawPbr` per face -> GGX prefilter -> IBL cube).
      Panel toggle / interval / "Recapture now"; `BENCH_PROBE*` env. Metals now reflect the real scene.
- [~] Stage B: **box parallax correction** (`BoxParallaxCorrect` in `EvaluateAmbiance`, b12 probe box,
      roughness-gated + edge-faded to kill swirl on near-mirror flats) + **artist placement** via
      `bench_probes_<scene>.json` + panel pos/box drags. TODO: multiple probes + nearest/blend selection.
- [x] Stage C: SSR-over-probe compositing — lighting pass emits probe IBL specular to MRT1
      (`myIblSpecTex`); SSR resolve does `hdr += conf·(ssrRadiance − iblSpec)` == `lerp(probe, ssr, conf)`.

## Phase 6 — Emissive global illumination  `[~]`

- [x] Real emissive **intensity** — see **Phase 3.4**
- [x] **Method decided: lazy SH irradiance volume** (DX11, no hardware RT — at the time, a DX12 port
      was judged a full backend rewrite not worth it mid-project; **superseded 2026-09 — see Phase 7**,
      the DX12 port is now underway specifically to unlock hardware RT for a real DDGI rewrite).
- [x] **Stage 1 — SH9 irradiance volume.** Grid of L2-SH probes (auto from bounds ~360u spacing,
      or `bench_gi_<scene>.json`). Each probe: tiny 16² forward cube capture (frustum-culled) →
      `GiProjectSHCS` folds it to SH with temporal hysteresis. Volume is **primed** over ~1 s at load
      then costs nothing (optional slow trickle for dynamic lights). Lighting samples it via
      `EvaluateGI()` (trilinear + backface weight), added to the ambient diffuse. Emissive geo is
      captured lit so it bounces; multi-bounce via the capture re-sampling the volume.
      `SetGiVolume` (b13), SH buffer (t22), panel section, `BENCH_GI*` / `BENCH_GI_VIZ`.
      **Steady-state cost ~+0.2 ms** (sampling only).
- [ ] Stage 1b: Chebyshev visibility (per-probe distance moments) to kill thin-wall leak; probe
      relocation for probes stuck inside geometry; capture with SSAO/local shadows.
- [ ] Stage 2: per-frame dynamic — SDF built in compute, probe rays marched in a CS (real DDGI).
- [ ] GI method decision — DDGI-style irradiance probes (leading candidate) vs RSM/LPV vs voxel CT
- [ ] Probe placement / update budget, leak reduction
- [ ] Combine with SSR for specular GI

## Phase 7 — DX11 → DX12 → DXR backend port  `[~]`

Started 2026-09-10. Goal: replace the raw-DX11 rendering backend with a thin RHI seam,
implement a DX12 backend behind it, then add DXR hardware ray tracing — mainly to give
Phase 6 GI a real hardware-traced DDGI rewrite instead of the SH-volume software path,
and to unlock RT reflections/shadows/AO for Phase 5/3.5 later. Full plan:
`~/.claude/plans/distributed-wishing-candle.md` (not in-repo — Claude Code plan file).
Progress log: memory `p5g3-dx12-port`.

**Decision:** hand-written minimal RHI (~48 methods) covering exactly what this engine
does, *not* adopting NVIDIA NVRHI — NVRHI's `BindingLayout`/`BindingSet` model doesn't
match the engine's free `register(b#/t#/s#/u#)` convention, and adopting it would touch
every shader + every bind site (more churn than writing the seam by hand). New code
lives in `Source/Application/tge/rhi/` (`Tga::rhi` namespace; in Application rather
than a separate lib, to avoid a link cycle with `Tga::DX11`), with a `dx11/` backend
subfolder. Four stages:

- **Stage 1 — COMPLETE** (functionally; see the honest exceptions list below) — RHI seam +
  DX11 backend at parity, zero visible change, heavily verified at every step:
  - [x] Step 0 — RHI interface (`Handles.h`/`Descs.h`/`Device.h`/`CommandContext.h`) +
    DX11 backend (`Dx11Device`, `Dx11CommandContext`, gen-checked handle pools) wrapping
    the pre-existing `Tga::DX11` statics.
  - [x] Step 1 — `DX11.{h,cpp}` becomes a thin facade over `rhi::IDevice`; shader cache
    (`PixelShader`/`VertexShader`/`ComputeShader`) moved to `rhi/ShaderCache.h`.
  - [x] Step 2 — `TextureResource`/`RenderTarget`/`DepthBuffer` gain lazy `GetSrv()`/
    `GetRtv()`/`GetDsv()` via a `MigrationView<Handle>` bridge (`WrapNative*`).
  - [x] Step 3 — input layouts become data (`rhi::InputElement` lists), built + cached
    by the backend (`CreateInputLayoutNative`) instead of ad hoc `D3D11_INPUT_ELEMENT_DESC`
    arrays scattered per shader.
  - [x] Step 4 — `GraphicsStateStack` split onto the RHI: blend/depth/raster/sampler
    state and all b0–b13 engine cbuffers now go through `ICommandContext`/
    `IDevice::AllocateDynamicConstants` (a proper bump-allocated upload ring, not
    one-buffer-per-alloc) instead of raw D3D11 state objects + `Map`/`Unmap`.
  - [x] Step 5 — `GpuProfiler` timestamps + `GpuMarker` debug events moved onto the RHI
    (`TimestampQueryHandle`, `ctx.PushMarker/PopMarker`); perf overlay API unchanged.
  - [x] Step 6 — 2D drawers (`CustomShapeDrawer`/`LineDrawer`/`SpriteDrawer`),
    `FullscreenEffect`, `SpriteShader` off raw `DX11::Context->`.
  - [x] Step 7 — `DeferredRenderer.cpp` (~1600 lines, the largest single file), migrated
    pass-by-pass since the passes share one large SRV/sampler/cbuffer bind function:
    - [x] sub-pass 1 — all 11 constant buffers → new `rhi::ConstantBuffer` helper
      (owns buffer + stage + slot; `Update`/`Bind` replace the `CreateBuffer`+
      `Map`/`Unmap`+`XSSetConstantBuffers(slot)` boilerplate — one place to touch for DX12).
    - [x] sub-pass 2 — render targets / viewports / clears → `SetTargets()` helper +
      `ctx.SetRenderTargets`/`SetViewport`/`ClearRenderTarget` via `RenderTarget::GetRtv()`/
      `DepthBuffer::GetDsv()` (8 sites: SSAO ×2, SSR ×2, transparent, G-buffer 4-MRT,
      lighting-resolve 2-MRT, post-fx fullscreen).
    - [x] sub-pass 3 — the 4 engine samplers (point/shadow-cmp/linear/GI-linear) →
      `rhi::SamplerHandle` via `IDevice::CreateSampler` + `ctx.SetSampler`.
    - [x] sub-pass 4 — structured buffers/SRVs/UAVs/textures. New `rhi::StructuredBuffer`
      helper (owns buffer+SRV+optional UAV): light buffer, cluster index/count buffers,
      GI SH-coefficient buffer, local-shadow transform buffer. Shadow-cascade texture
      array + local-shadow-atlas texture (real `ID3D11Texture2D`/DSV work, using one
      `Format::D32_Float` to auto-derive the typeless resource + depth SRV + DSV).
    - [x] sub-pass 5 — compute dispatch: `ctx.SetComputePipeline` (hash-cached
      `CreateComputePipeline` off the shader module) + `ctx.Dispatch` for cluster
      culling and GI SH projection.
    - [x] sub-pass 6 — remaining fullscreen-draw plumbing: `BindFullscreen`, `RenderSSAO`/
      `RenderSSR` SRV binds, `BindGBufferSrvs`/`UnbindGBufferSrvs`, `ResolveLighting`/
      `Composite`/`DebugBlit` draws, and the bloom/auto-exposure chain in `RenderPostFx`
      (`PostFxFullscreen` signature `ID3D11ShaderResourceView* const*` → `const rhi::SrvHandle*`,
      6 call sites updated). Raw D3D11 refs in the file: 48 → **2** (both `GiProjectProbe`'s
      `aCubeSrv` param, deliberately deferred to step 9 — public API signature change).
      **Step 7 is now fully complete** — `DeferredRenderer.cpp` is ~100% RHI-native.
  - [x] Step 8 — `CubemapPrefilter.cpp` (`Source/Game/source/`): persistent sampler +
    4 compute constant buffers → RHI, compute shader bind/dispatch → `ctx.SetComputePipeline`/
    `SetSampler`/`Dispatch`. Per-call scratch textures/SRVs/UAVs (created fresh on every
    GI/reflection-probe capture — up to thousands of times per bake) deliberately left raw:
    wrapping them into the RHI's permanent handle pools has no matching `Destroy()` here and
    would leak one pool slot + GPU reference per capture. DirectXTex export/DDS-load functions
    also left raw (unused, and DirectXTex's `*11`→`*12` loader swap is a Stage 2 task). Raw
    D3D11 refs: 54 → 22 (all documented, deliberate).
  - [x] Step 9 — `GameWorld.cpp` GI/reflection-probe capture: skybox fullscreen-triangle
    draw → RHI (mirrors `BindFullscreen`). Closed out both spots deferred from steps 7-8:
    `GiProjectProbe`'s param is now `rhi::SrvHandle` (`DeferredRenderer.h`/`.cpp` are now at
    **zero raw D3D11 references** — down from ~390 at the start of step 7), and `CubemapData`
    gained a leak-safe `GetSrv()` bridge (`MigrationView`, invalidated on each `Reset()`).
    Raw D3D11 refs: `GameWorld.cpp` 17 → 1 (bench screenshot capture, step-12 territory).
  - [x] Step 10 — `Source/Graphics/tge/videoplayer/video.cpp`: DYNAMIC texture + Map/Unmap
    per decoded frame → `IDevice::CreateTexture`/`CreateSrv` (once) + new `ctx.UpdateTexture`
    (every frame). Also added `IDevice::GetNativeSrv` (raw-pointer bridge for legacy
    `TextureResource` construction). Raw D3D11 calls: 6 → 0. Not wired into GameMain/
    GameEditor, so verified by building + running the standalone `Tutorial-13_Video` sample
    live for 16s (stable memory, no crash).
  - [~] Steps 11+12 (combined) — substantially done, not fully closed. Converted (2 parallel
    subagents on disjoint files + several pieces done directly, all builds/verification/commits
    centralized): `Model::MeshData` vertex/index buffers + `ModelShader::RenderMesh` (highest-
    traffic path in the port — every mesh, every scene), `DefaultEditorGraphics`/`SceneUtil`'s
    constant buffers, `FullscreenPixelateEffect` (a fullscreen effect missed back in step 6),
    `TextService`'s font atlas, `ImGuiInterface`'s device/context bridge, `TextureManager`'s
    procedural fallback textures, plus a dead-forward-declaration sweep. `ModelShader.cpp`/`.h`
    and `DeferredRenderer.cpp`/`.h` are now at **zero** raw D3D11 references.
    **Honest remaining gap** (see memory `p5g3-dx12-port` for full detail): `RenderTarget`/
    `DepthBuffer`/`TextureResource`'s own public API still takes/returns raw `DXGI_FORMAT`/
    `ID3D11ShaderResourceView*` — these are called from dozens of sites across the whole
    codebase, and rewriting them is a genuinely separate, high-ripple task not attempted here.
    This is also why the `DX11::Device/Context/SwapChain` statics can't be deleted yet — the
    wrapper classes' own `.cpp` files still need them. DirectXTex-based real asset loaders stay
    raw by design (Stage 2's `*11`→`*12` loader swap). A few per-call scratch-resource sites
    stay raw for leak-avoidance (`CubemapPrefilter`, `Viewport.cpp`'s mouse-picking readback).
  - [x] Wrapper-API rewrite — done for its actual scope: `RenderTarget::Create`'s two "create
    fresh" overloads (`DXGI_FORMAT` → `rhi::Format`), the one part of `RenderTarget`/
    `DepthBuffer`/`TextureResource` the raw-format complaint actually applied to
    (`DepthBuffer::Create` never took a format param; `TextureResource`'s raw-pointer API is
    used far too pervasively across the whole codebase to change in this pass). Added
    `rhi::Format::R8G8B8A8_Typeless` + two new native-pointer bridges (`GetNativeRtv`,
    `GetNativeTexture`) to support it. Updated ~20 call sites across `DeferredRenderer.cpp`,
    `Viewport.cpp`, `GameWorld.cpp`, `RenderResourcePool.h`, plus 3 sites outside this repo's
    tracked tree (2 Tutorials, 1 Example). The two "adopt the swapchain backbuffer" overloads,
    `Clear()`/`SetAsActiveTarget()`, and all of `DepthBuffer`'s internals stay raw on purpose —
    both are called during `DX11::Init` **before the RHI device object exists**, a real
    bootstrap ordering constraint, and the Clear/SetAsActiveTarget pair is besides the single
    hottest per-frame path in the renderer. **Verified as the highest-stakes single change in
    the whole port** (every render target the engine creates goes through it): both `.sln`
    build clean, Sponza + Room + auto-exposure-chain screenshots pixel-identical, editor ran
    18s+ clean including Viewport's trickiest TYPELESS+sRGB-RTV+linear-SRV case.
  - Stage 1 is now considered **functionally complete** — not literally "0 raw D3D11 anywhere"
    (a short, fully-documented list of deliberate exceptions remains: bootstrap-timing-bound
    code, a couple of leak-avoidance cases, and `TextureResource`'s stable legacy API surface)
    but every remaining site is either load-bearing by design or outside the engine runtime.
    See memory `p5g3-dx12-port` for the full accounting. Moving to **Stage 2**.
  - **Found + fixed a real bug along the way** (not port-scope, a genuine engine
    correctness bug the port's extra scrutiny surfaced): `Pool<ComPtr<T>>::Get()` in the
    new RHI backend was itself broken (`&s.value` invoked `ComPtr`'s overloaded out-param
    `operator&` instead of taking a real address), so once the sampler-object migration
    (step 4) actually started *binding* wrapped samplers, all 9 engine texture samplers
    silently resolved to a null D3D11 sampler state — every textured surface in every
    scene rendered with heavy directional streaking (every sample, every mip, garbage
    filtering). Caught by eye (a Sponza screenshot compared against a known-good
    reference), root-caused via targeted instrumentation, one-line fix (`std::addressof`
    instead of `&`). Verified against the reference image. Repo now under git
    (`github.com/Attomannen/TGE-DX12-Port`, private) specifically so this kind of
    regression is bisectable going forward.
- **Stage 2** `[~]` — DX12 backend behind the same seam. **Milestone 1 done** (2026-09-11):
  `Source/Application/tge/rhi/dx12/` (`Dx12Device`/`Dx12CommandContext`/`Dx12DescriptorHeap`)
  implements real device/adapter/queue creation, a flip-model swapchain + `Resize()`, 4
  descriptor heaps (RTV/DSV/CBV-SRV-UAV/Sampler) with free-list slot allocation, per-frame
  command-allocator + fence pacing, `CreateBuffer`/`CreateTexture`/`CreateSrv`/`CreateUav`/
  `CreateRtv`/`CreateDsv`/`CreateSampler`/`Destroy`, and `AllocateDynamicConstants` (an
  UPLOAD-heap ring, same design as DX11's). Verified against the real RTX 5060 Ti via a
  standalone smoke test (not part of the engine build): 180 frames presented with an
  animated clear color, plus a mid-run `Resize()` exercising full backbuffer/depth
  teardown-recreate — all clean, no crashes, no validation errors. DX11 regression-checked
  throughout (this is purely additive code; `DX11::Init()` untouched).
  **Milestone 2 done** (same day): the two root signatures (graphics/compute, sized from a
  survey of every `register()` in `EngineAssets/Shaders/*.hlsl*` — b0-b13, t0-t23, s0-s5,
  u0-u3), root-CBV binding for constant buffers (D3D12 root arguments are sticky across
  draws, matching this engine's bind-once-draw-many pattern with no extra tracking), a
  desc-hash PSO cache mirroring `Dx11Device`'s own, and the full `Dx12CommandContext` bind/
  draw/dispatch surface. The hard problem solved here: a DX12 descriptor table must be
  contiguous in the bound heap, but this engine binds arbitrary resources to arbitrary
  slots — solved with a permanent non-shader-visible creation heap plus per-frame shader-
  visible scratch heaps that get the actual bound set copied in right before each draw,
  with dirty-tracking specifically needed for samplers (a hard, universal 2048-descriptor
  heap ceiling that a naive "copy every draw" scheme would blow through on a busy scene).
  Verified via two standalone tests against the real RTX 5060 Ti: an animated-color triangle
  via the SV_VertexID trick, and a second pass drawing a real indexed quad through actual
  vertex/index buffers (`ModelShader::RenderMesh`'s exact draw shape) with a second, distinct
  PSO cached alongside the first — both 180 clean frames, no crashes, no validation errors.
  **Still not implemented** (assert clearly if reached, nothing needs them yet):
  `ClearUnorderedAccessFloat`, `UpdateTexture`'s mid-lifetime path, `GenerateMips` (no DX12
  equivalent, needs a compute shader), timestamp queries, PIX markers, ImGui interop.
  **Milestone 3 (in progress, 2026-09-11)**: `RenderTarget`/`DepthBuffer` storage migrated
  to hold `rhi::Handle`s directly on DX12 (a `MigrationView<TextureHandle>` added alongside
  the existing Rtv/Dsv/Srv ones, reusing its established "copy does not propagate, owner
  destroys" semantics — no new ref-counting needed). `TextureResource` needed zero changes
  (`GetSrv()` already preferred a populated `myRhiSrv` over the raw-pointer path).
  `RenderTarget::Clear`/`SetAsActiveTarget`/`GetRtv` and `DepthBuffer::Clear`/
  `SetAsActiveTarget`/`GetDsv` all branch on backend now. The DX12 swapchain's backbuffer
  changes resource every frame (flip-model, unlike DX11's single stable backbuffer) — solved
  with a dynamic-resolve mode (`RenderTarget::CreateFromDeviceBackBuffer`) that re-queries
  `IDevice::GetBackBufferRtv()` every call instead of caching a handle.
  `DX11::Init()`/`ResizeToWindowSize()` now branch on a `TGE_RHI=dx12` env var (no CLI-flag
  plumbing exists yet) into `InitDx12()`/`ResizeToWindowSizeDx12()`, which construct the RHI
  device directly against the real window handle and populate `BackBuffer`/
  `BackBufferNoSrgbConversion`/`DepthBuffer` via the new factories, skipping the raw D3D11
  device/swapchain block entirely. Also fixed a real bug this surfaced: `DX11::EndFrame()`
  unconditionally called `DX11::SwapChain->Present(...)`, which is null under DX12 (the
  DX12 backend presents its own swapchain from inside `Dx12Device::EndFrame`) — now branches.
  **First real end-to-end run of the DX12 backend through the actual engine init path**
  (not an isolated smoke test): `GameEditor_Debug.exe`/`GameMain_Debug.exe` launched with
  `TGE_RHI=dx12` now get all the way through device/adapter/swapchain creation, backbuffer +
  depth-buffer setup, and reach the first per-frame `BeginFrame()`. Caught and fixed one bug
  along the way: `DepthBuffer::Create`'s DX12 branch was passing the typeless resource format
  (`R32_Typeless`) instead of the logical depth format (`D32_Float`) to `CreateTexture`, which
  needs the logical format itself to correctly derive both the typeless resource format *and*
  the DSV clear-value format — D3D12 rejected the untyped clear value with `E_INVALIDARG`.
  Fixed, rebuilt, reverified via the same launch.
  It currently stops at `Dx12Device::GetNativeContext()` (an intentional `assert(false)`) —
  this is the well-known, already-scoped-separately gap: `ImGuiInterface::Init()` runs
  unconditionally in Debug builds (both Game and GameEditor) and calls
  `ImGui_ImplDX11_Init` via the `GetNativeDevice`/`GetNativeContext` escape hatch, which has
  no DX12 equivalent yet (`imgui_impl_dx11` → `imgui_impl_dx12` is explicitly a Stage 2
  outline item, not part of milestone 3's scope). DX11 regression-checked throughout
  (process-liveness smoke test after every change — zero impact on the shipping path).
  **`imgui_impl_dx12` swap — DONE, verified against real UI, same day.** Vendored
  `imgui_impl_dx12.h`/`.cpp` (ImGui 1.91.6, matching the already-vendored `imgui_impl_dx11`)
  into `Source/External/imgui/`. `ImGuiInterface.cpp`'s `Init`/`PreFrame`/`Render`/`Shutdown`
  all branch on `GetBackend()`. New `IDevice` escape hatches for what DX12 needs that DX11
  doesn't (`GetNativeCommandQueue`/`GetNativeCommandList`/`GetImGuiSrvDescriptorHeap`/
  `ImGuiFontSrvCpuHandle`/`ImGuiFontSrvGpuHandle` — DX12 has no persistent "device context"
  the way `GetNativeContext` assumes, so a few dedicated accessors were added rather than
  overloading that one); `GetNativeDevice()` is now real on DX12 too (`ID3D12Device*`).
  `Dx12Device` gained a small persistent shader-visible descriptor heap
  (`myImGuiSrvHeap`, capacity 64) reserved exclusively for ImGui's own font-atlas descriptor —
  separate from the engine's own per-frame scratch heaps, which reset every `BeginFrame` and
  can't hold anything that must survive across frames. `ImGuiInterface::Render()` binds this
  heap via `SetDescriptorHeaps` itself before calling `ImGui_ImplDX12_RenderDrawData` (that
  backend, unlike DX11's, expects the caller to bind heaps — confirmed by reading its source,
  not guessed); safe because ImGui renders last in the frame (right before `Present`) and
  nothing else touches a descriptor table afterward. Multi-viewport
  (`ImGuiConfigFlags_ViewportsEnable`, dragging a panel into its own OS window) is gated to
  DX11-only — this ImGui version's DX12 backend implements no `Renderer_CreateWindow`/
  `RenderWindow`/`DestroyWindow`, so leaving it on under DX12 would crash the first time a
  panel was torn out, a self-inflicted regression worth avoiding even though nothing exercised
  it yet.
  Also added a real, non-sRGB backbuffer RTV (`Dx12Device::myBackBufferRtvNoSrgb`, mirroring
  `DX11::BackBufferNoSrgbConversion`) — `GetBackBufferRtv(bool srgb)` previously ignored the
  flag and always returned the sRGB view; ImGui renders onto the non-sRGB one specifically
  because its colors are already gamma-encoded (writing through an sRGB RTV would double-apply
  gamma). Premake regenerated for both workspaces to pick up the two new vendored files
  (`External.vcxproj`'s file list is a static glob snapshot); toolset stayed `v145`, no
  regression this time.
  **Two more real bugs found and fixed via actually running it, not by inspection:**
  - `Dx12Device::CreateTexture`'s `RenderTarget`-bind branch unconditionally set the
    `D3D12_CLEAR_VALUE`'s format to `ToDxgi(desc.format)` — correct for the common case
    (a concrete format), but wrong for the deliberately-*typeless* case
    (`RenderTarget::Create`'s 4-arg overload, used by e.g. Viewport's TYPELESS+sRGB-RTV+
    linear-SRV editor render target): a clear value's format must be concrete, and
    `CreateTexture` has no visibility into which `RtvDesc::formatOverride` a later `CreateRtv`
    call will actually view it as. `D3D12CreateCommittedResource` rejected the typeless clear
    value with `E_INVALIDARG`, an assert away from a crash the very first time the editor
    created its own viewport render target (i.e. immediately after the main UI chrome
    finished its first frame). Fixed by adding `rhi::IsTypeless(Format)` (`Format.h`/`.cpp`,
    alongside the existing `IsDepth`) and skipping the clear value entirely for a typeless
    format — legal in D3D12 (just forgoes the fast-clear hint, irrelevant for these small,
    infrequently-cleared targets).
  - Caught live, with the user in the loop: after the fix above, the editor rendered a full,
    real, interactive frame (docked panels, menu bar, asset browser with a directory tree and
    file list, hover/selection highlighting) confirmed visually by the user — the first time
    any real UI has rendered through this DX12 backend. Clicking an asset (a `.tgs` scene file)
    then hit `Dx12Device::GetNativeSrv`'s intentional `assert(false)` — but this is a
    *different*, already-catalogued gap, not a new one: `TextureManager.cpp`'s texture-loading
    paths (`CreateSolidSrv` for the 3 procedural fallback textures, `CreateTextureFromTarga`)
    still extract a raw `ID3D11ShaderResourceView*` via `GetNativeSrv`, part of the
    Stage-1-accounting's documented "DirectXTex-based real asset loaders stay raw by design"
    gap (the plan itself defers the `*11`→`*12` DirectXTex loader swap to Stage 2). Loading a
    scene pulls in real textures, hitting this immediately — expected, not a regression from
    the ImGui work.
  DX11 regression-checked after every change (process-liveness + `Game.sln`/`GameEditor.sln`
  both build 0 errors throughout).
  **DX12 IBL cubemap prefiltering — fixed, RESOLVED (2026-09-12).** `CubemapPrefilter.cpp`
  had been reporting `Failed to load PrefilterSpecularCS or PrefilterDiffuseCS` on every DX12
  run all session (silently skipping specular/diffuse IBL prefiltering entirely, and — same
  root cause — silently skipping the skybox draw in every DX12 GI-probe capture). Root cause,
  found across 3 layers of the same underlying bug:
  1. The "did it load" checks (`CubemapPrefilter.cpp` ×3 call sites, `GameWorld.cpp` ×2) tested
     `shader->shader` — the DX11-only raw `ComPtr<ID3D11ComputeShader/VertexShader/PixelShader>`
     — which `DX11::ForceLoad*Shader` deliberately never populates on DX12 (there's no
     `ID3D11Device` to create it from). This made the check permanently read as "failed" on
     DX12 regardless of whether the shader genuinely loaded. Fixed by switching all 5 sites to
     the backend-agnostic `->module.IsValid()`, the same idiom `Dx12CommandContext::
     GenerateMips` already used for its own VS/PS validity check.
  2. That exposed a second, real bug the first one had been masking: `Dx12Device::
     CreateShaderModule` allocated a "valid" module handle even for **zero-byte bytecode** —
     so a shader file that genuinely failed to load (e.g. `CubemapPrefilter`'s "try
     `data/shaders/X` first, fall back to `Shaders/X`" logic, where the first path doesn't
     resolve to a real cooked asset) still reported `module.IsValid() == true`, skipping the
     fallback and handing `CreateComputePipelineState` empty bytecode (`0x80070057`/
     `E_INVALIDARG`, debug layer: *"A valid compute shader must be specified"*). DX11 never hit
     this because its real `CreateComputeShader(nullptr, 0, ...)` call fails its own HRESULT
     check naturally. Fixed: `CreateShaderModule` now returns a null handle for `size == 0`,
     matching DX11's real behavior.
  3. Fixing that let the prefilter's real compute dispatches run for the first time ever on
     DX12 — which promptly hit the sampler descriptor-scratch-heap's hard 2048 ceiling (see
     milestone 3's `GiProjectProbe` section below) mid-frame, since `GenerateMips`'s mip chain
     and `GeneratePrefilteredCubemap`'s per-mip dispatch both rebind the *same* one fixed
     sampler dozens to hundreds of times per cubemap. Root cause: `Dx12CommandContext::
     SetSampler`/`SetShaderResource`/`SetUnorderedAccess` marked their descriptor table dirty
     on **every call**, even when the bound handle hadn't actually changed — so a caller
     rebinding an unchanged sampler every loop iteration burned a fresh table range each time
     for nothing. Fixed generally (not per-caller): all three now compare against the
     previously-bound handle and only mark dirty on an actual change — safe because a D3D12
     root descriptor table stays bound across draws until explicitly rebound, so skipping the
     reflush when nothing changed is correct, not just faster.
  **Verified**: DX12 runs 200 frames clean (zero `DEVICE_HUNG`, zero asserts) with the real
  prefilter pipeline now executing (`Prefiltering 8 mips` → `Generated prefiltered cubemap`
  actually succeeding, previously always bailing out before reaching any GPU work). Re-ran
  with the debug layer active: no new validation errors (the one isolated startup swapchain
  message from the `DEVICE_HUNG` fix remains, unrelated). `BENCH_SCREENSHOT` comparison
  DX11 vs DX12: visually consistent, no regression. DX11 regression-checked throughout.
  **Net for milestone 3 so far**: DX12 now brings up its device, swapchain, backbuffer,
  depth buffer, AND a fully real, interactive ImGui UI, through the actual engine bootstrap —
  the frontier has moved from "can't create a depth buffer" to "can't load a texture asset."
  **`TextureManager` DX12 texture loading — DONE, same day.** DirectXTex's `*11`-suffixed
  loaders take an `ID3D11Device*` directly (no DX12 equivalent vendored); rather than adopt
  the heavier DX12 loaders (their own `ResourceUploadBatch` + queue, handing back a raw
  `ID3D12Resource*` with no adoption path into the RHI pool), used DirectXTex's
  backend-agnostic CPU-side decode (`LoadFromDDSFile`/`LoadFromWICFile` → `ScratchImage`) fed
  through the same `IDevice::CreateTexture(desc, subresources[], count)` path the procedural
  fallback textures already used — `Dx12Device::CreateTexture`'s upload path was already
  fully general over mip count and BC-compressed formats. New `TextureManager::LoadTextureDx12`
  mirrors the DX11 loading block's exact trial order and gating; cubemaps/3D textures are
  explicitly rejected with a clean error (not supported by this loader — `CubemapPrefilter`
  handles those separately). `TextureResource` gained a shared `myRhiTexture` (hoisted up from
  `RenderTarget`/`DepthBuffer`, deduplicating their identical declarations) and a new
  `SetRhiTexture()` bridge for non-member code. Found and fixed two more real, **separate,
  blocking** bugs along the way (not part of this fix but prerequisites to test it at all):
  `DX11::ForceLoad{Vertex,Pixel,Compute}Shader` unconditionally dereferenced `DX11::Device`
  (null under DX12) — would crash on the very first shader load; and `Shader::SetInputLayout`
  unconditionally called the DX11-only `CreateInputLayoutNative` bridge instead of just using
  the already-set `myInputElements` data DX12's PSO builder needs. Also added `fflush(stdout)`
  to `Log::LogWrite` (a genuine, permanent fix — stdout is fully-buffered when piped/redirected,
  so a real crash before a flush silently loses all prior console output, which is exactly what
  made the first crash here look silent).
  **Verified with real assets, not just "didn't crash":** `T_Default_c.dds` (1024×1024, 11 mip
  levels — a full real mip chain) loads and creates correctly; cubemap DDSes hit the clean
  rejection path; the engine then proceeds through `SpriteDrawer`/`ModelDrawer`/
  `DeferredRenderer::Init()` (GI volume, shadow atlas, cluster grid, G-buffer all built) before
  hitting a *different*, already-documented gap: `Dx12CommandContext::UpdateTexture`'s
  mid-lifetime path (needed by `TextService`'s font atlas) — flagged as not-yet-implemented
  back when milestone 2 shipped. DX11 regression-checked via a full `BENCH_SCREENSHOT` Sponza
  run (not just process-liveness, given this touches the hottest texture-loading path in the
  engine) — pixel-identical to the reference, before and after removing debug instrumentation.
  **Frontier moved from "can't load a texture asset at all" to "can't re-upload a texture
  after creation."**
  **`UpdateTexture` + `GenerateMips` — DONE, same day.** `UpdateTexture` (mid-lifetime full-
  subresource-0 overwrite): records into the current frame's own command list via a fresh
  UPLOAD-heap staging resource (can't use the synchronous initial-data path, which needs its
  own command list + a blocking wait). Added genuinely new, reusable infra for this:
  `Dx12Device::KeepAliveUntilFrameRetires`, a per-frame-in-flight deferred-release list freed
  at that slot's next `BeginFrame` (safe because the fence wait right above it already proves
  the GPU is done 2 frames back). `GenerateMips` has no DX12 native equivalent — implemented as
  a chain of fullscreen-copy blits (one mip at a time, bilinear-sampled — an adequate box-filter
  approximation for a font atlas), reusing the engine's existing `PostprocessVS`/
  `PostprocessCopyPS` shader pair via `DX11::Load{Vertex,Pixel}Shader` (already DX12-safe from
  this session's earlier shader-loading fix) rather than compiling anything new. Widened the
  interface to `GenerateMips(SrvHandle, TextureHandle)` since DX12 needs the owning texture (to
  build per-mip RTVs/SRVs) and the SRV pool doesn't track that back-reference — the one real
  call site already had both handles at hand. Handles the real complication that a mip chain
  needs several subresources in different states at once, which `TextureRec`'s single
  whole-resource state can't express, via a local per-call per-subresource state tracker.
  Also fixed a second real bug found immediately after, in the same `TextService.cpp` function:
  its font-atlas `TextureResource` wrapper would have DOUBLE-OWNED (and double-destroyed) the
  same handles `InternalTextAndFontData` already owns and destroys itself — DX11 "just worked"
  there only because a COM SRV can have two independent reference-counted owners; DX12
  descriptors can't. Fixed generally, not with a one-off workaround: `MigrationView` gained
  real non-owning-alias support (`owns` flag + `MakeAlias()`), and
  `TextureResource::SetRhiTexture` gained an `aTakesOwnership` parameter defaulting to the
  existing (owning) behavior everywhere else.
  **Verified**: DX11 regression via `BENCH_SCREENSHOT`, pixel-identical. DX12: `GameMain_Debug.exe`
  now runs `GraphicsEngine::Init()` to completion — reaching its own "All done, starting..."
  banner for the first time ever in this port — and enters the real per-frame game loop (scene/
  light/camera loading, GI probe setup, Sponza model load).
- [x] Step 9 — `CubemapPrefilter.cpp`'s two real, exercised entry points (`CaptureSceneToCubemap`,
  `GeneratePrefilteredCubemap`; the `LoadBaseFrom*`/`Export*` methods are confirmed-dead code with
  zero callers, deliberately left untouched) branch DX11/DX12, DX11 code paths unmodified.
  `CubemapData` gained an owning `MigrationView<TextureHandle>` and its `TextureResource resource`
  is populated as a **non-owning alias** (`SetRhiTexture(..., aTakesOwnership=false)`) — the same
  one-owner/many-non-owning-views pattern as `TextService`'s font atlas. `Dx12CommandContext::
  CopyTextureRegion` and a generalized array/cube-aware `GenerateMips` (per-slice RTV/SRV, explicit
  per-subresource barrier bracketing) were implemented to support it — previously unimplemented
  since nothing had called them yet. Along the way, fixed a real, previously-latent bug in
  `Dx12Device::CreateSrv`: it always took the `TEXTURECUBE` view-dimension branch for any TexCube
  resource, with no way to view a single face as a plain 2D-array slice (needed by `GenerateMips`'s
  per-face downsample loop).
  **Root-cause crash fix (the actual wall, not `CubemapPrefilter` itself)**: DX12 could not get
  past the very first frame's depth-buffer clear — a silent, deterministic access violation inside
  `ID3D12CommandList::ClearDepthStencilView`. Diagnosed via a new `Dx12Device::DrainDebugMessages`
  helper (drains the D3D12 debug layer's `ID3D12InfoQueue`, otherwise invisible without an attached
  debugger) which surfaced the real message once the debug layer was temporarily re-enabled:
  *"Descriptor ... which has an underlying stale or released resource is invalid for use"* — a
  genuine use-after-free, not anything specific to depth clears. Root cause: `TextureResource` and
  `RenderTarget` each declare a user destructor with no matching move constructor/assignment;
  under the Rule of Five that **silently suppresses the compiler-generated move operations** (even
  though `MigrationView` itself is properly move-aware, by design, specifically so these wrapper
  classes could rely on compiler-generated special members). Every `thing = Thing::Create(...)`
  factory-by-value assignment across the whole engine (`DepthBuffer::Create`, `RenderTarget::
  Create`, etc.) was therefore silently falling back to **copy** assignment. `MigrationView`'s
  copy ctor/assignment deliberately does not propagate ownership (each instance owns only what it
  creates) — so under a copy, the temporary returned by `Create()` kept ownership and destroyed
  the real DX12 resource/view the moment it went out of scope at the end of the assignment
  statement, leaving the just-assigned, persistent object (e.g. `DX11::myDepthBuffer`) holding a
  dangling handle from the very first frame onward. This is an **engine-wide latent bug**, not
  a `CubemapPrefilter`-specific one — it would have hit the very first `RenderTarget`/`DepthBuffer`
  ever assigned by value under DX12, which is exactly why nothing DX12 got further than the first
  frame before now. Fixed by adding explicit `= default` copy/move ctor+assignment to both
  `TextureResource` and `RenderTarget`, restoring the real move semantics `MigrationView.h`'s own
  design comment already assumed were in effect.
  **Verified**: DX11 regression via `BENCH_SCREENSHOT` on the real Sponza bench scene (`TEST`),
  pixel-identical, `CubemapPrefilter` DX11 path exercised (reflection probe + 360-probe GI prime).
  DX12: `GameMain_Debug.exe` now runs an **entire frame to completion and exits cleanly** (was: a
  silent crash on frame 1's depth clear, before *any* application code ran). On the real bench
  scene, DX12's `CaptureSceneToCubemap` now genuinely captures the scene into a cubemap texture and
  generates its mip chain (`CubemapPrefilter: Captured scene to cubemap (256x256, 9 mips)` — no
  crash, matching the DX11 log line). **Furthest any DX12 run has reached.**
  **Still to do**: `GeneratePrefilteredCubemap`'s DX12 branch correctly detects (rather than
  crashes on) that DX12 compute-shader loading for `PrefilterSpecularCS`/`PrefilterDiffuseCS`
  isn't wired up yet, logs an error, and bails out cleanly — DX12 compute-shader loading in
  general is still an open gap. `video.cpp`/`TextService.cpp`'s remaining `GetNativeSrv` uses
  share a related but smaller gap.
- [x] `DeferredRenderer::GiProjectProbe` DX12 crash (2026-09-11) — the per-GI-probe SH-projection
  compute dispatch (`GameWorld.cpp`'s `CaptureGiProbesImpl`, called right after each GI probe's
  own smaller `CaptureSceneToCubemap` succeeds) was the very first real DX12 compute dispatch +
  the very first compute→graphics transition this whole port had ever exercised, and turned up
  **three** distinct, real bugs plus one resource-budget wall, none actually specific to
  `GiProjectProbe` itself:
  1. **UAV-usage buffers/textures left in `COMMON` instead of `UNORDERED_ACCESS`.**
     `Dx12Device::CreateBuffer`/`CreateTexture` only set a real initial resource state for
     render-target/depth-stencil/copy-destination cases; a GPU-only UAV resource with no initial
     data (`myGiShBuffer`, the cluster-culling buffers, any future compute-output texture) was
     left in `COMMON`. D3D12's implicit state-promotion rule only promotes a resource from
     `COMMON` into *read* states on first use — **never** into `UNORDERED_ACCESS` — so writing
     through such a UAV is undefined behavior. Fixed by creating these resources directly in
     `UNORDERED_ACCESS` state when there's no initial data to upload first (the debug layer later
     confirmed this is a no-op for *buffers* specifically — "Buffers are effectively created in
     state COMMON", i.e. buffers already auto-promote fine regardless — but the identical fix for
     *textures* is real and necessary, since that rule doesn't apply to textures).
  2. **Stale graphics-PSO cache across a compute dispatch.** A D3D12 command list has exactly one
     active pipeline-state slot regardless of type — binding a compute PSO silently replaces
     whatever graphics PSO was bound. `Dx12CommandContext::SetGraphicsPipeline`'s "already bound,
     skip the redundant call" cache didn't know this, so the next `Draw` reusing the same
     `GraphicsPipelineHandle` as before a `Dispatch` would wrongly skip re-binding it, leaving the
     compute PSO active for a `Draw` call. Fixed by invalidating that cache whenever
     `SetComputePipeline` actually rebinds.
  3. **Unresolved, contained**: the *first* `OMSetRenderTargets` call after *any* compute
     `Dispatch` reliably access-violates deep in the D3D12 runtime/driver on this hardware — every
     subsequent identical call with the same handles then works completely normally. Confirmed via
     bisection this is not a resource-lifetime bug (handles/heap slots/resources all valid before
     and after) and not debug-layer-specific (identical with the layer off; the layer logs nothing
     around the fault). SEH-isolated in `Dx12CommandContext::SetRenderTargets` as a documented,
     honest containment (one dropped frame, not a real fix) rather than leaving it fatal — see the
     code comment there for what's already been ruled out.
  4. **Sampler descriptor-scratch-heap exhaustion.** A GI-probe-priming frame packs many more
     draws into one `BeginFrame`/`EndFrame` than a normal frame (`GameWorld::CaptureGiProbesImpl`'s
     batch loop) and exhausted the 800-slot-per-frame shader-visible sampler heap mid-recording.
     Raised to 2048 — the actual D3D12 hardware ceiling for a single shader-visible sampler heap,
     so this is the most headroom available short of a real per-frame-budget redesign.
  **Verified**: DX11 regression via `BENCH_SCREENSHOT`, pixel-identical (unaffected — all four
  fixes are DX12-only). DX12: the crash is gone — `GiProjectProbe` and the whole GI-probe-priming
  batch now run to completion inside one frame without crashing (previously: guaranteed crash on
  the very first probe). Execution now reaches the main scene draw (G-buffer + deferred lighting)
  for the first time ever, where it currently **hangs** (not a crash) — a new, separate,
  not-yet-debugged issue in that as-yet-unexercised code path, out of this fix's scope.
  Checkpoint (unchanged): `-rhi=dx12` visually identical to `-rhi=dx11` on every scene +
  editor + Tutorials, PIX-clean, perf parity or better.
- **DX12 crash-after-a-few-frames + a real fix for the post-GI hang** (2026-09-11, later same
  day) — the hang above turned out to be masking (and be masked by) two more real bugs:
  1. **The D3D12 debug layer itself was fatal.** `ID3D12InfoQueue`/`IDXGIInfoQueue` default to
     `DebugBreak()` on CORRUPTION/ERROR-severity messages; with no debugger attached that's an
     unhandled exception silently killing the process a few frames later (bare exit code `0x87D`,
     no WER event, no visible error at all). Disabling `SetBreakOnSeverity` for every severity on
     both info queues did not stop it. Left the debug layer off entirely for DX12 (DX11's own
     separate debug layer is unaffected) — documented in `DX11::InitDx12`.
  2. **The `SetRenderTargets` SEH containment was dropping real work, not just a crash.** Turned
     it from "catch and drop the frame" into "catch and retry" (the same call always succeeds on
     a second attempt) — when the fault landed on `BeginGeometryPass`'s own G-buffer bind (the
     single most frequent `SetRenderTargets` call in the engine), the dropped frame meant that
     frame's model draws went nowhere, visibly showing only the skybox/ambient with no geometry.
  **Verified live** (not just via automated bench): the real Sponza scene renders correctly under
  DX12 for the first time, matching the DX11 reference, and the process no longer crashes or hangs
  in the first several frames from either of these two causes.
- **The actual root cause of the "hangs after a couple of frames" symptom, part 1: found and
  fixed** (2026-09-11, same day, continued) — added a per-`EndFrame` `GetDeviceRemovedReason()`
  check (kept permanently: without it, a dead D3D12 device is invisible until something else
  happens to call an HRESULT-returning device method) and discovered the device was being marked
  `DXGI_ERROR_DEVICE_HUNG` (not a crash Windows reports) as early as the frame-2/3 boundary, in
  *every* configuration tried — not something the earlier fixes above actually touched. Re-enabled
  the D3D12 debug layer **with GPU-based validation** just long enough to get one real diagnostic
  (accepting its "several minutes per frame" slowdown) and it found a genuine bug: *"GPU-BASED
  VALIDATION: Draw, SRV resource dimensions differs from that expected by shader: SRV Dimension
  Expected: D3D12_SRV_DIMENSION_TEXTURECUBE, SRV Dimension In Descriptor: D3D12_SRV_DIMENSION_
  TEXTURE2D"*. Root cause: `TextureManager (DX12)`'s DDS loader correctly rejects cubemap files
  (cubemap loading isn't implemented for DX12 yet — a known, separately-tracked gap) but the
  generic "load failed" fallback path in `TextureManager::GetTexture` always built a flat 2D
  checkerboard texture regardless of what shape the caller actually needed. `GraphicsStateStack`'s
  default/horizon ambient cubemaps (`whiteCubeMap.dds`/`horizonCubeMap.dds`, both DX12-unloadable)
  hit exactly this path, handing a `TextureCube`-typed shader slot a `Texture2D` descriptor —
  undefined behavior at the hardware level, not merely a cosmetic wrong-texture bug. Fixed by
  probing the failed asset's own DDS header (cheap, metadata-only) and building a proper 6-face
  `TextureCube` fallback (`CreateSolidCubeTexture`, mirroring the existing `CreateSolidTexture`
  2D helper) when it was actually a cubemap. **Verified via GPU-based validation**: the SRV-
  dimension-mismatch message is completely gone after this fix (was present on every single frame
  before it). Also added `IDevice::CaptureBackBufferPng` (DX12-only; DX11 already has its own
  working screenshot path) as a permanent diagnostic tool, since `BENCH_SCREENSHOT` was previously
  silently non-functional under DX12 (it reaches into `DX11::SwapChain`/`DX11::Context` directly,
  both null there) — a self-contained BMP writer, no DirectXTex dependency needed.
  **Not fixed, still open**: fixing the SRV-dimension bug did **not** stop the `DEVICE_HUNG`.
  Exhaustive bisection with the fix in place — `BENCH_CLUSTERED/SSAO/SHADOWS/SSR/POSTFX/
  LOCAL_SHADOWS/DEFERRED/PROBE/GI=0` individually and **all nine disabled simultaneously**
  (leaving only the bare G-buffer geometry pass + lighting resolve + composite, the absolute
  minimum needed to see anything at all) — still hits `DEVICE_HUNG` at the same frame-2/3
  boundary, every time, regardless of which scene or camera mode. Re-running with the debug layer
  + GPU-based validation active a second time (after the cubemap fix) produced **zero** further
  validation messages of any kind before the hang — meaning this is a class of GPU-side fault
  (most likely a genuine shader hang/infinite loop, or something GPU-based validation's resource-
  access checking doesn't cover) that CPU-side bisection and the validation layer's current
  checks can't localize any further. `0x887A0006` (`DXGI_ERROR_DEVICE_HUNG`, not `DEVICE_REMOVED`
  as earlier logs from before this check existed had assumed) means Windows' TDR watchdog killed
  the device after ~2s of the GPU appearing stuck — consistent with the fixed "frame 2" timing
  regardless of workload (it's plausibly frame 0 or 1's GPU work that never finishes, only
  *noticed* when frame 2's fence-wait blocks on it). **Next step needs GPU-side tooling this
  environment doesn't have** — a PIX or NSight Graphics capture of the exact hanging frame would
  show which draw/dispatch never retires, which no amount of further CPU-side toggling will
  reveal.
- **`DEVICE_HUNG`: actual root cause found and fixed** (2026-09-11, same day, continued further) —
  a PIX capture (walked through interactively: Developer Mode + admin launch for timing data,
  `TGE_RHI=dx12 BENCH_FRAMES=10 BENCH_WARMUP=0`, GPU crash dump capture on) showed
  `DeviceRemovalDetected(DXGI_ERROR_DEVICE_HUNG)` as the literal last event, preceded only by
  ordinary `SpriteDrawer` instanced-quad draws — nothing exotic. Found and fixed in passing:
  ImGui's vendored `imgui_impl_dx12.cpp` unconditionally created its **own** independent
  `ID3D12CommandQueue` for the one-off font-texture upload instead of reusing the app's queue (two
  "Graphics Queue" rows visible in PIX); switched `ImGuiInterface.cpp` to the struct-based
  `ImGui_ImplDX12_InitInfo::Init()` so the real queue can be passed through. Real progress came
  from two of the user's own fixes: (1) `TextureRec`/`BufferRec::state` was left at its
  creation-time value (typically `COPY_DEST`) after `CreateBuffer`/`CreateTexture` uploaded initial
  data and internally transitioned the resource further — later barriers used this stale state as
  `StateBefore`, which D3D12 does not repair; fixed by computing the real post-upload state up
  front. (2) D3D12 views carry no reference to (or transition on behalf of) their owning resource,
  unlike DX11 — `SrvRec`/`UavRec`/`RtvRec`/`DsvRec` gained `texture`/`buffer` owner fields so
  `SetRenderTargets`/`ClearRenderTarget`/`ClearDepthStencil`/`SetShaderResource`/
  `SetUnorderedAccess` can all now emit the resource-state transition D3D12 requires at bind time
  (previously entirely absent engine-wide). Neither fix alone stopped the hang, but they made the
  next step possible: `Dx12Device::DrainDebugMessages` (an `ID3D12InfoQueue` message pump, written
  earlier but never actually wired up) was hooked into `EndFrame`, and the D3D12 debug layer was
  re-enabled (`TGE_DX12_DEBUG_LAYER=1`; the layer's break-on-severity is disabled in
  `CreateDeviceAndQueue`, so it now only logs instead of raising an uncatchable `DebugBreak()` —
  the earlier "enabling it crashes outright" finding was this, not a fundamental incompatibility).
  This surfaced two real, previously invisible bugs:
  1. **`Dx12CommandContext::GenerateMips`** manages a cubemap's mip chain with its own
     per-subresource state array (`subState`, local to the call) specifically because the mip
     chain needs different subresources in different states at once (destination = RENDER_TARGET,
     source = PIXEL_SHADER_RESOURCE) — something the single whole-resource `TextureRec::state`
     can't represent. It assumed `SetRenderTargets`/`SetShaderResource` were state-tracking-inert,
     true when it was written but no longer true once fix (2) above added owner-based
     auto-transitions to those same functions: calling them from inside `GenerateMips` now *also*
     transitions the WHOLE resource via `TransitionResource`, corrupting `TextureRec::state` for
     the rest of that texture's life (confirmed via the debug layer: every subresource except each
     face's mip 0 — i.e. exactly the mips `GenerateMips` touches — reported as being in the wrong
     state for whatever it was used for next). Fixed by having `GenerateMips` bind its RTV/SRV
     directly (`SEH_OMSetRenderTargets` + raw descriptor-table slot assignment) instead of going
     through the auto-transitioning bind path, since `barrierOne` already emits fully correct
     per-subresource barriers on its own.
  2. **The actual `DEVICE_HUNG` cause**: `Dx12Device::Destroy(TextureHandle/BufferHandle)` freed
     the underlying `ComPtr<ID3D12Resource>` **immediately** (`Pool::Free` overwrites the slot with
     a fresh, empty record on the spot) with no regard for whether a command list still referenced
     it — a real GPU-side use-after-free. `CubemapPrefilter::CaptureSceneToCubemap` re-populates
     the *same* `CubemapData` many times in a row across a GI-probe bake (its own header comment:
     "thousands of times per GI bake") with no GPU flush between captures; each capture starts
     with `outCubemap.Reset()`, which destroyed the *previous* capture's texture while that
     capture's own render/copy/`GenerateMips` commands were still sitting unexecuted in the
     current frame's not-yet-submitted command list. Confirmed via the debug layer: "An
     ID3D12Resource object ... referenced in a command list ... was deleted prior to
     executing/closing the command list. This is invalid and can result in application
     instability." Fixed at the architectural level rather than at this one call site: the engine
     already has exactly the right mechanism for this (`Dx12Device::KeepAliveUntilFrameRetires`,
     used for one-off `UpdateBuffer`/`UpdateTexture` upload resources) — `Destroy(BufferHandle)`/
     `Destroy(TextureHandle)` now route the resource's `ComPtr` through it before freeing the pool
     slot, so the real GPU object stays alive until that frame-in-flight's fence has actually
     retired, regardless of what CPU-side code destroyed the handle or when.
  **Verified**: DX12 now runs the full Sponza bench for 500 frames with **zero** `DEVICE_HUNG`
  events (previously 100% reproducible by frame 2, invariant across every fix up to this point);
  re-ran with the debug layer + GPU-based validation active for 60 frames — the resource-lifetime
  and state-mismatch messages are completely gone (one unrelated, isolated, one-time swapchain
  message remains, not a hang cause). `BENCH_SCREENSHOT` confirms DX12 renders the Sponza scene
  correctly, pixel-equivalent to the DX11 reference (same geometry, same colored lights). DX11
  regression-checked via the same screenshot path: unaffected. The DX12 debug layer is now a real,
  usable diagnostic tool going forward — opt in via `TGE_DX12_DEBUG_LAYER=1`
  (`TGE_DX12_GPU_VALIDATION=1` for GPU-based validation on top; both `_DEBUG`-only, off by default
  since GPU-based validation alone roughly halves frame rate).
- **"Fix Stage 2" cleanup pass (2026-09-12) — closed every remaining known DX12 gap, including a
  real GameEditor-crashing bug found live with the user.**
  - **DX12 draw-call counter** — `Dx12CommandContext::Draw`/`DrawInstanced`/`DrawIndexed`/
    `DrawIndexedInstanced` never called `DX11::LogDrawCall()` (a plain, backend-agnostic static
    counter — the DX11 command context's 4 equivalent call sites already did). Every DX12 bench
    report and perf-overlay reading showed 0 draw calls regardless of real scene complexity. Fixed
    by adding the same call to all 4; verified DX12 now reports the same draw-call count as DX11
    for the same scene at steady state (206 = 206) — an earlier apparent 1502-vs-1310 discrepancy
    turned out to be the GI-probe-priming batch landing inside the averaging window differently
    between runs, not a real per-frame difference.
  - **DX12 cubemap/volume DDS loading** — `TextureManager::LoadTextureDx12` rejected every
    cubemap DDS outright ("not supported through this loader"), silently falling back to a flat
    solid-color cube for the engine's default/horizon ambient maps (and would reject any real
    authored skybox asset too). The rest of the loader already handled arrays generically; only
    cubemap-specific dimension bookkeeping was missing. Fixed: `TextureDimension::TexCube` (with
    `depthOrArraySize` = `metadata.arraySize / 6`, since DirectXTex's `arraySize` for a cubemap is
    already the total face count while the RHI's convention is "number of cubes") — `CreateSrv`
    already auto-selects a `TEXTURECUBE` view for that dimension. Volume (3D) textures remain
    unsupported (genuinely out of scope, matches `GenerateMips`'s stance). Verified: the
    `whiteCubeMap.dds`/`horizonCubeMap.dds` rejection messages are gone, debug layer clean,
    DX11-vs-DX12 screenshot comparison visually consistent.
  - **`ClearUnorderedAccessFloat` implemented** — a milestone-2-era `assert(false)` stub whose
    documented blocker ("`UavHandle` doesn't remember which texture/buffer owns that view") had
    already been closed by the `DEVICE_HUNG` fix pass's `UavRec` owner fields, leaving nothing
    to actually defer. Implemented properly: copies the UAV's permanent (non-shader-visible)
    descriptor into a fresh slot of the current frame's own shader-visible CBV/SRV/UAV scratch
    heap (already bound every frame), then calls `ClearUnorderedAccessViewFloat` with that GPU
    handle + the permanent CPU handle, per the API's unusual dual-handle requirement. Feeds
    `DeferredRenderer::ClearGi()`, which the automated bench never exercises (GI priming starts
    active by default; `ClearGi()` only runs on an explicit re-prime, e.g. the editor's "Re-prime
    GI" button, or a lighting-parameter change) — implemented and code-reviewed against the
    already-proven `FlushGraphicsTables`/`FlushComputeTables` pattern in the same file, but not
    exercised live this session.
  - **Three real GameEditor-under-DX12 crashes found and fixed live with the user** (interactive
    testing — the automated GameMain bench never touches editor-only code, so none of these were
    visible before now). All three are the exact same bug class as the `DEVICE_HUNG` root cause's
    sibling fixes: editor-only code calling straight into a DX11-only raw pointer/ComPtr that
    `DX11::ForceLoad*Shader`/`TextureResource`/etc. never populate on DX12, missed by every earlier
    migration pass since none of it is exercised by `GameMain`'s bench flow:
    1. `SceneUtil.cpp::DrawOutlines` bound the viewport's ID/selection-outline render target via
       `TextureResource::SetAsResourceOnSlot` (a raw `PSSetShaderResources` off a DX11-only
       ComPtr) — `assert(mySRV.Get())` fired the instant a scene's viewport needed that pass (i.e.
       as soon as a scene was open). Fixed: routed through `ctx.SetShaderResource`, matching every
       other bind in the file.
    2. `Viewport.cpp`'s main 3D viewport display (`ImGui::Image(...)`) cast
       `TextureResource::GetShaderResourceView()` (null on DX12) straight to an `ImTextureID` —
       worse than a null-texture no-op, since DX12's `ImTextureID` convention is a
       `D3D12_GPU_DESCRIPTOR_HANDLE` value, not a view pointer at all, so this fed ImGui's DX12
       backend a bogus GPU address for the main viewport image every frame. Same underlying issue
       in `DefaultEditorGraphics::GetTextureID` (asset-browser thumbnails / material previews).
       Both fixed via `IDevice::ImGuiTextureId(SrvHandle)` — a bridge that existed since milestone
       3's ImGui port but was itself still broken (its own comment: "this predates the milestone-2
       dual-heap redesign... very likely broken now", returning a GPU handle from the PERMANENT
       non-shader-visible heap, which has no valid GPU handle at all). Implemented properly: a
       per-`SrvHandle` slot cache in `myImGuiSrvHeap` (the small persistent shader-visible heap
       already reserved for ImGui's font atlas, with headroom the class comment already
       anticipated for "any future ImGui::Image user textures") — allocates a slot once per
       handle, re-copies the descriptor every call (cheap; keeps a resized texture's new
       underlying resource in sync through the same cached slot).
    3. **The actual root cause of GameEditor "vanishing" with zero error, zero crash dialog, and
       zero Windows crash dump under DX12** (confirmed genuinely reproducible: opening any scene
       and moving the mouse over the viewport crashed it every single time) — `Viewport.cpp`'s
       `MouseOver()` (mouse-picking readback) called
       `aTarget.GetShaderResourceView()->GetResource(&src)`, an unconditional null-pointer
       dereference on DX12 (this function was previously, deliberately left raw during the port —
       "no RHI CPU-readback texture support yet" — a real, accepted gap, but one that crashed
       instead of gracefully doing nothing). Diagnosed with two new pieces of PERMANENT
       infrastructure added specifically because this crash produced literally no diagnostic
       anywhere (no stdout, no dump in `%LOCALAPPDATA%\CrashDumps`, no Windows Error Reporting
       event — even though other crashes this session, e.g. `GameMain_Debug.exe`'s during the
       `DEVICE_HUNG` investigation, did produce dumps): `GoEditor.cpp` now installs a top-level
       `SetUnhandledExceptionFilter` that resolves the faulting address to a symbol + source
       file/line via DbgHelp against the Debug build's own PDB (no external debugger needed) and
       logs it before letting the default handler continue — immediately pinpointed
       `MouseOver+0xDC` at `Viewport.cpp:373`. Fixed by adding a real, general
       `IDevice::ReadBackUintPixel4(TextureHandle, x, y, uint32_t out[4])` to the RHI itself
       (`Device.h`), moving `MouseOver`'s existing (working, unchanged) DX11 logic into
       `Dx11Device::ReadBackUintPixel4`, and implementing a DX12 version mirroring
       `CaptureBackBufferPng`'s established one-off-command-list + `WaitForGpuIdle` synchronous
       readback pattern (creates a 1-texel `D3D12_HEAP_TYPE_READBACK` buffer, `CopyTextureRegion`
       with a 1×1×1 box, maps, reads, unmaps) — `Viewport.cpp` no longer touches D3D11 at all for
       this. **Verified live, interactively, by the user**: opened a scene and moved the mouse over
       the viewport under DX12 repeatedly — no crash, matching the exact sequence that was
       previously 100% reproducible across five straight attempts.
  DX11 regression-checked throughout (process-liveness plus a full `BENCH_SCREENSHOT` Sponza run);
  `Game.sln` + `GameEditor.sln` both build 0 errors. **The only remaining documented raw-D3D11
  exception in editor code is `Viewport.cpp`'s per-mouse-move object-picking region-readback path
  itself is now fixed — the one still-deliberate gap is unrelated: nothing left uses
  `GetShaderResourceView()`/`SetAsResourceOnSlot`/a raw `ImTextureID` cast anywhere in
  `Source/Editor`+`Source/EditorDefaultGraphics` any more** (confirmed via a full grep sweep of
  both trees). DX12 GameEditor is, for the first time this port, genuinely usable end-to-end:
  device/swapchain/UI bring-up, real texture/cubemap loading, opening a scene, viewing it in the
  3D viewport, and mouse-hovering/picking objects in it all work without crashing.
- **Backend chooser launcher (2026-09-12)** — with `TGE_RHI=dx12` no longer needed just to keep
  the engine from immediately hanging/crashing, added a small native Win32 window (no ImGui/D3D
  device exists yet at this point, so it can't be one) with two buttons, "Legacy" and "DX12",
  shown before either `GameMain` or `GameEditor` boots (`Source/Application/tge/windows/
  BackendChooser.{h,cpp}`) — picking one just sets the `TGE_RHI` env var the existing
  `DX11::Init()` check already reads, so it's a friendlier front end for that switch, not a new
  selection mechanism. Skipped entirely (no popup) whenever `TGE_RHI` is already set, so every
  scripted/bench/CI invocation from this whole port is unaffected. Also added a permanent,
  shared top-level crash handler (`Source/Application/tge/windows/CrashHandler.{h,cpp}`,
  factored out of the GameEditor-specific one from the `DEVICE_HUNG`/editor-crash work) to both
  `Go.cpp` and `GoEditor.cpp`.
  **Found and fixed a real bug via live testing with the user**: the chooser's `WM_DESTROY`
  handler called `PostQuitMessage(0)` after a normal button click (to stop its own local message
  loop) — but `PostQuitMessage` posts `WM_QUIT` to the whole THREAD's message queue, not "close
  this one dialog", so that stale `WM_QUIT` sat queued until the main application's own game-loop
  message pump started moments later and immediately consumed it, exiting the whole app right
  after a successful backend pick — with everything else (device, scene, GI) having initialized
  completely correctly, no error anywhere, making it look exactly like a random silent crash
  (and with no crash-handler output, since it genuinely wasn't a crash). Fixed: the chooser's own
  local loop only needs its `gDone` flag to stop; removed the errant `PostQuitMessage` entirely.
  **Verified live, interactively, by the user**: picked DX12 in the chooser, the Sponza scene
  loaded and ran continuously in free-fly mode as expected, no unexpected exit.
- **Stage 3** — DXR inline `RayQuery` (SM 6.5) hardware-traced GI, replacing the Phase 6
  SH-volume software path with a real DDGI (BLAS/TLAS, per-probe ray tracing into the
  existing SH probe volume). Not started.
  **Two prerequisites the plan always assumed, confirmed NOT actually done yet (checked
  2026-09-12)**: (1) every shader, DX12 included, still compiles via legacy `D3DCompileFromFile`
  at `*_5_0` (DXBC, Shader Model 5.0) — fine for ordinary PSOs, but inline `RayQuery` needs SM 6.5/
  DXIL, which only DXC produces; the DXC swap from Stage 2's own outline never actually happened.
  (2) `Dx12Device` creates a plain `ID3D12Device`, not `ID3D12Device5` — acceleration-structure
  build/update needs that plus `ID3D12GraphicsCommandList4`, and a `D3D12_FEATURE_D3D12_OPTIONS5`
  `RAYTRACING_TIER_1_1` check (the RTX 5060 Ti supports this comfortably — Turing onward all do).
  Both are small, mechanical, do-first items — not blockers, just not yet true despite being
  assumed complete by this section's own outline below.
- **Stage 4** — RT reflections (replaces Phase 5 SSR) / RT shadows (replaces Phase 3.5
  shadow maps) / RT AO (replaces Phase 3.2 SSAO), each behind its own toggle against the
  existing screen-space technique. Not started.

---

## Cross-cutting track (parallel, not sequential)

- [x] **Render-graph / pass-list abstraction** — `RenderGraph` (thin linear pass list, auto GPU marker +
  `GpuProfiler` scope per pass), `RenderResourcePool` (frame-transient `RenderTarget` pool, keyed by
  w/h/format, `ReleaseAll()` after Execute), `GpuMarker`. All in `Source/Graphics/tge/render/`. Engine owns
  the pool (`GraphicsEngine::GetRenderResourcePool()`); `DeferredRenderer::BuildFrame(graph, drawOpaque, dbg)`
  registers geometry/lighting/composite (or gbufDebug). No auto-reordering/aliasing yet — passes run in add
  order. Next passes (SSAO, bloom, shadows, SSR) `AddPass` + `pool.Acquire` instead of hard-wiring.
- [ ] GPU instancing for repeated props (`DrawIndexedInstanced`, per-instance transform stream) — lower
  priority now that Sponza is 28 draws
- [ ] AO bake tool (mesh-space / bent-normal) — offline high-quality path; SSAO (Phase 3.2) is the realtime one
- [ ] Normal-map TBN cleanup — the model PS negates the bitangent, so the cooker green-flips by default
  (`--src-normals`). Fragile convention coupling; fix properly and drop the flip.
- [ ] Cooker `--audit` mode — no-cook report: FBX material count vs cooked DDS vs `cook.json` aliases,
  list unmatched. Would have made every New Sponza re-export (`.003` suffixes, silent texture drops) a one-liner.
- [ ] Shader system cleanup: runtime-HLSL → cooked, shared constant buffers, reflection, deduplicated input layouts
- [ ] FBX-SDK import robustness (`ModelFactory`): the glTF→FBX New Sponza exposes gaps our path mishandles (position.w, some sub-meshes). Harden or move to a better importer.
- [x] **Cooker BC backend = NVIDIA Texture Tools** — shells out to `nvcompress.exe` (auto-detected at the
  default install dir / `NVTT_DIR` / PATH / `--nvtt`), DirectXTex fallback. `--nvtt-quality fast|production|highest`,
  `--no-nvtt`. **This fixed the DamagedHelmet**: DirectXTex `Compress` to `BC7_UNORM_SRGB` was emitting an
  all-zero texture for the `inputs`/JPEG-sourced path — packed data was verified correct, the compressor was the bug.
  Serialised via mutex (CUDA). ~8 s for the helmet's 4 maps.
- [x] **Bench loads `.tgo` / `.tgs`** — `BENCH_MODEL=x.tgo` (uses the object def's explicit per-mesh textures);
  `BENCH_SCENE=x` loads `x.tgs` + `x.leveldata/` (GUID-named object files). Each object resolves a model via
  (a) inline Model property, (b) `"path"` to a `.tgo`, or (c) **`"object-definition": "<name>"`** → first
  `<name>.tgo` found under the game data root (the real editor format). `.tgo` "Model" property `"name"` is the
  descriptive object-def name, not the literal "Model". `TEST.tgs` = Spaceship + seated Sponza.
- [x] **Cooker: Unreal standard-material suffixes** — `_BaseColor`/`_Normal`/`_Emissive` plus
  `_OcclusionRoughnessMetallic` / `_ORM` / `_ARM` → mapped straight to the `_M` output (already R:AO G:Rough
  B:Metal). `--tgo-name <name>` sets the object-def name, `--tgo-pad N` pads the textures array to N rows.
  Spaceship cooked: `Spaceship_Material.001_{C,N,M,FX}.dds` (NVTT BC7/BC5), `--src-normals dx` (Unreal exports
  DirectX-convention normals). C map verified correct (white body + 3 marker dots), no black-texture bug.
- [ ] TODO: DDS must sit next to the `.fbx` for raw `BENCH_MODEL=*.fbx` auto-resolution (helmet's are in `source/` now); `.tgo`/scene paths are absolute-from-game-root so unaffected.
- [ ] Decal pass — `dirt_decal` and friends need blended projected decals; flat constant until then (alpha-test in the opaque G-buffer pass gives ugly black borders).
- [ ] `TextureCooker` → `AssetCooker`: glTF import (cgltf) so the Khronos / Intel test-asset ecosystem is usable *(parked — assets being FBX-converted by hand for now; conversion quality is the current blocker)*
- [ ] LOD story for large scenes

## Tools deliverables (from the original ask)

- [x] TextureCooker (TGA-standard DDS packing, manifest, threading) — `p5g3-texturecooker`
- [ ] GPU profiler / frame inspector (Phase 2.5) — + GPU debug markers for PIX/RenderDoc
- [ ] Editor G-buffer + lighting debug views (Phase 2.5)
- [x] **Material Editor (GameEditor tab)** — `File ▸ New material…` / double-click a `.tgmat`. Dockable
  document: 3D PBR preview viewport (`EditorViewport`, Alt/MMB orbit) of a switchable primitive
  (Sphere/Cube/Cylinder/Cone/Torus/Plane), Unreal-style properties panel (base colour, metallic, roughness,
  AO, normal strength, emissive colour+strength, 4 texture-map slots w/ drag-drop `.dds` + AssetBrowser
  "Set"), and a Preview-Lighting panel (key-light dir/colour/intensity, ambient, cubemap). `.tgmat` = flat
  JSON (`MaterialAsset`), read back at runtime by the game (`GameWorld::LoadTgmat`) for the built-in room
  surfaces and the debug sphere. New forward shader `PbrConstModelShaderPS.hlsl` (b11 `ConstMaterial`
  cbuffer) drives the no-textures preview; textured `.tgmat` fall back to the stock PBR shader.
- [x] **Primitive pack** — engine built-ins grew from Cube/Plane to
  **Cube / Plane / Sphere / Cylinder / Cone / Torus** (procedural in `ModelFactory::InitPrimitives`,
  `GetModelInstance("Sphere")` …). Draggable `.fbx` + `.tgo` copies with a checker map in
  `Source/Game/data/Primitives/` (`PrimCube` … — stem-prefixed to dodge the editor's unique-`.tgo` rule).
- [x] **Built-in procedural room** — `Scene ▸ <BuiltinRoom>` in the tune panel (and `BENCH_SCENE=<BuiltinRoom>`):
  6 primitive-plane surfaces, per-surface fixed-param material via `GBufferDebugMatPS`, 4 neutral ceiling
  lamps, no-face-cull. Size + per-surface PBR + Load-.tgmat in the *Built-in room* panel.
- [ ] TextureCooker `--audit` mode (Cross-cutting) — FBX vs cooked coverage report, no cook
- [ ] Standalone: AO baker, (later) asset cooker CLI
