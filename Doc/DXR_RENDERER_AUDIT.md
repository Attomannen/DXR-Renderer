# DX12 / DXR Renderer Audit — Performance Root Causes and Feature Roadmap

Scope: the DX12 DXR ("full DXR renderer") path only — `DxrSmokeTestCS.hlsl` +
`DxrCommon.hlsli` + `GiTraceInlineCS.hlsl` + `AtmosphereVolumeRTCS.hlsl`, and
their C++ drivers in `DeferredRenderer.cpp`, `Dx12Device.cpp` and
`GameWorld.cpp`. The raster/DX11 path is out of scope.

Everything below was read from source. Ray-count estimates are arithmetic from
the actual shader code and the actual default `Tunables`, not measurements —
they tell you where to point a profiler, not what the profiler will say.

---

## MEASURED (2026-09-16) — read this before the predictions below

### Benchmarking methodology on this machine — read first

This is a **laptop** GPU (RTX 5070 Laptop) and it thermally throttles hard
across a sequence of benchmark runs. A cold first run reads **~8.9 ms** where
the same config warm reads **~13.5 ms**. That is a 35% swing, larger than every
optimisation in this document combined, and it silently invalidated the first
two rounds of measurement taken for this audit.

Any A/B on this machine must therefore:

1. Throw away a warm-up run before recording anything.
2. **Interleave** the configurations (A,B,A,B,...), never run all of A then all
   of B.
3. Repeat 3+ times and take the **median**, not the mean — a single anomalous
   run (one 18.81 ms outlier in a set otherwise reading 12.8-13.2) skews a mean
   badly.
4. Treat differences under ~0.3 ms as noise even after all of the above.

Numbers below follow that protocol: Sponza, 1600x900, vsync off, Release, 450
frames after 120 warm-up, median of 3 interleaved reps on a fully warm GPU.
Repeatability at that point is excellent (13.49 / 13.48 / 13.51).

### Where the frame actually goes

| config | median frame | cost of the feature |
|---|---:|---:|
| baseline (`dxrAoSamples = 2`) | 13.49 ms | — |
| `BENCH_DXR_AO_SAMPLES=4` (the old hardcoded value) | 15.11 ms | AO at 4 rays = **2.84 ms** |
| `BENCH_DXR_AO=0` | 12.27 ms | AO at 2 rays = 1.22 ms |
| `BENCH_VOLUMETRIC=0` | 10.86 ms | volumetric fog = **2.63 ms** |
| `BENCH_DXR_REFLECTIONS=0` | 11.60 ms | reflections = **1.89 ms** |
| `BENCH_GI=0` | 13.01 ms | GI probe capture = 0.48 ms (already batched) |

After the AO and fog work below, the same bench reads **11.35 ms**, with fog
down to 0.48 ms:

| config | median frame | cost of the feature |
|---|---:|---:|
| current (`dxrAoSamples = 2`, bounded fog march) | 11.35 ms | — |
| `BENCH_VOLUMETRIC=0` | 10.87 ms | volumetric fog = **0.48 ms** (was 2.63) |
| `BENCH_VOLUMETRIC_STEPS=16` | 11.58 ms | no faster than 32 — the march already exits early |

Total so far: **15.11 -> 11.35 ms, +33% fps**, with no quality dial turned down
except AO 4 rays -> 2.

**The predicted cost ordering in 1.9 was wrong.** Corrected, measured order at
the original defaults:

1. **DXR AO, 2.84 ms** — predicted 5th, actually 1st.
2. **Volumetric fog, 2.63 ms** — predicted 2nd. Correct.
3. **Reflections, 1.89 ms** — predicted 1st, actually 3rd.
4. GI probe capture, 0.48 ms.
5. Specular AA, and `FORCE_NON_OPAQUE` traversal overhead — both at noise.

Why reflections were overestimated: `dxrReflectionRoughnessCutoff` is 0.55 and
Sponza is almost entirely rough stone and cloth, so few pixels ever enter the
reflection path. The 29-rays/pixel arithmetic in 1.1 is the *worst case per
reflective pixel*, not the scene average. A shiny scene would move reflections
back up this list; do not generalize this row from Sponza alone.

### The single most important finding

**Only ray count moves this renderer. Every per-ray micro-optimisation tried --
traversal *and* shading alike -- landed inside the noise floor.**

Four independent A/B tests, all interleaved on a warm GPU, all came back at
noise:

- Removing `RAY_FLAG_FORCE_NON_OPAQUE` from camera/reflection/GI/fog rays in
  favour of per-instance TLAS flags: 15.12 vs 15.16 ms.
- Doing the same for AO rays and dropping their same-instance rejection so they
  could use traversal hardware: 15.33 vs 15.21 ms.
- Specular AA (4 extra anisotropic normal-map taps per hit): no change.
- Bounding reflection ray `TMax` from 100000 to 4000: no change once repeated.

Against that, every change that removed whole rays paid immediately: AO 4 -> 2
samples was 1.62 ms, and bounding the fog march was 2.15 ms.

Note what this does *not* say. An earlier draft of this section concluded "not
traversal-bound, therefore shading-bound" -- but specular AA is pure per-hit
shading work and removing it also did nothing, so that inference was wrong. The
supported statement is narrower and more useful: **cost tracks the number of
rays cast, and work removed from inside a ray -- of either kind -- disappears
into latency.** Plan accordingly: pursue anything that reduces ray or sample
counts (denoiser, ReSTIR, half-res passes, adaptive sampling, cheaper secondary
shading); do not spend time shaving instructions or texture taps out of a hit.

### Changes applied, with measured results

- **`dxrAoSamples` tunable, default 4 -> 2 (1.1 item 4)** — applied. **1.62 ms,
  10.7% of the frame** (15.11 -> 13.49 ms), the only change here that clearly
  paid. AO was a hardcoded `4u` in an unrolled loop; it is now a tunable with an
  ImGui slider and `BENCH_DXR_AO_SAMPLES`. The converged image barely moves
  (mean absolute difference between the 4-ray and 2-ray AO debug views was
  0.55/255) because the temporal resolve accumulates across frames — the real
  cost of fewer samples is noise *during camera motion and disocclusion*, which
  a static benchmark cannot show. Check it in motion before committing to 2, and
  drop to 1 (a further ~0.6 ms) once a denoiser exists.

