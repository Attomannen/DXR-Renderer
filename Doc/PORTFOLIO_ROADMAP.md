# DXR Renderer: Portfolio Roadmap

Where the DX12/DXR renderer stands, what it costs, and what to build next.
Rewritten September 2026 after the material, editor and measurement work.

---

## 1. Current state

| Area | Status |
|---|---|
| Renderer | Single inline-RayQuery compute pass: primary, direct + shadows, AO, GI lookup, reflections |
| Denoising | NVIDIA NRD 4.17.3 (REBLUR default, RELAX optional), checkerboard option |
| Upscaling | DLSS SR, DLAA and Ray Reconstruction, each independent of the others |
| Temporal | Native TAA (most stable), jitter-free motion vectors |
| Materials | Unreal packing (`_BC`/`_N`/`_ORM`/`_E`), one parameter block shared by raster, glass and DXR |
| Glass | Per-material IOR / thickness / absorption / opacity, composited after the ray-traced frame |
| Lighting units | Physical: sun in lux, sky and emissive in cd/m², lights in lumens/candela |
| Camera | Aperture / shutter / ISO -> EV100, metered auto exposure, EV comp, pre-exposure |
| Tonemapping | AgX (default), AgX Punchy, ACES |
| GI | Ray-traced SH probe volume |
| Raster path | Deferred: cascaded shadows, SSAO, SSR, clustered lights, fog |
| Editor | Viewport renders through the same renderer (ray-traced where available) |
| Tooling | Profiler tab, feature cost sweep, bench harness (`BENCH_*`), `--scan-alpha` cooker mode |

### Hardware context (read this before comparing numbers)

The dev machine is an **MSI Cyborg 15 with an RTX 5070 Laptop GPU capped at
60 W** (`nvidia-smi`: max power limit 60 W, default 50 W). A 5070 Laptop in a
larger chassis runs 100-115 W, so expect roughly 55-65% of published figures for
that GPU. Always quote the wattage next to the GPU name.

### Measurement method (earlier numbers in this doc were wrong without it)

- The GPU ramps from ~20% to 99% utilisation over the first seconds. Use
  **`BENCH_WARMUP=600`**; short warm-ups measure the ramp and vary by 50%+.
- Within one session, runs then repeat to about ±0.03 ms.
- **Across** sessions the same config still drifts (11.9 vs 17.8 ms on Bistro)
  as the machine heat-soaks against the 60 W cap. Never compare numbers from
  different sessions.
- For any A/B, use the interleaved feature sweep (`BENCH_COST_SWEEP=1`) or run
  baseline / change / baseline back to back and check the two baselines agree.
- Screenshot comparisons need identical `BENCH_FRAMES`: the scripted camera is
  parameterised by fraction of the run, so a different length moves the view.

### Measured costs (1600x900, native TAA + NRD)

| Scene | GPU frame | Ray pass | NRD |
|---|---|---|---|
| Sponza | 6.8-10.5 ms | 3.0-5.4 ms | 2.5-3.5 ms |
| Bistro | 9.8-17.8 ms | 5.9-12.3 ms | 2.5-3.4 ms |

Feature costs from the interleaved sweep:

| Feature | Sponza | Bistro |
|---|---|---|
| Primary rays + 1 sun shadow (floor) | 1.4 ms | 2.7 ms |
| Reflections | 0.5 ms | 4.0 ms |
| NRD denoiser | 3.4 ms | 2.5 ms |
| Direct light + shadows | 0.7 ms | 1.9 ms |
| Indirect GI lookup | 0.7 ms | 1.1 ms |
| Ambient occlusion | 0.5 ms | 0.8 ms |

DLSS Quality roughly halves the whole frame (Bistro 17.8 -> 8.4 ms measured in
one session).

---

## 2. Open bugs, in priority order

1. **DLSS-family temporal wobble.** Frozen camera, consecutive frames: native
   TAA 0.08, DLAA 2.08, DLSS Quality 1.77, RR 2.34. Ruled out: the ray jitter
   works (a forced 20 px offset shifts the image in both native and DLAA), the
   jitter sign and axis (all four combinations wobble identically) and NRD
   (wobbles with it on or off). Reporting **zero** jitter to Streamline is
   stable and no softer by an edge-detail metric. Next suspect: the DLSS reset
   flag being set every frame, which would make it reconstruct each jittered
   frame standalone.
2. **Bistro glass renders wrong** - shop windows appear as flat grey panels with
   heavy noise. Likely the glass pass reading the wrong scene copy, or those
   materials being misclassified.
3. **DX11 backend crashes on exit** in `Dx11Device::WrapNativeSrv`. DX12 is
   unaffected.
