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

## Phase 4.5 — TAA + motion vectors  `[ ]`

- [ ] Per-object motion-vector G-buffer channel (needs prev-frame transforms)
- [ ] Jittered projection + temporal resolve with neighbourhood clamp
- [ ] History buffer management, disocclusion handling
- Prereq for reflection / GI denoising; also unlocks motion blur later

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
- **Stage 2** `[~]` — DX12 backend behind the same seam (D3D12MA, `d3dx12.h`/Agility SDK, DXC,
  one root signature mirroring the existing register layout, automatic barrier tracker).
  **Starting now.** Goal: implement `Source/Application/tge/rhi/dx12/` against the exact same
  `IDevice`/`ICommandContext` interface Stage 1 built — every converted call site should work
  unmodified, selected via `-rhi=dx12` at startup. See memory `p5g3-dx12-port` for the detailed
  plan (descriptor heaps, upload ring, PSO cache, root signature, barrier tracker, DXC compile
  path, `imgui_impl_dx12`, DirectXTex `*12` loaders). Checkpoint: `-rhi=dx12` visually identical
  to `-rhi=dx11` on every scene + editor + Tutorials, PIX-clean, perf parity or better.
- **Stage 3** — DXR inline `RayQuery` (SM 6.5) hardware-traced GI, replacing the Phase 6
  SH-volume software path with a real DDGI (BLAS/TLAS, per-probe ray tracing into the
  existing SH probe volume). Not started.
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