- **Bounded volumetric fog march (1.4)** — applied. **2.15 ms, 2.63 -> 0.48 ms.**
  The march fired an unconditional full-scene shadow ray on every one of its 32
  steps. It now (a) carries the previous segment's exit transmittance into the
  next, halving the `FogOpticalDepth` evaluations, (b) skips the shadow ray for
  any step whose scattering weight is already negligible, and (c) breaks once
  the remaining transmittance falls below a threshold. Both exits have provable
  error bounds rather than being quality guesses, because `FogOpticalDepth` is
  monotonic in distance, so everything still to come from step i is bounded by
  `exp(-tau(a_i))`. Confirmed by the fact that `BENCH_VOLUMETRIC_STEPS=16` is
  now *no faster* than 32: the march already exits long before the step cap, so
  the step count no longer needs to be traded against quality at all.

  Two bugs fixed in the same loop, both of which change how fog looks:
  - The jittered sample position read `a + (float(i) + offset) * stepLength`
    while `a` already contained `i * stepLength`, so the sample point advanced
    at **double** the intended rate and ran to twice the end distance. Roughly
    half the shadow rays were fired outside the fog volume entirely, and every
    sample was paired with a `weight` belonging to a different segment.
  - The phase function hardcoded `g = 0.8` with an inlined Henyey-Greenstein,
    silently ignoring `Tunables::volumetricAnisotropy` (default **0.45**) and
    leaving the matching `FogPhase()` helper in `AtmosphereCommon.hlsli` dead.
    Now calls `FogPhase()`, so the authored value is actually used.

- **Batched GI probe dispatch (1.3)** — applied. ~0.5 ms, and it removes a
  pathological dispatch shape (N serialized single-thread-group dispatches with
  a full UAV barrier between each). Smaller than first measured, but the shape
  fix matters more than the number: the win scales with batch size, so it is
  considerably larger during priming (8-32 probes/frame) than in the 4/frame
  steady state this bench measures. `BENCH_GI_BATCH=0` restores the old
  behaviour for A/B.

- **Per-instance FORCE_OPAQUE / FORCE_NON_OPAQUE TLAS flags (1.2)** — applied,
  **no measurable win**, kept because it is strictly more correct and costs
  nothing. Every Sponza material is already classified `kRayOpaque`, so
  `AcceptRayTriangle` was already returning on its first branch. Re-measure on
  content with real alpha-cutout foliage before judging it.

### Diagnosed 2026-09-16: "one row of tapestries is much darker than the other"

Reproduced on Sponza (`BENCH_CAM=room BENCH_SHOT_FRAME=66`, which is ~0.75 s
into the orbit and frames both rows at once). Chased through the debug views
rather than guessed:

- **View 10 (`TraceSunVisibility`) is the answer.** The sunward row reads solid
  white, the shaded row solid black. The brightness split is a genuine
  sun/shadow boundary, not a shading bug.
- View 2 (environment) is close to uniform across both rows, so the environment
  term is not what separates them.
- Inverted shading normals were the first hypothesis and are **wrong**. Flipping
  `shadingWorldNormal` into `geoWorldNormal`'s hemisphere produced a
  *byte-identical* frame (mean abs diff 0.0000 over 90k sampled pixels), i.e.
  the guard never fires on this content. The change was reverted rather than
  left in as dead code; see the note in `DecodeHit` for how to reinstate it if
  content ever does show it.

So the real complaint is not that the shadowed row is shadowed -- it is that
shadow falls off a cliff to near-black, because the only things filling it are
`dxrAmbientIntensity` (0.02 by design) and a GI volume that currently evaluates
**only the L0 SH band** (see `EvaluateDxrGi`, and the unresolved regression in
`SESSION_AUDIT_DXR_GI.md`). This is a GI problem, and 2.3 is where it gets
fixed. It is worth noting that this is the second time in this audit that a
plausible shading-code hypothesis lost to a five-minute debug-view check.

- **Not applied: dropping AO's same-instance rejection.** It bought no
  performance, so it was reverted rather than left in as an unvalidated change
  to how AO looks. It remains worth revisiting as a **correctness** question:
  a mesh occluding itself is real ambient occlusion, and because `ModelFactory`
  merges sub-meshes by material, one "instance" is a large share of the scene,
  so AO currently under-occludes systematically. That is a look change, not a
  perf change, and should be judged on a scene view that actually shows
  self-occlusion (the saved Sponza bench camera is a flat wall close-up and is
  useless for it).

---

## Part 1 — Why performance is awful

### 1.1 The per-pixel ray budget is roughly 30 rays, and ~20 of them are reflections

Trace the default path for one camera pixel (`DxrSmokeTestCS.hlsl::main`) with
stock tunables (`dxrReflectionSamples = 4`, `dxrAmbientOcclusion = true`,
`dxrReflectionRoughnessCutoff = 0.55`):

| Work | Rays | Source |
|---|---|---|
| Primary | 1 | `main` |
| `TraceAmbientOcclusion` | 4 | 4-ray stratified hemisphere |
| `ShadeDirect` -> `TraceSunVisibility` | 4 | 4 sun-disc samples |
| `ShadeDirect` -> punctual lights | N | one shadow ray per in-range light |
| Reflections: 4 x `TraceReflection` | 4 | the reflection rays themselves |
| ...each of which calls `ShadeDirect` again | 4 x (4 + N) | full sun + all lights, **per reflection sample** |
| **Total** | **29 + 5N** | |

At 1600x900 with zero local lights that is **~42M fully-shaded rays per frame**.
With 8 local lights in range it is ~100M. This is the single biggest cost, and
the recursive `ShadeDirect` inside `TraceReflection` is the dominant term — 24
of those 29 rays exist to shade reflections.

`TraceReflection` also calls `DecodeHit` (15 `ByteAddressBuffer` loads + up to 4
anisotropic texture samples) and `EvaluateDxrGi` (8 probes x 9 SH float4 loads)
on every one of its 4 samples. The reflection path is not a cheap secondary
bounce; it is a second full deferred shade.

**Actions, in order of payoff:**

