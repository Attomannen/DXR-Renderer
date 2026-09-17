# Sponza benchmark harness

`GameMain` (the `Game.sln` runtime) is scene-driven: it loads a `.tgs` scene and
the `.tgo` object definitions it references. If `BENCH_SCENE` is not set, it
uses the first `.tgs` found under the game asset root, so no particular FBX is
required. It then sets up lights + a camera and, when asked, runs a deterministic
fly-through and writes a JSON report.

## Running

```
cd Bin
BENCH_FRAMES=1200 BENCH_WARMUP=200 ./GameMain_Release.exe
```

Convenience scripts (`Bin/`):

| script | what |
|--------|------|
| `frame.bat <model> [out.png] [forward]` | **curated per-model framing** → screenshot + `<model>_bench.json`. `model` = `sponza` / `newsponza` / `helmet`. Tuned camera + exposure baked in per model. |
| `view.bat <model>` | free-flight; **F5** saves `bench_camera_<model>.json` (overrides the curated framing) |
| `tune.bat [scene]` | free-fly `<scene>.tgs` (default `TEST`) with the live **Render tuning** ImGui panel — **Scene dropdown** (switches any `*.tgs` at runtime), sun pitch/yaw/colour/intensity, ambient, shadows (+normal-offset/bias/strength/show-cascades), SSAO (+radius/bias/intensity/power), **Post FX** (bloom threshold/knee/intensity, auto/manual exposure + key/min/max/adapt-speed, EV comp), clustered-cull & deferred toggles, G-buffer view combo. `` ` `` toggles the panel. Non-retail builds only. |
| `shot.bat <model> [out.png] [spin\|fixed\|room\|orbit\|forward]` | screenshot from the saved viewpoint with a chosen motion mode |

Env vars (all optional):

| var | default | meaning |
|-----|---------|---------|
| `BENCH_FRAMES` | `0` | `0` = interactive free-fly (WASD + RMB-look, E/Q up/down, Shift = fast; **F5** save viewpoint → `bench_camera.json`, **F9** reload, **F6** print). `>0` = run N frames, write the report, quit. Also forces vsync off. |
| `BENCH_CAM` | `spin` | when a `bench_camera.json` exists: `spin` holds the saved position and sweeps yaw in place (still shot matches the save), `fixed` = dead still, `orbit` circles the saved point, `room` orbits the middle of the scene. No saved camera → auto room orbit. |
| `BENCH_SPIN` | `35` | half-sweep (deg) for `spin` mode |
| `BENCH_EXPOSURE` | `1` | scales point-light output |
| `BENCH_ORBIT` | auto | camera orbit radius override |
| `BENCH_ROT_X` | `0` | rotate the model about X (deg) — e.g. `-90` to bring Z-up content to Y-up |
| `BENCH_WARMUP` | `60` | leading frames excluded from stats |
| `BENCH_SPONZA_COPIES` | `1` | grid of Sponza copies — raises sub-mesh / draw-call load |
| Local lights | selected `.tgs` only | Author `{ "lighting": { "lights": [ { "pos":[x,y,z], "color":[r,g,b] (0..1), "intensity":N, "range":<world>, "radius":N } ] } }` in the scene itself. Final RGB = `color * intensity * BENCH_EXPOSURE`. An absent/empty array means no local lights; the runtime never loads `bench_lights_*.json` or creates a fallback rig. |
| `BENCH_DEFERRED` | `1` | `1` = deferred G-buffer path, `0` = forward |
| `BENCH_CLUSTERED` | `1` | `1` = froxel-clustered light culling (compute), `0` = brute-force loop over all lights per pixel (A/B). Deferred path only. |
| `BENCH_SSAO` | `1` | `1` = screen-space AO pass (folded into ambient), `0` = off. Deferred path only. `BENCH_GBUF=8` shows the AO buffer. |
| `BENCH_SHADOWS` | `1` | `1` = directional cascaded shadow maps (4 cascades, PCF 5×5, cascade-blended), `0` = off. Deferred path only. |
| `BENCH_CONTACT` | `1` | `1` = screen-space contact shadows on the sun (short depth ray-march in the lighting pass, fills what the cascades miss), `0` = off. Needs `BENCH_SHADOWS=1`. ~0.1 ms. |
| `BENCH_LOCAL_SHADOWS` | `1` | `1` = point/spot light shadows (shared 4096² atlas, nearest ≤8 casters re-rendered each frame). `0` = off. Deferred path only. `BENCH_GBUF=9` shows the atlas. |
| `BENCH_AMBIENT` | `1` | scales the ambient / IBL term (useful for isolating a light's contribution). |
| `BENCH_SSR` | `1` | `1` = screen-space reflections (view-space ray-march after lighting, added into HDR). `0` = off. Deferred path only. Only shows on surfaces below the roughness cutoff. |
| `BENCH_CUBEMAP` | `horizonCubeMap` | IBL / skybox cube: `env_studio`, `env_powerplant`, `env_slipway`, `horizonCubeMap`, or any `Textures/<name>.dds`. Used directly when the reflection probe is off, and as the probe's own sky when it's on. |
| `BENCH_PROBE` | `1` | `1` = reflection probe: re-capture the scene into a prefiltered cube every N frames and use it for IBL. `0` = use `BENCH_CUBEMAP` directly. |
| `BENCH_PROBE_INTERVAL` | `30` | probe re-capture cadence in frames. |
| `BENCH_PROBE_POS` | scene centre | `x,y,z` world position for the probe. |
| _(probe box)_ | `sceneExtents*1.35` | box-parallax influence half-extents. Override per scene with `bench_probes_<scene>.json` = `{ "probes": [ { "pos":[x,y,z], "box":[hx,hy,hz] } ] }`, or drag in the Ambient/IBL panel. |
| `BENCH_MATBALL` | `0` | `1` = show the material-preview debug sphere (`Primitives/Sphere.fbx`). The Materials tab edits it as a full material (surface type incl. glass, constants or texture maps with Unreal packing, emissive, glass parameters) and can load/save a `.tgmat`. |
| `BENCH_MATBALL_TGMAT` | unset | `.tgmat` file for the preview sphere. |
| `BENCH_MATBALL_SURFACE` | `Opaque` | `Opaque`, `Masked` or `Transparent` (glass). |
| `BENCH_MATBALL_OPACITY` / `_ROUGHNESS` / `_METALNESS` | material | Preview sphere overrides, 0..1. |
| `BENCH_MATBALL_IOR` / `_REFRACTION` | `1.52` / `1` | Preview sphere glass index of refraction and screen-space refraction strength. |
| `BENCH_ORBITBALLS` | `0` | `N` = orbit N spheres (max 24) around the scene centre, all sharing the debug-sphere material. `BENCH_ORBIT_RADIUS` / `BENCH_ORBIT_SPEED` / `BENCH_ORBIT_BALLRAD` tune the ring. Emissive spheres each spawn a (non-shadow) area-light proxy. Live controls under *Material preview → Orbiting spheres*. |
| `BENCH_GI` | `1` | `1` = emissive-GI irradiance volume (lazy SH probes; primes over ~1 s at load then near-free). `0` = off. Deferred only. |
| `BENCH_GI_PRIME` | `8` | probes captured per frame while priming the volume (higher = faster prime, bigger load spike). |
| `BENCH_GI_VIZ` | `0` | `1` = show the raw GI irradiance term instead of the lit scene. |
| _(GI grid)_ | auto | `bench_gi_<scene>.json` = `{ "origin":[x,y,z], "spacing":[sx,sy,sz], "counts":[cx,cy,cz] }` overrides the auto grid; also editable in the Emissive GI panel. |
| `BENCH_SUN_PITCH` / `BENCH_SUN_YAW` | 55 / -35 | directional-light angle in degrees (also live in the tune panel). |
| `BENCH_POSTFX` | `1` | `1` = bloom + composite (ACES) post-fx pass, `0` = plain engine tonemap. Deferred path only. Auto-exposure is off by default (manual 1.0) — toggle it live in the tune panel's **Post FX** section. |
| `BENCH_GBUF` | `0` | `1`–`8` = show a channel instead of lighting (1 albedo, 2 normal, 3 roughness, 4 metalness, 5 baked AO, 6 emissive [HDR, `albedo·mask·strength`], 7 depth, 8 SSAO) |
| `BENCH_NOCULL` | – | non-zero disables the whole-model frustum cull |
| `BENCH_SCREENSHOT` | – | path — writes a PNG of the backbuffer near the end of the run |
| `BENCH_REPORT` | `bench_report.json` | report path (relative to `Bin/`) |
| `BENCH_SCENE` | first `.tgs` found | load `<name>.tgs` + its `<name>.leveldata/` folder: every object file (Model property inline, `path` to a `.tgo`, or `object-definition: <name>` resolved under the data root) is instantiated with its translation/rotation/scale (degrees). Set this only to choose a particular scene; when unset or empty, the first scene found is used. |
| `BENCH_CAMFILE` | `bench_camera_<scene>.json` | artist-placed camera for the scene (F5 in `scene.bat <scene>`). For `TEST` → `bench_camera_TEST.json`. |
| `BENCH_CAM` (scene) | **`fixed`** | a scene run holds the placed camera dead-still by default (repeatable benchmark, matches the framing). Raw model runs still default to `spin`. |
| `BENCH_NOVSYNC` | – | set non-zero to disable vsync in interactive mode too |

The report has `frame_ms` (engine frame delta — GPU-bound in the current forward
renderer), `cpu_ms` (Update+Render submit time only), `draw_calls_mean`,
`model_load_ms`, `first_frame_ms`, and the scene description.

## Phase 0 baseline (2026-09-09)

RTX 5060 Ti, 1600×900, vsync off, Release, forward PBR, 8 point lights + 1
directional + IBL ambient. Sponza = **120 sub-meshes → 120 draw calls per copy**.

| copies | draw calls | frame_ms mean | p50 | p99 | fps |
|-------:|-----------:|--------------:|----:|----:|----:|
| 1× | 120 | 1.7 – 2.0 (clock-noisy: GPU ~85% idle) | ~1.9 | ~4.8 | ~500 |
| 4× | 480 | 5.43 | 5.59 | 8.58 | 184 |
| 9× | 1080 | 12.02 | 12.17 | 16.7 | 83 |

Scaling is ~linear in draw calls (~11 µs / sub-mesh at frame level) — one draw
call per sub-mesh, no batching, no instancing. `cpu_ms` is tiny (0.03–0.25 ms):
the forward path is entirely GPU-bound.

## Phase 1 — draw-call reduction (2026-09-09)

Three changes, same bench:

- **A. Hoisted per-instance state.** `ModelShader` split into `RenderSetup()`
  (shader / input-layout / constant-buffer binds, once per instance) +
  `RenderMesh()` (textures + IA + `DrawIndexed`, per sub-mesh). Static meshes no
  longer map/bind the 8 KB bone buffer per sub-mesh.
- **C. Material merge at import.** `ModelFactory` collapses sub-meshes that share
  a material into one vertex/index buffer (Sponza **120 → 28**), baked into the
  mesh cache (v3).
- **D. Whole-model frustum cull.** `Model::GetBounds()` +
  `ModelDrawer::SetCullFrustum()` (opt-in; default off = unchanged behaviour).

| copies | draws P0 → P1 | frame_ms P0 | frame_ms P1 | Δ | cpu_ms P0 → P1 |
|-------:|-------------:|------------:|------------:|--:|---------------:|
| 1× | 120 → 28  | 1.7–2.0 | ~1.8 | flat (GPU idle-bound) | 0.03 → 0.009 |
| 4× | 480 → 106 | 5.43 | **4.14** | −24 % | 0.12 → 0.017 |
| 9× | 1080 → 131 | 12.02 | **6.18** | −49 % | 0.24 → 0.019 |

1× doesn't move: at 1.8 ms the GPU is ~85 % idle and clock-throttling, and there
is nothing off-screen to cull. The 9× win is mostly frustum culling
(~4.7 of 9 copies visible). The remaining 9× cost is **overdraw** — overlapping
geometry still runs the full forward PBR + 8-light loop per fragment; that is what
Phase 2 (deferred shading) targets.

`BENCH_NOCULL=1` disables the cull for A/B comparison.

## Phase 2 — deferred shading (2026-09-09)

`DeferredRenderer` (`Source/Game/source/DeferredRenderer.{h,cpp}`): 4-MRT G-buffer
+ shared depth → fullscreen PBR resolve → HDR (`R16G16B16A16F`) → engine tonemap
to the backbuffer.

- **G-buffer**: albedo `R8G8B8A8`, world normal `R16G16B16A16F`, ORM+emissiveMask
  `R8G8B8A8`, emissive `R11G11B10F`; depth is the shared `DX11::DepthBuffer`.
- **Geometry pass** reuses `PbrModelShaderVS` + new `GBufferPS.hlsl` via a
  `ModelShader`, so it inherits Phase 1's hoisted binds + frustum cull.
- **Lighting pass** (`DeferredLightingPS.hlsl`) reconstructs world pos from depth
  and runs the same `PBRFunctions.hlsli` light loop as the forward shader.
- Verified visually equivalent to the forward path (see the paired screenshots).

Both paths below include the Phase 1 changes + frustum cull:

| copies | forward | deferred | Δ | notes |
|-------:|--------:|---------:|--:|-------|
| 1× | 1.72 ms | **1.42 ms** | −17 % | overdraw from overlapping arches |
| 4× | 4.12 ms | **3.95 ms** | −4 %  | visible copies barely overlap |
| 9× | 6.19 ms | **4.95 ms** | −20 % | + frame pacing much tighter (p99 7.6 vs 9.9 ms, std 0.84 vs 1.64) |

Deferred also decouples lighting cost from geometry: adding lights only grows the
fullscreen pass. The 8-light cap (`NUMBER_OF_LIGHTS_ALLOWED`) currently hides that
— lifting it + tiled/clustered light culling is Phase 3.

Engine changes: `RenderTarget::GetRenderTargetView()` accessor;
`ModelDrawer::Draw(instance, shader)` now respects the cull frustum too.

## Phase 2.5 (in progress) — GPU profiler

`Source/Graphics/tge/render/GpuProfiler.{h,cpp}` — D3D11 timestamp queries, nested
`TGA_GPU_SCOPE(profiler, "name")` scopes, 5-frame ring-buffered non-blocking
readback. The bench brackets `geometry` / `lighting` / `composite` (deferred) or
`forward`; the report gains a `gpu_ms` block.

Per-pass GPU cost (RTX 5060 Ti, 1600×900):

| | frame | geometry | lighting | composite |
|--|------:|---------:|---------:|----------:|
| deferred 1× | 1.31 ms | 1.09 | **0.20** | 0.01 |
| deferred 9× | 4.74 ms | 4.52 | **0.19** | 0.03 |
| forward 1× | 1.69 ms | single pass | | |
| forward 9× | 6.17 ms | single pass | | |

**Lighting is flat ~0.2 ms regardless of scene size or overdraw** — the deferred
win, measured. All remaining cost is the G-buffer geometry pass (vertex throughput
+ 4-MRT bandwidth). Phase 3 (many lights) will therefore be cheap: more lights
only grow the fixed-cost fullscreen resolve.

## Model import cache

First load of `Sponza.fbx` via the FBX SDK: **~16 s**. `ModelFactory` now caches
the converted static-mesh data to `Bin/CookedAssets/meshcache/<asset>.tgmesh`
(compact 15-float vertices; full layout kept for meshes that use vertex colours /
extra UVs / skin weights). Subsequent loads: **~0.7 s** (~23×). Invalidated by the
`.fbx` write-time + size; skinned meshes stay on the SDK path.
