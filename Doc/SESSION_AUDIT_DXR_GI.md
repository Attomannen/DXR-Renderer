# DXR/GI Session Audit — What Changed, What to Keep, What to Redo

Baseline for comparison: `P5G3-OKAY_RENDERING` (last known-good build, confirmed by a
fresh from-source rebuild that still renders correctly). Every line in this
document was verified by diffing the current source tree against that folder
with `diff --strip-trailing-cr` (plain `diff` falsely reports 100% of these
files as changed due to CRLF/LF differences alone — always strip that first).

Every file that differs from the baseline differs *only* because of the sesion
of work summarized below. There is no other source divergence anywhere in the
engine.

## Keep — confirmed, independently-verified bug fixes

These are not GI-theory changes. Each traces to a specific, reproducible
symptom with a root cause, unrelated to the GI regression below. Reverting
these would bring back real, confirmed bugs.

1. **`EngineAssets/Shaders/DeferredCompositePS.hlsl`** — called a function
   `AcesTonemap` that never existed anywhere in the codebase (the engine
   actually uses `AgX_Tonemap`, defined in `PostFxCommon.hlsli`), and separately
   used a vector condition on `?:` (`hdr == hdr ? hdr : 0` on a `float3`),
   which newer DXC rejects. This shader failing to compile is why the very
   first screenshot of this session was pure black — the tonemap/composite
   pass is the last step before the backbuffer, so nothing ever reached the
   screen. Fixed by calling `AgX_Tonemap` and switching to per-component
   scalar ternaries. **Keep.**

2. **`EngineAssets/Shaders/ExposureAdaptPS.hlsl`** — the manual-exposure branch
   ran a photographic EV100 formula (`multiplier = 1/(1.2*2^EV100)`) on
   `Tunables::manualExposure`, a value whose name, default (`1.0f`), doc
   comment ("manual 1.0 keeps the tuned look"), and UI slider range
   (0.05-8.0, labelled "Exposure") all agree is meant to be a plain linear
   multiplier. Plugging the default `1.0` into the EV100 formula gives
   `~0.417`, i.e. ~1.26 stops of unintended darkening applied by default,
   independent of GI/ambient/anything else. Fixed to use the value directly
   as the multiplier. **Keep.**

3. **`Source/Game/source/GameWorld.cpp`, the TGO per-instance texture-override
   block (~line 2609-2621)** — ran unconditionally for every mesh of every
   ordinary instance whenever `textureMeshIndex < MAX_MESHES_PER_MODEL`
   (true for virtually everything), even when `instance.GetTextures(...)`
   returned four nulls (no override authored — the overwhelmingly common
   case). It silently replaced the mesh's real, texture-bearing
   `materialIndex` (correctly set from `AssignDefaultMaterials`, one line
   above) with a brand-new, empty, per-instance record every frame.
   `DecodeHit`'s fallback for a record with no texture and no fixed material
   is a flat colour hashed from `materialIndex` alone — this is the
   "checkerboard/mosaic of flat colours extending to the horizon" bug.
   Confirmed fixed by the "Albedo" debug view screenshot showing real,
   correctly-bound textures afterward. Fixed by gating the block on
   `textures[0]||textures[1]||textures[2]||textures[3]` actually being
   non-null. **Keep.**

4. **`EngineAssets/Shaders/DxrCommon.hlsli`, `DecodeHit`'s no-ORM-texture
   default** — fell back to `roughness = 1.0` (fully rough) for any textured,
   non-fixed material with no bound ORM map, which disagrees with
   `TextureCooker`'s own "no source map" constant (`roughness = 0.5`).
   Produces a flat, saturated yellow wash `(ao=1, rough=1, metal=0)` in the
   ORM debug view for any material missing a cooked `_M` texture. Changed
   the fallback to `0.5` to match the cooker's convention. **Keep** (low
   risk, purely a "no data" default, does not touch any live-data path).

5. **`EngineAssets/Shaders/DxrCommon.hlsli`, `AcceptRayTriangle` +
   `Source/Graphics/age/render/RayTracingMaterialTable.h`** — every
   `RayQuery` in the DXR pipeline carries `RAY_FLAG_FORCE_NON_OPAQUE`, so
   every candidate triangle of every material, opaque included, ran through
   `AcceptRayTriangle`'s texture sample + threshold compare, even for
   ordinary opaque geometry (walls, floors) that never has cutout alpha.
   Extended `FixedMaterial::rayVisibility` from a 2-state value to 4
   (`kRayUnclassified` default = old always-sample behaviour, so anything
   that never classifies a material stays exactly as safe as before;
   `kRayTransparent`; `kRayMasked` = confirmed real cutout, still samples;
   `kRayOpaque` = confirmed no cutout, skips the sample entirely). Wired the
   classification in `GameWorld.cpp` from each material's real authored
   `surfaceType`. This is a performance fix, not a correctness fix — **keep**,
   but it's real engine-behaviour surface area, so if anything looks
   different post-revert it is the first place to check (should only ever
   make opaque geometry *faster*, never change what it looks like).

6. **Non-rendering, unrelated to any of the above**: the `.tgmat` write bug
   in `ScenePropertyTypes.cpp` (serialized all `MAX_MESHES_PER_MODEL` slots
   instead of just used ones), the skinned-mesh merge-by-material fix in
   `ModelFactory.cpp`, the TextureCooker NVTT false-availability bug and
   stale-output cleanup pass, and the Editor's light-selection-panel /
   HDR-environment-picker additions (`SceneObjectProperties.cpp`, `Scene.h`,
   `SceneSerialize.cpp`, `DefaultEditorGraphics.cpp`). None of these touch
   GI/lighting math and none are implicated by the regression. **Keep.**