1. Drop `dxrReflectionSamples` to **1** and add a real reflection denoiser
   (see 2.1). Four unfiltered samples is an expensive way to get a result that
   still needs denoising. This alone should be close to a 2x frame-time win.
2. Inside `TraceReflection`, replace the full `ShadeDirect` with a cheap
   reflection shade: sun only (1 shadow ray, no disc sampling), no punctual
   shadow rays, no specular lobe. A reflection of a shadow is not worth 4+N
   rays. Gate the expensive version behind roughness < ~0.1 if you want mirrors
   to keep contact shadows.
3. `TraceSunVisibility` fires **4 rays** to soften a sun whose angular radius is
   hardcoded at `0.00329` rad (0.19 degrees). At 1 px ~= 0.03 degrees that
   penumbra is a few pixels wide. Use **1 ray** with the existing frame-index
   rotation and let the temporal resolve converge it. Straight 4x reduction of
   all sun shadow cost, on primary *and* reflection hits.
4. `TraceAmbientOcclusion` fires 4 more rays at full resolution, on top of a GI
   volume that already encodes occlusion. Either drop to 1 ray + denoise, or
   run AO at half resolution and upsample.

### 1.2 `RAY_FLAG_FORCE_NON_OPAQUE` is on every single RayQuery in the engine

Every `RayQuery` template in `DxrCommon.hlsli`, `DxrSmokeTestCS.hlsl`,
`GiTraceInlineCS.hlsl` and `AtmosphereVolumeRTCS.hlsl` carries
`RAY_FLAG_FORCE_NON_OPAQUE`. Meanwhile `Dx12Device::CreateRaytracingBlas` builds
every BLAS with `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE`
(`Dx12Device.cpp:937`) — the force flag overrides it.

The consequence is not just the texture sample. It is that **every candidate
triangle of every ray returns to the shader** through the `Proceed()` loop
instead of being resolved entirely inside the RT cores' traversal hardware. The
`kRayOpaque` fast path added in the previous session (`AcceptRayTriangle`
returning `true` immediately) removes the texture fetch but **not** the
traversal round-trip, which is the larger half of the cost. This is a
multiplicative tax on all ~30 rays per pixel.

**Fix — this is the highest value-per-line change in the audit:**

`Dx12Device::BuildRaytracingTlas` hardcodes
`d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE` (`Dx12Device.cpp:1116`). Plumb
the material's `rayVisibility` through `RaytracingInstanceDesc` and set
`D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE` for instances whose material is
`kRayOpaque`. Then remove `RAY_FLAG_FORCE_NON_OPAQUE` from the RayQuery
templates and pass it per-trace only where cutout geometry can actually be hit.
Because it is a TLAS instance flag, it costs nothing at build time and needs no
BLAS rebuild.

Expect this to be worth 1.5-3x on traversal for scenes that are mostly opaque
(i.e. all of them).

Note this changes the `DecodeHit` signature — its parameter is the concrete
`RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_NON_OPAQUE>`
type (`DxrCommon.hlsli:502`), so the template flags must be changed in lockstep
across every call site.

### 1.3 GI probes are dispatched one thread group at a time, with a barrier between each

`DeferredRenderer::GiProjectProbeRT` ends with:

```
ctx.Dispatch(1, 1, 1);   // one 64-thread group traces and reduces this probe
ctx.UavBarrier(myGiShBuffer.Handle());
```

One group of 64 threads occupies **one SM out of the 40-80 on the GPU**, with a
full pipeline drain after it. With the default `giRTRayCount = 256` and
`giTrickle = 4`, `CaptureGiProbesImpl` issues **4 of these serialized,
single-group, barrier-separated dispatches every frame, forever**
(`giKeepUpdating` defaults to `true`, `giFrameSkip = 1`).

Each probe traces 256 rays as 4 sequential 64-ray batches, each ray doing a full
`DecodeHit` + `ShadeDirect` (so 4 sun shadow rays + N light rays each). That is
~6k fully-shaded rays executed at roughly 1/60th of the GPU's width, with no
occupancy to hide traversal latency, four times per frame. Milliseconds, not
microseconds.

**This cost is also completely invisible in your profiler.**
`CaptureGiProbesImpl` is called at `GameWorld.cpp:2672`, *before*
`s.gpu.BeginFrame()` at line 2678, so it falls outside every `RenderGraph` GPU
scope. If you have been profiling the DXR pass and the numbers did not add up to
your frame time, this is the gap.

**Fix:** restore the batched dispatch. `Doc/SESSION_AUDIT_DXR_GI.md` documents a
`GiProjectProbeBatchRT` / `GiProbeBatchEntry` design that was reverted as part of
isolating the GI regression — one `Dispatch(batchSize,1,1)` indexed by
`SV_GroupID.x`. That is exactly the right shape. Reintroduce it, with one
`UavBarrier` after the whole batch rather than per probe. Secondary: move
`CaptureGiProbesImpl` inside the `gpu.BeginFrame()`/`EndFrame()` window and give
it its own scope so it stops being unmeasured.

### 1.4 Volumetric fog fires up to 32 shadow rays per half-res pixel, on by default

`fogEnabled = true`, `volumetricEnabled = true`, `volumetricSteps = 32` are all
defaults (`DeferredRenderer.h:218-230`). In DXR mode `RenderAtmosphere` selects
`myVolumeRTCS` = `AtmosphereVolumeRTCS.hlsl`, whose march calls
`FogSunVisibility` — a full-scene `TMax = 100000` shadow ray — **once per step**.

At 1600x900, half resolution is 800x450 = 360k pixels x 32 steps =
**11.5M unbounded shadow rays per frame**, purely for fog. That is a third of
the camera pass's entire budget, enabled by default, and it is trivially
reducible:

- Cap `TMax` at the light's meaningful range rather than 100000.
- Use a coarse shadow representation for fog — the existing CSM, or a low-res
  ray-traced visibility volume — instead of a per-step scene trace.
- Drop to 8-12 steps and lean on the existing interleaved jitter + temporal
  resolve, which the shader is already written to do.
- At minimum, default `volumetricEnabled` to `false` in DXR mode until the above
  lands, so the cost is opt-in.

### 1.5 Every hit loops all 1024 possible lights with no culling