4. **Editor has no irradiance probe volume**, so indirect light there is
   ambient-only and differs from the game.

---

## 3. Performance plan

Ordered by value for the effort. The measurement method above is a prerequisite
for all of it.

### 3.1 Done this cycle
- **Cutout classification** (`--scan-alpha`): ray queries skip the alpha test on
  materials that provably have none. Sponza ray pass 7.05 -> 4.67 ms.
- **Reflection roughness cutoff 0.55 -> 0.35**: Bistro 11.88 -> 9.80 ms, no
  visible difference.
- **Reflection rays per pixel up to 8**: free, because the shader already scales
  the count with roughness.

### 3.2 SHARC radiance cache (the main one)
Reflection cost is the *shading* at each hit: a full direct shade plus GI and
environment lookup per sample. Capping ray length does nothing (measured). A
radiance cache replaces the re-shade with a lookup.
- SDK: NVIDIA RTX Kit / RTXGI 2.x (SHARC). Not yet vendored.
- Plan: populate the cache from the existing `DecodeHit`, query it in
  `TraceReflection` instead of `ShadeDirect`, keep the direct path for the
  primary hit.
- Expected: most of Bistro's 4 ms, plus multi-bounce reflections as a byproduct.
- Effort: about a week.

### 3.3 NRD in performance mode
NRD costs more than everything it denoises on Sponza (3.4 ms). Build it with
`REBLUR_PERFORMANCE_MODE`; expect roughly a third off. Cheap and immediate.

### 3.4 Split the ray dispatch
Separate primary, shadow, AO and reflection passes. Enables per-effect
resolution, honest per-pass timers, and is the prerequisite for ReSTIR and SER.

### 3.5 Shader Execution Reordering
Typically 10-30% on divergent secondary-ray shading, which is exactly the
reflection case. Needs a RayGen shader (DXR 1.2 / SM 6.9), so it depends on 3.4.

### 3.6 Opacity Micromaps
Cuts alpha-test traversal for foliage; Bistro pays about 7% for correct cutouts
today. Needs a newer Agility SDK plus the OMM SDK.

### Measured and rejected
- **Ray Reconstruction as the denoiser on this machine**: 5.44 ms at native
  against NRD's 2.47 ms. Revisit on a full-TGP GPU.
- **Capping reflection ray length**: no effect (7.98 vs 8.00 ms, bracketed).
- **Cheap reflection shading via an extra shader parameter**: made the whole hit
  path ~3 ms slower through codegen, in every variant. If retried, do it with a
  compile-time variant, not a runtime flag.

---

## 4. Portfolio features

| Feature | Why it stands out | Depends on |
|---|---|---|
| Path-traced reference mode | Proves the real-time image is physically right; A/B toggle | Physical units, pre-exposure |
| SHARC reflections + multi-bounce | Modern, measurable, fixes the top cost | 3.2 |
| ReSTIR DI (many lights) | Flagship RT technique; Bistro's lamps become real lights | 3.4 |
| Ray-traced glass | Replaces screen-space refraction; the material data already exists | - |
| Physical camera effects | Depth of field from aperture, motion blur from shutter | Camera model |
| Physical sky + atmosphere | Time of day with correct lux / EV | Photometry |
| HDR display output | Shows AgX + physical exposure on HDR monitors | Tonemapper |

---

## 5. Suggested order

1. Fix the DLSS wobble (bug 1) - it undermines every screenshot and video.
2. Fix Bistro glass (bug 2).
3. NRD performance mode (3.3) - cheap, immediate.
4. Path-traced reference mode - strongest single portfolio item, self-contained.
5. SHARC (3.2) - the real performance and quality win.
6. Split the dispatch (3.4), then ReSTIR DI and SER (3.5).

---

## 6. Completed this cycle

- **Refactors**: `DeferredRenderer` split into core / DXR / GI / atmosphere /
  post-FX; `GameWorld` split with `BenchConfig`, `SceneFiles` and
  `GiProbeScheduler`; compact 40-byte static vertex (4.5x smaller buffers).
- **Materials**: Unreal texture packing, one shared parameter block, per-material
  glass, cooker `--packing unreal` and `--scan-alpha`.
- **Raster deferred on DX12**: was rendering black. The BRDF LUT was never bound
  (ambient divided by zero), SSR cleared the environment slot and leaked its
  blend state, and the composite left alpha undefined.
- **Editor viewport**: renders through the deferred / ray-traced renderer with
  shadows, glass and scene lights; asset caches throttled and the TLAS rebuilt
  only when the scene changes.
- **DLSS / RR independence**: RR runs at any quality ratio, with motion vectors
  scaled by the render extent.