## Revert — the actual suspects

A freshly-rebuilt, completely unmodified copy of `P5G3-OKAY_RENDERING`
(rebuilt from source today, not an old cached `.exe`) renders GI correctly.
The *only* files that differ between that tree and current are the ones
above (independently confirmed good) and the ones below. By elimination,
the GI regression is in this list.

1. **`EngineAssets/Shaders/GiTraceInlineCS.hlsl` — infinite-bounce feedback**
   (`SampleGiVolumeFeedback`, wired into the per-ray radiance accumulation).
   Idea: feed the volume's own last-converged SH state back into itself at
   each probe-ray hit, so double-occluded corners (lit only by light that
   has already bounced twice) pick up second/third-bounce light instead of
   being mathematically zero forever — the standard DDGI/Lumen "infinite
   bounce" trick. **Suspect.** The known risk already called out in the
   code's own comment: probes in the same batch run as independent thread
   groups with no ordering guarantee, so a feedback read can race a
   neighbour's write within one dispatch. Reasoned to be low-severity
   (never a torn read, self-corrects via hysteresis) — but reasoning is not
   proof, and this is the single most likely candidate for turning "probes
   have some data" into "probes read back as near-zero," e.g. if the
   trilinear feedback sample can return something that then gets clamped by
   the firefly guard, or if a subtly wrong sign/scale compounds every frame
   via the hysteresis blend instead of converging.

2. **`EngineAssets/Shaders/GiCommon.hlsli` (`EvalGiSHWindowed`) +
   `EngineAssets/Shaders/DxrCommon.hlsli` (`EvaluateDxrGi`) — coefficient-space
   SH blending with band windowing.** Idea: the baseline only used the L0
   (flat, direction-independent) SH band in the camera-facing GI lookup,
   with a comment explaining this was a deliberate rollback after full L1/L2
   SH caused visible triangular facets on flat receivers with this sparse a
   probe grid. Root-caused that facet bug to *how* it blended (evaluating
   irradiance independently at each of the 8 trilinear corner probes, then
   blending the results — direction-blind blending of direction-dependent
   values) rather than to using directional SH at all, and rewrote it to
   blend the raw SH coefficients across corners first and reconstruct once,
   with the higher bands damped (`wL1=0.85, wL2=0.5`) to control ringing.
   **Suspect**, for the same reason as above: mathematically checked
   correct against the standard L0/L1/L2 irradiance formulas and the
   standard SH basis constants (independently re-verified this session), but
   never confirmed to actually look right end-to-end, and it's the exact
   piece of math the user's own memory points at ("broke when implementing
   color bleed into shadows").

3. **`EngineAssets/Shaders/DxrCommon.hlsli` — second-bounce environment
   specular in `TraceReflection`.** Idea: a reflection ray's hit surface
   never got its own specular response (only `ShadeDirect`'s diffuse+direct
   term), so a mirror facing another glossy/metal surface went flat —
   "dark reflections." Added a Fresnel-weighted environment-cube sample at
   the hit's own reflection vector as a bounded, non-recursive 2-bounce cap.
   **Lower-risk suspect** — this only affects the reflection ray path
   (`TraceReflection`), not the probe-capture or camera-GI path directly, so
   it's a less likely cause of the GI symptom specifically, but it's new,
   unverified-in-practice behaviour in the same family of changes.

4. **`Source/Graphics/age/render/DeferredRenderer.h/.cpp` +
   `EngineAssets/Shaders/GiTraceInlineCS.hlsl` — batched probe dispatch**
   (`GiProjectProbeBatchRT`, `GiProbeBatchEntry`, one `Dispatch(batchSize,1,1)`
   instead of `batchSize` separate dispatches). Idea: probes were traced one
   Dispatch per probe from the CPU, which is why priming visibly took a long
   time. Batched multiple probes into one dispatch, indexed by
   `SV_GroupID.x`, sharing the per-frame-constant cbuffer fields (hysteresis/
   ray count/lighting) across the batch. **Suspect**, though probably the
   least likely of the four: this is a pure dispatch-shape change (same
   per-probe math, just more groups per call), the only new risk it
   introduces is the same infinite-bounce-feedback race noted in #1 above
   (present regardless of batching, just possibly more probes racing at
   once), and a real end-to-end build did complete and run without errors.
   Still worth reverting alongside the others for a clean baseline, then
   reintroducing on its own once GI is confirmed correct again, so it can be
   verified in isolation.

## Recommended path back to a working state

1. Revert `GiTraceInlineCS.hlsl`, `GiCommon.hlsli`, and the GI-related parts
   of `DxrCommon.hlsli` (`EvaluateDxrGi`, `TraceReflection`'s second-bounce
   addition) to exactly match `P5G3-OKAY_RENDERING`.
2. Revert `DeferredRenderer.h/.cpp`'s batched-dispatch addition back to the
   single-probe `GiProjectProbeRT`, and the corresponding call site in
   `GameWorld.cpp`'s `CaptureGiProbesImpl`.
3. Rebuild, confirm GI looks correct again (matching the rebuilt baseline).
4. Reintroduce the four ideas above **one at a time**, rebuilding and
   visually confirming GI after each, in this order (lowest-risk first):
   batched dispatch → second-bounce reflection specular → coefficient-space
   SH windowing → infinite-bounce feedback. Whichever one first makes GI
   go dark again is the actual bug, isolated to a single, small, re-testable
   change instead of four at once.