`ShadeDirect` (`DxrCommon.hlsli`) does:

```
for (uint li = 0; li < lightCount; ++li)
    localLit += EvaluatePunctualLight(gLights[li], ...);
```

`kMaxLights` is 1024. There is no spatial culling of any kind on the DXR path —
and this loop runs on the primary hit *and* on all 4 reflection hits. With 20
lights that is 100 potential shadow rays per pixel.

`ClusterCullCS.hlsl` and `DeferredRenderer::CullClusters` already exist and
already build a per-cluster light list — but `BuildFrame` returns early for
`IsDxrRenderer()` (`DeferredRenderer.cpp:1590-1600`), so clustered culling never
runs in DXR mode.

**Fix (short term):** run `CullClusters` in DXR mode too and have `ShadeDirect`
walk the cluster's light list from the hit's view-space position instead of the
global array. Most hits will see 0-4 lights instead of all of them.

**Fix (right answer):** RIS / ReSTIR DI — see 2.2.

### 1.6 Smaller but real shader-level waste

- **`RayTextureMip` is computed on every textured hit and used only by debug
  view 6.** `DecodeHit` unconditionally runs
  `s.textureMip = RayTextureMip(mat.albedoSrv, uvDx, uvDy)`
  (`DxrCommon.hlsli:619`), which does a `GetDimensions` on a bindless descriptor
  plus ~10 ALU. The only consumer is
  `if (gLightingView == 6u)` in `DxrSmokeTestCS.hlsl:311`. Delete it from the
  hot path, or gate it on the debug view. Free win on every hit in the renderer,
  including all reflection and GI hits.

- **16x anisotropic filtering on incoherent rays.** `myDxrSmokeSampler` is
  created with `filter = Anisotropic, maxAnisotropy = 16`
  (`DeferredRenderer.cpp:372-373`) and is the sampler for *every* ray texture
  fetch. Anisotropic filtering costs up to 16 taps per sample and only pays off
  where the footprint is genuinely elongated and coherent — true for primary
  rays, worthless for reflection and GI hits. Use a second trilinear (or 4x)
  sampler for secondary rays; with up to 4 maps sampled per hit this is up to 64
  taps per reflection hit you are paying for nothing.

- **Specular-AA costs 4 extra normal-map samples per hit.** `specularAaEnabled`
  defaults to `true`; when set, `DecodeHit` takes 4 additional `SampleGrad` taps
  of the normal map for any hit with `roughness < 0.7` — with the 16x aniso
  sampler above. This also runs on reflection hits, where it is pointless.
  Gate it to primary rays only (pass `specularAaStrength = 0` from
  `TraceReflection`).

- **`DecodeHit` loads 42 floats per hit.** Position, normal, UV, tangent and
  binormal for 3 vertices = 15 `ByteAddressBuffer` loads. The binormal is
  redundant — it is reconstructible as `cross(n, t) * sign`, saving 3 loads.
  Longer term, an octahedral-packed ray vertex format (pos f32x3, normal+tangent
  packed to 8 bytes, UV f16x2 = 24 bytes) would roughly halve the hit-shading
  bandwidth for the whole renderer.

- **`AcceptRayTriangle` reloads two structured-buffer records per candidate
  triangle** (`gRayGeometry[instanceId]`, then `gMaterials[g.materialIndex]`)
  before it can even check the fast-path flag. Once 1.2 lands this is only on
  genuinely masked geometry and stops mattering; until then it is on everything.

### 1.7 CPU-side: the TLAS gather loop reallocates per frame

`GameWorld.cpp:2556-2665` rebuilds, every frame:

- `std::vector<rhi::RaytracingInstanceDesc> rayInstances` — no `reserve`, grows
  by reallocation to one entry per *mesh* of every instance in the scene.
- `std::map<const ModelInstance*, Matrix4x4f> nextRayTransforms` — a red-black
  tree with **one heap allocation per instance per frame**, plus an O(log n)
  `find` into the previous frame's map per instance.

Replace both with `std::unordered_map` reserved once and a `rayInstances` vector
kept as a member and `.clear()`ed. In a Release build this is probably 0.2-1 ms;
**in a Debug build with checked iterators it can be 10-50x that**, which is
worth confirming — if you are benchmarking a Debug configuration, that is its
own answer and everything above is secondary.

`RegisterRaySceneSrv` is correctly cached (hash lookup after first call), and
`RayTracingMaterialTable::Upload` is correctly deduplicated per frame+revision.
Those are fine.

### 1.8 TAA jitter is hardcoded off, so nothing stochastic ever fully converges

`RenderDxrSmokeTest` sets `myTaaJitter = {0,0}` unconditionally
(`DeferredRenderer.cpp:1440`), with a comment explaining why: the RayQuery
camera would jitter ray directions while the projection matrices and motion
vectors stay unjittered, which is not a valid TAA coordinate system.

The reasoning is correct, but the consequence is that the temporal resolve
recovers *no* geometric detail and only accumulates the per-frame stochastic
rotation of AO and reflections. This is very likely *why* the sample counts in
1.1 were pushed to 4 in the first place — a vicious cycle where weak temporal
convergence forces expensive per-frame sampling.

Fixing jitter properly (jitter ray directions, `myViewToProj`, and the motion
vector reprojection as one unit) is a prerequisite for cutting sample counts,
and it is also a hard requirement for DLSS to perform correctly — DLSS is being
handed `myTaaJitter = 0` today, which will visibly cost it quality.

### 1.9 What to measure first

Before changing anything, close the measurement gap:

1. Move `CaptureGiProbesImpl` inside the profiler window (1.3) — you are
   currently blind to it.
2. Split the `dxrRenderer` scope into sub-scopes, or just bisect with the
   existing tunables: set `dxrReflections = false`, then
   `dxrAmbientOcclusion = false`, then `fogEnabled = false`, then
   `giKeepUpdating = false`, one at a time, and record the frame time after
   each.

Predicted ordering of costs at defaults, largest first: reflections (1.1) >
volumetric fog (1.4) > `FORCE_NON_OPAQUE` overhead spread across everything
(1.2) > serialized GI probes (1.3) > AO (1.1) > sun disc sampling (1.1).
Confirm before acting.

---

## Part 2 — Features and additions worth building

Ordered by value relative to effort, given where the renderer currently is.

### 2.1 A real ray denoiser (highest value)

`TemporalResolveCS.hlsl` is a TAA — a temporal resolve with neighbourhood
clamping. It is not a ray denoiser: there is no variance estimate, no spatial
pass, no per-signal handling. That is why AO needs 4 rays, reflections need 4
samples, and the sun needs 4 disc samples: nothing downstream can clean up a
1-sample estimate.

Adding a proper denoiser is what unlocks every sample-count reduction in Part 1.
Options, cheapest first:

- **A-trous / SVGF-lite**: temporal accumulation with per-pixel variance, then
  2-3 edge-stopping a-trous passes guided by the depth + normal + roughness you
  are *already writing* (`gSurfaceGuide`, `gTemporalDepth`, `gMotionVectors`).
  All the inputs exist. This is the pragmatic in-engine choice.
- **NRD (NVIDIA Real-Time Denoisers)**: drop-in, handles diffuse/specular/AO/
  shadows as separate signals, expects exactly the guide buffers you already
  produce. You already link Streamline, so the integration pattern is familiar.
- **DLSS Ray Reconstruction**: already wired up
  (`EvaluateRayReconstruction`, `gDiffuseAlbedo`/`gSpecularAlbedo` guides are
  already written) but off by default and hamstrung by zero jitter (1.8). Fix
  jitter and this may be most of the win for free.

Denoise separately: reflections, AO, and GI have completely different frequency
and disocclusion characteristics and should not share one resolve.

### 2.2 ReSTIR DI (direct lighting)

Replaces 1.5 properly. Instead of N shadow rays per hit, do weighted reservoir
sampling over the light set with spatiotemporal reuse: **one** shadow ray per
pixel regardless of light count, with quality that *improves* as lights are
added. This is the single change that would let the DXR path scale to the 1024
lights the buffer is already sized for.

Natural follow-on: ReSTIR GI to replace or supplement the probe volume, which
would remove the boiling/facet class of problems the probe volume keeps
producing.

### 2.3 Finish and harden the DDGI volume

The volume already has 8x8 octahedral distance moments (`GiVisibility`) but
`EvaluateDxrGi` deliberately does not use them — the comment explains they
produced visible radial lobes on flat receivers. Standard DDGI solves exactly
this with:

- **Probe relocation**: nudge probes out of geometry toward open space. Probes
  inside walls are the usual cause of the "circular pools" artifact.
- **Probe classification**: mark fully-enclosed probes inactive and skip tracing
  them entirely — a direct perf win on 1.3 as well.
- **Proper Chebyshev weighting with a depth bias** and normal-biased sampling,
  which is what makes the moments usable rather than a visible kernel.

Only the L0 SH band is used today (`EvaluateDxrGi`), which is why GI is flat.
The L1/L2 path exists and is mathematically checked; per
`Doc/SESSION_AUDIT_DXR_GI.md` it needs to be reintroduced *one change at a time*
against a known-good baseline. Probe relocation first will likely make the
directional bands behave.

### 2.4 Shader Execution Reordering / coherency

Reflection and GI rays are maximally incoherent, and every one of them runs the
full bindless `DecodeHit` + `ShadeDirect` inline. On Ada and later,
`NvHitObject` / SER reorders threads by hit material before shading and
routinely gives 20-40% on exactly this kind of workload. Requires moving
secondary rays from inline `RayQuery` to a DXR 1.0 pipeline with a shader table
— a real refactor, but the payoff scales with everything else you add.

Cheaper first step in the same direction: sort/bin reflection rays by material
index in a compute pre-pass.

### 2.5 Variable-rate and adaptive sampling

- Trace reflections at half resolution and upsample with the roughness guide;
  keep full-rate only below ~0.1 roughness.
- Skip AO entirely where the GI volume already reports low visibility.
- Adaptive per-pixel sample counts driven by the denoiser's variance estimate —
  spend rays where the image is actually noisy.

### 2.6 Transparency and refraction

`kRayTransparent` materials are excluded from DXR hits entirely and composited
by the raster forward pass. Ray-traced refraction/absorption would make the
existing glass material actually correct, and is a natural fit now that
`TraceReflection` exists.

### 2.7 Infrastructure worth having

- **BLAS management**: `CreateRaytracingBlas` calls `WaitForGpuIdle()` **twice**
  per BLAS (build, then compaction). At load time that is merely slow; the
  moment you stream anything it is a hitch per mesh. Batch BLAS builds into one
  command list with one fence, and defer compaction to a later frame.
- **BLAS refit for skinned/animated geometry** — there is no update path today,
  so animated meshes cannot be correctly ray-traced.
- **TLAS build flags**: TLAS uses `PREFER_FAST_BUILD`. For a mostly-static scene
  `PREFER_FAST_TRACE` is the better trade — TLAS build is cheap relative to the
  30 rays/pixel traversing it.
- **Emissive geometry as light sources** — currently emissive surfaces only
  contribute via the probe volume, so they do not cast direct light or sharp
  shadows. Light-BVH sampling over emissive triangles would fix this and pairs
  naturally with ReSTIR DI.

---

## Summary — suggested order of work

Revised against the measurements at the top of this document, not the original
predictions.

Done:
- ~~`FORCE_OPAQUE` TLAS instance flags (1.2)~~ — applied; no measurable win,
  kept anyway, re-measure on cutout-heavy content.
- ~~Batched GI probe dispatch (1.3)~~ — applied; ~0.5 ms, more while priming.
- ~~`dxrAoSamples` 4 -> 2 (1.1)~~ — applied; **1.62 ms / 10.7%**, the one clear
  win so far. Validate in motion, not just on a static bench.

- ~~Bounded volumetric fog march (1.4)~~ — applied; **2.15 ms**, plus two bug
  fixes that change how fog looks.

Next, in measured-payoff order:
1. **Denoiser (2.1).** The enabler for everything else: it is what lets AO go to
   1 ray and reflections to 1 sample without visible noise in motion. Given the
   "not traversal-bound" finding above, reducing sample counts is the *only*
   lever that has actually moved this renderer, and the denoiser is what makes
   those reductions safe.
2. **Reflections (1.89 ms).** Cut `dxrReflectionSamples` to 1 once the denoiser
   lands (`BENCH_DXR_REFLECTION_SAMPLES` exists for A/B), and replace the full
   recursive `ShadeDirect` inside `TraceReflection` with a cheap reflection
   shade.
3. Fix TAA jitter as one coherent unit (1.8). Also required for DLSS quality.
4. Cheap shader cleanups with no visual cost: delete `RayTextureMip` from the
   hot path, trilinear sampler for secondary rays, specular-AA on primary rays
   only, sun disc 4 rays -> 1 (1.1, 1.6).
5. CPU: `std::map` -> `unordered_map` in the TLAS gather loop (1.7). `cpu_ms` is
   currently ~0.7 ms of a 15 ms frame, so this is not urgent on this scene.
6. Clustered light culling in DXR mode (1.5), then ReSTIR DI (2.2) — Sponza
   benches with 0 local lights, so 1.5 is invisible here and will matter as soon
   as the scene has real light counts.

---

## Session addendum 2026-09-16 (2): TAA jitter, and a negative result on reflections

### Sub-pixel jitter enabled (1.8) — applied

`RenderDxrSmokeTest` pinned `myTaaJitter` to zero. The comment there said the
ray offset, the projection matrices and the motion vectors were not jittered
"as one unit". Re-reading the contract, two of the three were already right:

- `TemporalResolveCS` is written for jittered input. Its header says "unjittered
  pixel motion", and it reconstructs the fixed output grid by fetching current
  samples at `p + 0.5 - Jitter`.
- The fog volume already applies the matching `-jitter` when it reads ray depth.
- `myTaaJitter` was already plumbed to the rays, the resolve, the fog and
  DLSS/RR.

The projection matrices are deliberately left unjittered -- nothing samples
through them, they only project hit points for motion vectors, and keeping them
clean is what makes motion jitter-free. The one genuine inconsistency was in
`WriteTemporal`, which measured motion from `ClipToPixel(currentClip)` (the
jittered sample position) rather than from the pixel centre, leaving a sub-pixel
bias exactly equal to the jitter. Fixed; that change is a no-op while jitter is
zero.

Jitter is a Halton(2,3) sequence on [-0.5, 0.5] render pixels, with the phase
count scaled by the DLSS upscale ratio (8 at 1:1, up to 64). It is suppressed
when neither TAA nor DLSS is resolving, and for one frame after a light edit.
`Tunables::taaJitter` / `BENCH_TAA_JITTER` / an ImGui checkbox A/B it.

Measured, dead-still camera and again on the architectural orbit view, using
mean absolute Laplacian as an edge-harshness proxy alongside std-dev to prove it
is not just blurring:

| view | meanAbsLaplacian on -> off | stdDev on -> off | frame |
|---|---|---|---|
| static wall (mostly texture) | 0.00802 -> 0.00868 (**-7.6%**) | 0.07482 -> 0.07539 (-0.8%) | equal |
| architectural orbit (geometric edges) | 0.05362 -> 0.08255 (**-35%**) | 0.23044 -> 0.23171 (-0.5%) | 11.11 vs 11.11 ms |

So: 35% less edge aliasing where there are real geometric edges, contrast
preserved, and no frame cost. This is also the precondition for DLSS Ray
Reconstruction being worth enabling -- it was previously being handed a jitter
offset of zero.

### Negative result: bounding reflection ray TMax does nothing

Reflection rays use `TMax = 100000`. The hypothesis was that a reflection ray
which hits nothing is the most expensive outcome, because it must traverse the
whole BVH before it can conclude "miss" and fall back to the environment cube --
so bounding it should be a clear win at grazing angles where rays skim far
across the scene.

A single run at `TMax = 4000` appeared to save 0.76 ms. Repeated and
interleaved, it did not: identical configurations spread from **11.06 to 11.90
ms**, which swamps the effect entirely. The change was reverted rather than
shipped as a speculative knob.

This is the third hypothesis this session to die on contact with a repeated
measurement (after `FORCE_OPAQUE` traversal and the inverted-normal theory), and
the second to have been "confirmed" by a single run first. On this machine a
single run is not evidence. Follow the protocol at the top of this document.

### Camera height vs reflection cost — partially reproduced

`BENCH_ORBIT_HEIGHT` was added to pin the orbit height (it otherwise oscillates
over the run, averaging the effect away). Reflection cost, orbit camera:

| camera height | reflections on | off | cost of reflections |
|---|---:|---:|---:|
| low (0.02) | 11.90 ms | 9.21 ms | **2.69 ms** |
| high (0.35) | 11.67 ms | 9.50 ms | **2.17 ms** |

So reflections do cost ~24% more with a low camera, but total frame time barely
moves (11.90 vs 11.67) and this is nowhere near the severity reported from
free-flight. The orbit camera still looks *down* at the scene; it never produces
a true grazing view along the floor, which is the case that should be worst.

The mechanism to check in a real grazing view -- stated as hypothesis, not
measurement -- is screen coverage of the roughness gate. Reflections only run
where `hs.roughness < dxrReflectionRoughnessCutoff` (0.55, generous). A polished
floor filling the lower screen flips a large pixel count from ~9 rays to ~29
(4 samples x (1 reflection ray + a full ShadeDirect on the hit)). That is a
coverage effect, not a "how much is there to reflect" effect: ray cost is driven
by ray count and traversal distance, and the scene's emptiness does not reduce
either.

---

## Session addendum 2026-09-16 (3): DLSS Ray Reconstruction measured, and rejected

RR was the recommended denoiser because it is already integrated, already fed
the guide buffers, and was only blocked by the zero jitter fixed above. It now
runs -- `DeferredRenderer` logs `DXR denoiser: DLSS Ray Reconstruction active`
once, and logs an explicit error if Streamline declines it, so "the denoiser did
nothing" can never again be confused with "Streamline silently refused".

`BENCH_DXR_DENOISER=1` enables it; `BENCH_DLSS_MODE=0..5` selects
native / DLAA / Quality / Balanced / Performance / Ultra Performance.

Sponza, 1600x900, warm, interleaved, `BENCH_SCENE=Scenes/Sponza`:

| config | frame | vs native |
|---|---:|---:|
| native temporal | 10.68 ms | — |
| DLAA (no RR) | 11.80 ms | +1.1 ms |
| **DLAA + RR** | **19.85 ms** | **+9.2 ms** |
| DLSS Quality / Performance / Ultra Perf | 8.33 ms | *capped, see below* |

**RR costs ~8 ms on top of DLAA at 1600x900 on this laptop RTX 5070.** The rest
of the entire frame is ~11 ms. DLAA alone costs a healthy ~1.1 ms through the
exact same Streamline plumbing, so this is the RR model itself, not the
integration. Disabling `alphaUpscalingEnabled` (pointless at 1:1) saved ~2 ms of
that and has been left off.

Pairing RR with upscaling does not rescue it: RR's cost is driven by *output*
resolution, so it stays ~8-10 ms while only the ray budget shrinks. The code
gates RR to `!superResolution` anyway, i.e. to the single most expensive
configuration available.

**Conclusion: RR is not the right denoiser for this GPU at this resolution.** A
fixed ~8 ms is not payable out of an 11 ms frame. Treat this as hardware- and
resolution-specific rather than a verdict on RR -- on a desktop RTX part, or at
a lower output resolution, the arithmetic could easily flip. Re-measure before
assuming.

The in-engine a-trous / SVGF-lite denoiser (2.1) would cost ~0.5-1 ms instead of
8, and remains the right target. It is a larger change: AO and reflections are
composited inline in the camera pass (`color = A + ao*B + envSpecular*lerp(1,ao,
rough)`), so they must first be split into their own UAV targets, which needs
`kNumUavRegisters` raised past 8 (u0-u7, only u7 free) and a root-signature
change in `Dx12Device`.

### Two bugs found while measuring this

- **Changing `dlssMode` did not resize the DXR render target.** The render scale
  is baked in by `CreateDxrSmokeTarget`, and the ImGui combo calls
  `RecreateDxrTargets()` for exactly that reason -- but the startup/env path did
  not. The ray pass kept rendering at full resolution while DLSS was told it was
  upscaling: strictly more work than native, and the more aggressive the mode
  the slower it got (Performance measured *slower* than Quality). Fixed.

- **The bench caps at the display refresh.** VSync is force-disabled in
  `Go.cpp`, but in windowed flip without tearing support Present(0) is still
  composited at refresh, so every configuration faster than 8.33 ms reports
  exactly 8.33 ms (120.0 fps). All three DLSS SR modes hit this and are
  therefore indistinguishable in the table above. Any future measurement below
  ~8.4 ms on this machine is meaningless without addressing this.

- Benchmarks must now pass `BENCH_SCENE=Scenes/Sponza`. A new, currently empty
  `Scenes/Intel-Sponza.leveldata` sorts first and the harness picks the first
  `.tgs` it finds, which silently benchmarks an empty scene (0 draw calls).

---

## Session addendum 2026-09-16 (4): colour bleed and light leaking

Two separate problems, four compile-time switches at the top of
`DxrCommon.hlsli` (these shaders compile from source at runtime, so flipping one
is a file edit and a restart, not a rebuild).

### Colour bleed — `DXR_GI_DIRECTIONAL`

`EvaluateDxrGi` loaded all nine SH coefficients per probe and then used only
`sh[0]`. L0 is the direction-independent band, so the bounce was a flat ambient
term by construction: there was no mechanism by which a red wall could tint the
floor next to it. Colour bleed *is* the L1 band.

The reason it had been rolled back to L0-only was faceting, and the root cause
of that was the blend, not the bands. The old code evaluated irradiance
independently at each of the eight trilinear corner probes and blended the
results -- averaging eight different directional answers, which is
direction-blind and produces triangular facets on flat receivers. It now
accumulates the raw coefficients across the eight corners, normalises once, and
reconstructs once via `EvalGiSH`. That is exact, because SH reconstruction is
linear in the coefficients.

`DXR_GI_L1_WEIGHT` (0.85) and `DXR_GI_L2_WEIGHT` (0.5) damp the higher bands. L1
carries the directional bounce and keeps most of its energy; L2 is shape
refinement and rings on high-contrast transitions at this probe density, so it
is halved.

### Light leaking — `DXR_GI_VISIBILITY` and `DXR_TWO_SIDED_SHADOWS`

**Sun through geometry (`TraceShadowRay`).** Two independent leaks:

1. `RAY_FLAG_CULL_BACK_FACING_TRIANGLES`. Occlusion is not a two-sided question
   of taste -- geometry between a surface and the light blocks, whichever way
   its triangles wind. Culling back faces let the sun pass straight through any
   single-sided wall approached from its reverse side.
2. Rejecting **every** candidate sharing the receiver's instance. This was
   guarding against self-shadowing acne on thin folded cloth, but `ModelFactory`
   merges sub-meshes by material, so one "instance" is a large share of the
   scene: a wall could not shadow a floor that used the same material. This is
   the big one.

Now two-sided, rejecting only the originating triangle -- which is all the acne
guard ever needed, since the ray origin is already pushed off the surface by
`OffsetRayOrigin` / `ReconstructRaySurface`. `HitSurface` gained
`primitiveIndex` and it is threaded through `ShadeDirect`, `ShadeDiffuseDirect`,
`TraceSunVisibility` and `EvaluatePunctualLight`. `TraceAmbientOcclusion`
deliberately still has the old instance rejection (see its own note).

**GI through walls (`GiProbeVisibility`).** Each probe has stored 8x8
octahedral distance moments all along, and `EvaluateDxrGi` never read them, so a
probe on the far side of a wall lit surfaces through it. They are now used as a
Chebyshev occlusion weight. Two things differ from the earlier attempt that
produced circular pools: a normal bias of a quarter cell (so a receiver no
longer occludes itself against the wall it is sitting on), and a 0.08 floor on
the weight (so a probe is attenuated rather than hard-zeroed, which is what had
turned the falloff into a visible radial kernel).

### Measured

Sponza, architectural view (`BENCH_CAM=room BENCH_SHOT_FRAME=66`), all deltas
against the previous behaviour:

| change | mean abs diff | signed | note |
|---|---:|---:|---|
| shadow fix alone | 1.20/255 | -1.19 | darker = less leaked sun |
| + GI directional & visibility | 4.98/255 | -1.05 | |
| **both** | **6.15/255** | **-2.24** | |

Frame cost of both, warm, median of 3: **11.69 vs 11.32 ms, +0.37 ms** -- just
above the noise floor.

Scene brightness fell 0.3819 -> 0.3726 while contrast rose 0.2321 -> 0.2345:
less light arriving where it should not, slightly more range where it should.

**Caveat on attribution.** On Sponza the GI change dominates and the shadow fix
is modest, because most of Sponza's shadow casters already belong to different
instances than their receivers and the sun is close to overhead. On content
where a single merged material spans both caster and receiver -- which is
exactly the "sun penetrates through meshes" symptom -- the shadow fix should
matter far more. Re-check it there before concluding it is a small effect.

The GI volume must re-prime after any of these change, since the probes' stored
SH is computed with the shadow rays being fixed here.

---

## Session addendum 2026-09-16 (5): Bistro at ~5 fps

`BENCH_SCENE=Scenes/TEST` (Bistro exterior, 132 TLAS instances, 2048 GI
probes, 0 local lights). Metric is `gpu_ms.dxrRenderer` from the bench report,
because `frame_ms` is clamped at 100 ms and hides anything slower.

### Ruled out first

- **VRAM.** Sampled per-process during a run: 3.6 GB dedicated of 8.1 GB, 217 MB
  shared. No paging. (The 1.56 GB mesh cache does not all live on the GPU.)
- **GI priming.** 2048 probes at 8/frame is ~256 frames, but an 840-frame run
  sat at the same 79 ms after priming finished.
- **Today's changes.** Two-sided shadows are *faster* on Bistro (60.5 vs 67.1 ms
  with the old per-instance rejection), because the old code made shadow rays
  keep traversing past every rejected same-instance candidate. The GI
  directional/visibility lookup measured ~0.2 ms.

There is no history to compare "a while back" against: the entire DXR renderer
(every `dxr*` tunable, all of `EngineAssets/Shaders`) has never been committed.
**Commit it** -- without history, a regression like this cannot be bisected.

### Where the 79.8 ms went

| removed | camera pass |
|---|---:|
| baseline | 79.8 ms |
| **reflections** | **23.8 ms (-56)** |
| direct + shadows (primary hit only) | 73.8 ms (-6) |
| AO | 76.1 ms (-3.7) |
| GI lookup | 79.6 ms (~0) |

Plus fog ~12 ms in `dxrRendererToHdr`. Bistro is full of glossy surfaces, so
far more pixels fall under the 0.55 roughness cutoff than on Sponza, and every
one of the 4 samples ran a full `ShadeDirect` on its hit -- a 4-ray sun disc
each, i.e. 16 sun-shadow rays per reflective pixel. `TraceReflection` also
ignores `dxrDirectLighting`, which is why that toggle only showed 6 ms.

### Fixes applied

1. **One sun-shadow ray per reflection hit** (`DXR_REFLECTION_SUN_SAMPLES`,
   default 1; primary hits keep 4). 79.8 -> 60.5 ms. Converged image with
   auto-exposure off: **byte-identical** to 4 rays.
2. **Roughness-adaptive reflection sample count.** A near-mirror's GGX lobe is
   so narrow that extra samples land on the same point, and in the fade band
   above 0.7 x cutoff only `rayWeight` of the traced result survives. Samples
   now scale with `lobe * rayWeight` (still an unbiased average; only noise
   changes), and pixels with `rayWeight <= 0.01` skip tracing entirely.
   59.3 -> 33.2 ms. Converged image with auto-exposure off: mean abs diff
   **0.23/255, signed 0.00, 0% of pixels > 16 off**. (With auto-exposure on the
   diff read as a uniform +3.4/255 -- that was exposure adapting, not bias.)

**Bistro camera pass 79.8 -> ~33 ms. Sponza GPU frame ~11 -> 6.4 ms.** As with
the AO change, the cost of fewer samples is noise in *motion*, which a static
comparison cannot show -- check it moving.

### Levers measured but left for you to choose

| lever | Bistro result | trade-off |
|---|---|---|
| `BENCH_DXR_REFLECTION_CUTOFF=0.3` | 79.8 -> 26.8 ms (before fixes) | loses RT reflections on 0.3-0.55 surfaces |
| `BENCH_VOLUMETRIC_STEPS` 32/16/8 | fog 12.5 / 10.9 / 6.9 ms | banding, mostly hidden by jitter + TAA |
| `BENCH_DLSS_MODE=2` (Quality) | frame **14.4 ms** | see bug below -- not apples-to-apples |

New levers: `BENCH_DXR_DIRECT`, `BENCH_DXR_REFLECTION_CUTOFF`.

### Bug found: fog never renders under DLSS super-resolution

`ResolveDxrSmokeToHdr` skips fog when upscaling
(`superResolution ? false : RenderAtmosphere(true)`) on the grounds that it
must be applied after DLSS at display resolution -- but the later "atmosphere"
graph pass calls `RenderAtmosphere()` with `beforeTemporal = false`, and that
immediately returns for ray depth (`if (rayDepth && !beforeTemporal) return
false`). So with any DLSS SR mode, volumetric fog is silently dropped. The DLSS
Quality number above is missing ~7-12 ms of fog. Fixing it properly means
reconciling the render-resolution ray depth with a display-resolution fog pass
(or computing fog at render resolution before DLSS).

### On `dxr_reflection_optimization.md`

Useful and applied or already present: half-res tracing (via DLSS SR),
roughness-based termination, low spp + denoise, BLAS compaction and
`PREFER_FAST_TRACE` (already in `CreateRaytracingBlas`), and fetching only
IDs during traversal (`DecodeHit` already works that way). Instance masking is
valid and not yet done -- every instance and query uses `0xFF`.

Not applicable here: SER / `NvReorderThread` and payload sizing are `TraceRay`
pipeline concepts, and this engine has no hit shaders. Not accurate as written:
HLSL cannot choose per-thread execution order within a `Dispatch`, GPUs have no
"L2 cache" for Morton-ordered dispatch to exploit on NVIDIA, there is no "DXR
fallback" BLAS eviction, and all four references are bare `https://github.com`.
